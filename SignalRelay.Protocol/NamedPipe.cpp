#include "NamedPipe.h"

#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include "Platform.h"

namespace sr::ipc {

namespace {

// DACL: full access for the current user only. No Everyone, no network logons.
struct PipeSecurity {
    SECURITY_ATTRIBUTES sa{};
    PSECURITY_DESCRIPTOR sd = nullptr;
    PipeSecurity() {
        std::wstring sddl = L"D:P(A;;GA;;;" + platform::currentUserSid() + L")";
        if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
            sa.nLength = sizeof sa;
            sa.lpSecurityDescriptor = sd;
            sa.bInheritHandle = FALSE;
        }
    }
    ~PipeSecurity() {
        if (sd) LocalFree(sd);
    }
};

bool ownedByCurrentUser(HANDLE h) {
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetSecurityInfo(h, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS) return false;
    bool ok = false;
    LPWSTR str = nullptr;
    if (owner && ConvertSidToStringSidW(owner, &str)) {
        ok = platform::currentUserSid() == str;
        LocalFree(str);
    }
    // Elevated processes may create objects owned by BUILTIN\Administrators; accept only for the same session user.
    if (!ok && owner) {
        LPWSTR s2 = nullptr;
        if (ConvertSidToStringSidW(owner, &s2)) {
            ok = std::wstring(s2) == L"S-1-5-32-544";
            LocalFree(s2);
        }
    }
    LocalFree(sd);
    return ok;
}

}  // namespace

PipeConnection::PipeConnection(void* handle) : h_(handle) {
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeConnection::~PipeConnection() {
    close();
    if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);  // handle closed only once no thread can use it
    CloseHandle(readEvent_);
    CloseHandle(writeEvent_);
}

void PipeConnection::close() {
    if (open_.exchange(false) && h_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(h_, nullptr);
        DisconnectNamedPipe(h_);
    }
}

bool PipeConnection::send(const std::string& payload, unsigned timeoutMs) {
    std::string frame = encodeFrame(payload);
    if (frame.empty()) return false;
    std::lock_guard lock(writeMu_);
    std::size_t off = 0;
    while (off < frame.size()) {  // handle partial writes
        if (!open_) return false;
        OVERLAPPED ov{};
        ov.hEvent = writeEvent_;
        ResetEvent(writeEvent_);
        DWORD written = 0;
        BOOL ok = WriteFile(h_, frame.data() + off, static_cast<DWORD>(frame.size() - off), &written, &ov);
        if (!ok) {
            if (GetLastError() != ERROR_IO_PENDING) {
                close();
                return false;
            }
            if (WaitForSingleObject(writeEvent_, timeoutMs) != WAIT_OBJECT_0) {
                CancelIoEx(h_, &ov);
                GetOverlappedResult(h_, &ov, &written, TRUE);
                close();  // backpressure beyond the deadline: drop the connection, peer reconnects
                return false;
            }
            if (!GetOverlappedResult(h_, &ov, &written, FALSE)) {
                close();
                return false;
            }
        }
        off += written;
    }
    return true;
}

void PipeConnection::readLoop(const std::function<void(std::string&&)>& onFrame) {
    FrameDecoder dec;
    std::vector<char> buf(16 * 1024);
    while (open_) {
        OVERLAPPED ov{};
        ov.hEvent = readEvent_;
        ResetEvent(readEvent_);
        DWORD got = 0;
        BOOL ok = ReadFile(h_, buf.data(), static_cast<DWORD>(buf.size()), &got, &ov);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                if (!GetOverlappedResult(h_, &ov, &got, TRUE)) {
                    if (GetLastError() != ERROR_MORE_DATA) break;
                }
            } else if (err != ERROR_MORE_DATA) {
                break;
            }
        }
        if (got == 0) continue;
        dec.feed(buf.data(), got);
        std::string payload;
        for (;;) {
            auto st = dec.next(payload);
            if (st == FrameDecoder::State::NeedMore) break;
            if (st == FrameDecoder::State::Malformed) {
                close();  // close on malformed frames
                return;
            }
            onFrame(std::move(payload));
        }
    }
    close();
}

PipeServer::PipeServer(std::wstring name, unsigned maxInstances, OnConnection onConnection)
    : name_(std::move(name)), maxInstances_(maxInstances), onConnection_(std::move(onConnection)) {
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeServer::~PipeServer() {
    stop();
    CloseHandle(stopEvent_);
}

bool PipeServer::start() {
    if (running_) return true;
    running_ = true;
    ResetEvent(stopEvent_);
    thread_ = std::thread([this] { acceptLoop(); });
    return true;
}

void PipeServer::stop() {
    if (!running_.exchange(false)) return;
    SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard lock(workersMu_);
        for (auto& w : live_)
            if (auto c = w.lock()) c->close();
    }
    std::vector<std::thread> ws;
    {
        std::lock_guard lock(workersMu_);
        ws.swap(workers_);
    }
    for (auto& t : ws)
        if (t.joinable()) t.join();
}

void PipeServer::acceptLoop() {
    PipeSecurity sec;
    HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    while (running_) {
        DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
        if (firstInstance_) openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
        HANDLE h = CreateNamedPipeW(name_.c_str(), openMode, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, maxInstances_, 64 * 1024, 64 * 1024, 0,
                                    sec.sd ? &sec.sa : nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            // Another process owns the first instance (squatting or a second engine) or instances exhausted.
            if (WaitForSingleObject(stopEvent_, 500) == WAIT_OBJECT_0) break;
            continue;
        }
        firstInstance_ = false;
        OVERLAPPED ov{};
        ov.hEvent = connectEvent;
        ResetEvent(connectEvent);
        BOOL connected = ConnectNamedPipe(h, &ov);
        DWORD err = GetLastError();
        if (!connected && err == ERROR_IO_PENDING) {
            HANDLE waits[2] = {connectEvent, stopEvent_};
            DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
            if (w != WAIT_OBJECT_0) {
                CancelIoEx(h, &ov);
                CloseHandle(h);
                break;
            }
            DWORD dummy;
            connected = GetOverlappedResult(h, &ov, &dummy, FALSE);
        } else if (!connected && err == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        if (!connected) {
            CloseHandle(h);
            continue;
        }
        auto conn = std::make_shared<PipeConnection>(h);
        ULONG pid = 0;
        if (GetNamedPipeClientProcessId(h, &pid)) conn->peerPid_ = pid;
        std::lock_guard lock(workersMu_);
        // Reap finished workers so reconnect cycles do not accumulate threads.
        for (std::size_t i = 0; i < workers_.size();) {
            if (live_[i].expired()) {
                if (workers_[i].joinable()) workers_[i].join();
                workers_.erase(workers_.begin() + static_cast<std::ptrdiff_t>(i));
                live_.erase(live_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                ++i;
            }
        }
        live_.push_back(conn);
        workers_.emplace_back([this, c = std::move(conn)]() mutable {
            onConnection_(c);
            c->close();
            c.reset();  // expire the weak reference so the accept loop can reap this worker
        });
    }
    CloseHandle(connectEvent);
}

std::shared_ptr<PipeConnection> connectPipe(const std::wstring& name, unsigned timeoutMs, std::string* error) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            if (!ownedByCurrentUser(h)) {
                CloseHandle(h);
                if (error) *error = "pipe is not owned by the current user";
                return nullptr;
            }
            DWORD mode = PIPE_READMODE_BYTE;
            SetNamedPipeHandleState(h, &mode, nullptr, nullptr);
            auto c = std::make_shared<PipeConnection>(h);
            return c;
        }
        DWORD err = GetLastError();
        if (GetTickCount64() >= deadline) {
            if (error) *error = err == ERROR_FILE_NOT_FOUND ? "engine pipe not found (engine not running)" : "cannot connect to pipe (error " + std::to_string(err) + ")";
            return nullptr;
        }
        if (err == ERROR_PIPE_BUSY) WaitNamedPipeW(name.c_str(), 200);
        else Sleep(100);
    }
}

}  // namespace sr::ipc
