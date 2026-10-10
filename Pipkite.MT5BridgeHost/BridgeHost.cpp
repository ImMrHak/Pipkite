// MT5BridgeHost.dll, imported by the EA. owns the pipe worker, the queues, the EA journal and the
// reloadable implementation dll. exports never block, worker threads never call into MQL.
// this dll is never unloaded while the EA runs, implementations get swapped behind it.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Envelope.h"
#include "Pipkite.Protocol/NamedPipe.h"
#include "Pipkite.Protocol/Platform.h"
#include "PipkitePluginAbi.h"

using nlohmann::json;
using namespace sr;

namespace {

constexpr int kHostAbiVersion = 1;
constexpr std::size_t kControlCapacity = 256;
constexpr std::size_t kExecCapacity = 256;
constexpr std::size_t kOutCapacity = 1024;

enum Status { Disconnected = 0, Connecting = 1, Authenticated = 2, Paired = 3, Error = -1 };

// implementation module with lease-based lifetime
struct ImplModule {
    HMODULE module = nullptr;
    const SrBridgeImplApi* api = nullptr;
    std::string version;
    ~ImplModule() {
        if (module) FreeLibrary(module);  // runs only when no lease remains -> never unloads executing code
    }
};

std::shared_ptr<ImplModule> loadImpl(const std::wstring& path, const std::string& expectedSha256, std::string& error) {
    auto hash = crypto::sha256File(path);
    if (!hash) {
        error = "implementation file not found";
        return nullptr;
    }
    if (!expectedSha256.empty() && crypto::toHex(*hash) != expectedSha256) {
        error = "implementation hash mismatch";
        return nullptr;
    }
    HMODULE h = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) {
        error = "LoadLibrary failed (" + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    auto getApi = reinterpret_cast<SrBridgeImplGetApiFn>(GetProcAddress(h, "SrBridgeImplGetApi"));
    const SrBridgeImplApi* api = getApi ? getApi() : nullptr;
    if (!api || api->abiVersion != SR_BRIDGE_IMPL_ABI_VERSION || !api->toEa || !api->fromEa || !api->healthcheck || !api->version) {
        FreeLibrary(h);
        error = "implementation ABI mismatch";
        return nullptr;
    }
    if (api->healthcheck() != 1) {
        FreeLibrary(h);
        error = "implementation healthcheck failed";
        return nullptr;
    }
    auto m = std::make_shared<ImplModule>();
    m->module = h;
    m->api = api;
    m->version = api->version();
    return m;
}

// host state
struct Host {
    std::mutex mu;  // guards queues and small state; never held during I/O
    std::deque<std::string> controlIn, execIn, out;
    std::atomic<int> status{Disconnected};
    std::string statusText = "not initialized";
    std::wstring terminalId, server;
    std::int64_t login = 0;
    bool simulated = false;
    std::atomic<bool> running{false};
    std::atomic<bool> upgrading{false};
    std::thread worker;
    std::shared_ptr<ipc::PipeConnection> conn;
    std::mutex implMu;
    std::shared_ptr<ImplModule> impl;
    std::condition_variable outCv;
    HANDLE inboundEvent = nullptr;  // optional wake-up for harnesses/native hosts; the EA does not need it

    // journal
    std::mutex journalMu;
    std::wstring journalPath;
    std::map<std::string, std::string> journal;  // commandId -> last state

    std::string accountKey() const { return platform::narrow(terminalId) + "|" + std::to_string(login) + "|" + platform::narrow(server); }

    std::shared_ptr<ImplModule> lease() {
        std::lock_guard lock(implMu);
        return impl;
    }
    void setStatus(int s, const std::string& text) {
        status = s;
        std::lock_guard lock(mu);
        statusText = text;
    }
};

Host* g_host = nullptr;
std::mutex g_initMu;

std::string convert(const std::shared_ptr<ImplModule>& m, bool toEa, const std::string& in) {
    if (!m) return {};
    std::string out(8192, '\0');
    for (int attempt = 0; attempt < 2; ++attempt) {
        int32_t n = toEa ? m->api->toEa(in.data(), static_cast<int32_t>(in.size()), out.data(), static_cast<int32_t>(out.size()))
                         : m->api->fromEa(in.data(), static_cast<int32_t>(in.size()), out.data(), static_cast<int32_t>(out.size()));
        if (n > 0) {
            out.resize(static_cast<std::size_t>(n));
            return out;
        }
        if (n == 0) return {};
        out.assign(static_cast<std::size_t>(-n), '\0');
    }
    return {};
}

void loadJournal(Host& h) {
    const auto key = crypto::toHex(crypto::sha256(h.accountKey())).substr(0, 16);
    h.journalPath = platform::ensureSubdir(L"ea_journal") + L"\\" + platform::widen(key) + L".log";
    std::ifstream f(h.journalPath);
    std::string line;
    while (std::getline(f, line)) {
        auto p1 = line.find('|');
        auto p2 = line.find('|', p1 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos) continue;
        h.journal[line.substr(0, p1)] = line.substr(p1 + 1, p2 - p1 - 1);
    }
}

bool appendJournal(Host& h, const std::string& commandId, const std::string& state, const std::string& detail) {
    std::lock_guard lock(h.journalMu);
    std::string line = commandId + "|" + state + "|" + std::to_string(GetTickCount64()) + "|" + detail + "\n";
    for (char& c : line)
        if (c == '\r') c = ' ';
    HANDLE f = CreateFileW(h.journalPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) && written == line.size();
    ok = ok && FlushFileBuffers(f);  // durable before the EA submits to the broker
    CloseHandle(f);
    if (ok) h.journal[commandId] = state;
    return ok;
}

void sendEnvelope(Host& h, const std::shared_ptr<ipc::PipeConnection>& c, const std::string& type, json payload) {
    ipc::Envelope e;
    e.type = type;
    e.accountKey = h.accountKey();
    e.payload = std::move(payload);
    if (c) c->send(e.serialize());
}

void handleUpgrade(Host& h, const std::shared_ptr<ipc::PipeConnection>& c, const json& p) {
    h.upgrading = true;
    // Drain: wait briefly for the EA to consume queued execution messages (engine already inhibited dispatch).
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard lock(h.mu);
            if (h.execIn.empty()) break;
        }
        Sleep(50);
    }
    std::string error;
    bool drained;
    {
        std::lock_guard lock(h.mu);
        drained = h.execIn.empty();
    }
    std::shared_ptr<ImplModule> next;
    if (!drained) error = "execution queue did not drain; upgrade postponed";
    else next = loadImpl(platform::widen(p.value("path", "")), p.value("sha256", ""), error);
    std::string previous;
    if (next) {
        std::lock_guard lock(h.implMu);
        previous = h.impl ? h.impl->version : "";
        h.impl = next;  // atomic switch at a message boundary; old module retires when its leases drop
    }
    h.upgrading = false;
    sendEnvelope(h, c, "BRIDGE_UPGRADE_RESULT", {{"ok", next != nullptr}, {"version", next ? next->version : ""}, {"previous", previous}, {"error", error}});
}

void workerLoop(Host& h) {
    int backoffMs = 500;
    while (h.running) {
        h.setStatus(Connecting, "connecting to Pipkite engine");
        std::string err;
        auto c = ipc::connectPipe(platform::pipeName(L"bridge"), 1500, &err);
        if (!c) {
            h.setStatus(Disconnected, "engine not reachable: " + err);
            for (int t = 0; t < backoffMs && h.running; t += 100) Sleep(100);
            backoffMs = std::min(backoffMs * 2, 5000);
            continue;
        }
        auto secret = platform::loadSecret(L"pairing");
        if (!secret) {
            h.setStatus(Error, "pairing secret missing: start Pipkite once on this Windows account");
            c->close();
            for (int t = 0; t < 3000 && h.running; t += 100) Sleep(100);
            continue;
        }
        backoffMs = 500;
        const std::string nonceB = crypto::toHex(crypto::randomBytes(16));
        std::string nonceE;
        auto implLease = h.lease();
        sendEnvelope(h, c, ipc::msg::HELLO,
                     {{"terminalId", platform::narrow(h.terminalId)}, {"login", std::to_string(h.login)}, {"server", platform::narrow(h.server)}, {"nonce", nonceB},
                      {"hostVersion", kHostAbiVersion}, {"implVersion", implLease ? implLease->version : ""}, {"simulated", h.simulated}});
        {
            std::lock_guard lock(h.mu);
            h.conn = c;
        }
        int stage = 0;  // 0 await CHALLENGE, 1 await WELCOME, 2 running
        std::thread writer([&h, c] {
            while (c->open() && h.running) {
                std::string item;
                {
                    std::unique_lock lock(h.mu);
                    h.outCv.wait_for(lock, std::chrono::milliseconds(200), [&] { return !h.out.empty() || !c->open() || !h.running; });
                    if (h.out.empty() || h.status.load() < Authenticated) continue;
                    item = std::move(h.out.front());
                    h.out.pop_front();
                }
                std::string js = convert(h.lease(), false, item);
                if (js.empty()) continue;  // malformed EA message; dropped with no side effects
                json j = json::parse(js, nullptr, false);
                if (j.is_discarded()) continue;
                j["accountKey"] = h.accountKey();
                if (!c->send(j.dump())) {
                    std::lock_guard lock(h.mu);
                    h.out.push_front(std::move(item));  // keep trade events for the next connection
                    break;
                }
            }
        });
        c->readLoop([&](std::string&& frame) {
            std::string perr;
            auto env = ipc::parseEnvelope(frame, &perr);
            if (!env) {
                c->close();
                return;
            }
            if (stage == 0) {
                if (env->type != ipc::msg::CHALLENGE) {
                    c->close();
                    return;
                }
                nonceE = env->payload.value("nonce", "");
                auto expect = crypto::hmacSha256(*secret, "engine|" + nonceB + "|" + nonceE);
                auto proof = crypto::fromHex(env->payload.value("proof", ""));
                if (!proof || !crypto::constantTimeEqual(*proof, expect) || nonceE.size() != 32) {
                    h.setStatus(Error, "engine authentication failed (pairing secret mismatch)");
                    c->close();
                    return;
                }
                auto mine = crypto::hmacSha256(*secret, "bridge|" + nonceE + "|" + nonceB + "|" + h.accountKey());
                sendEnvelope(h, c, ipc::msg::AUTH, {{"proof", crypto::toHex(mine)}});
                stage = 1;
                return;
            }
            if (stage == 1) {
                if (env->type != ipc::msg::WELCOME) {
                    c->close();
                    return;
                }
                const bool paired = env->payload.value("paired", false);
                h.setStatus(paired ? Paired : Authenticated, paired ? "paired with Pipkite engine" : "authenticated; waiting for pairing approval in Pipkite");
                stage = 2;
            }
            if (env->type == "BRIDGE_UPGRADE") {
                std::thread([&h, c, p = env->payload] { handleUpgrade(h, c, p); }).detach();
                return;
            }
            if (env->type == ipc::msg::WELCOME) {
                const bool paired = env->payload.value("paired", false);
                h.setStatus(paired ? Paired : Authenticated, paired ? "paired with Pipkite engine" : "authenticated; waiting for pairing approval in Pipkite");
            }
            std::string kv = convert(h.lease(), true, frame);
            if (kv.empty()) return;
            const bool isExec = env->type == ipc::msg::EXECUTE;
            bool rejected = false;
            {
                std::lock_guard lock(h.mu);
                auto& q = isExec ? h.execIn : h.controlIn;
                if (q.size() >= (isExec ? kExecCapacity : kControlCapacity)) rejected = true;
                else q.push_back(std::move(kv));
            }
            if (!rejected && h.inboundEvent) SetEvent(h.inboundEvent);
            if (rejected && isExec) {
                // Backpressure: never silently discard an executable command.
                sendEnvelope(h, c, ipc::msg::COMMAND_STATUS,
                             {{"commandId", env->payload.value("commandId", "")}, {"state", "Rejected"}, {"reason", "bridge execution queue full"}, {"submitted", false}});
            }
        });
        c->close();
        h.outCv.notify_all();
        writer.join();
        {
            std::lock_guard lock(h.mu);
            h.conn.reset();
        }
        if (h.running) h.setStatus(Disconnected, "connection to engine lost; reconnecting");
    }
}

void setOutString(const std::string& s, wchar_t* buf, int cap) {
    std::wstring w = platform::widen(s);
    if (!buf || cap <= 0) return;
    int n = static_cast<int>(std::min<std::size_t>(w.size(), static_cast<std::size_t>(cap - 1)));
    std::memcpy(buf, w.data(), static_cast<std::size_t>(n) * sizeof(wchar_t));
    buf[n] = L'\0';
}

}  // namespace

extern "C" {

__declspec(dllexport) int BridgeVersion() { return kHostAbiVersion; }

// Returns 1 on success; negative codes: -1 already initialized for another account, -2 implementation unavailable.
__declspec(dllexport) int BridgeInitialize(const wchar_t* terminalId, long long login, const wchar_t* server, int simulated) {
    std::lock_guard lock(g_initMu);
    if (g_host) {
        // Exactly one copier owner per terminal/account: a second initialization for another account is refused.
        if (g_host->login == login && g_host->server == (server ? server : L"")) return 1;
        return -1;
    }
    auto h = new Host();
    h->terminalId = terminalId ? terminalId : L"";
    h->login = login;
    h->server = server ? server : L"";
    h->simulated = simulated != 0;
    loadJournal(*h);
    h->inboundEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    // Load the active implementation recorded by the engine.
    std::string error = "no active implementation recorded";
    auto cfg = platform::readFileUtf8(platform::ensureSubdir(L"bridge") + L"\\active_impl.json", 64 * 1024);
    if (cfg) {
        json j = json::parse(*cfg, nullptr, false);
        if (!j.is_discarded()) h->impl = loadImpl(platform::widen(j.value("path", "")), j.value("sha256", ""), error);
    }
    if (!h->impl) {
        h->statusText = "bridge implementation unavailable: " + error;
        h->status = Error;
        g_host = h;
        return -2;
    }
    h->running = true;
    h->worker = std::thread([h] { workerLoop(*h); });
    g_host = h;
    return 1;
}

// Returns bytes copied (>0), 0 when nothing is queued, or -needed when capacity is too small.
__declspec(dllexport) int BridgeTryReceive(unsigned char* buf, int capacity) {
    Host* h = g_host;
    if (!h || !buf || capacity <= 0) return 0;
    std::lock_guard lock(h->mu);
    std::deque<std::string>* q = !h->controlIn.empty() ? &h->controlIn : (!h->upgrading && !h->execIn.empty() ? &h->execIn : nullptr);
    if (!q) return 0;
    const std::string& front = q->front();
    if (static_cast<int>(front.size()) > capacity) return -static_cast<int>(front.size());
    std::memcpy(buf, front.data(), front.size());
    int n = static_cast<int>(front.size());
    q->pop_front();
    return n;
}

// Returns 1 when queued, 0 when the outbound queue is full (EA retries later), -1 on invalid input.
__declspec(dllexport) int BridgeTrySend(const unsigned char* buf, int len) {
    Host* h = g_host;
    if (!h || !buf || len <= 0 || len > 64 * 1024) return -1;
    {
        std::lock_guard lock(h->mu);
        if (h->out.size() >= kOutCapacity) return 0;
        h->out.emplace_back(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(len));
    }
    h->outCv.notify_one();
    return 1;
}

__declspec(dllexport) int BridgeConnectionStatus() { return g_host ? g_host->status.load() : Disconnected; }

__declspec(dllexport) int BridgeStatusText(wchar_t* buf, int capacity) {
    if (!g_host) {
        setOutString("not initialized", buf, capacity);
        return 0;
    }
    std::string s;
    {
        std::lock_guard lock(g_host->mu);
        s = g_host->statusText;
    }
    auto impl = g_host->lease();
    s += impl ? " [impl " + impl->version + "]" : " [no impl]";
    setOutString(s, buf, capacity);
    return static_cast<int>(s.size());
}

// Durable EA journal: returns 1 when flushed to disk. The EA never submits a command whose acceptance
// record could not be journaled.
__declspec(dllexport) int BridgeJournalAppend(const wchar_t* commandId, const wchar_t* state, const wchar_t* detail) {
    if (!g_host || !commandId || !state) return 0;
    return appendJournal(*g_host, platform::narrow(commandId), platform::narrow(state), detail ? platform::narrow(detail) : "") ? 1 : 0;
}

// Returns the last journaled state for a command (used to deduplicate redelivered commands).
__declspec(dllexport) int BridgeJournalState(const wchar_t* commandId, wchar_t* buf, int capacity) {
    if (!g_host || !commandId) return 0;
    std::lock_guard lock(g_host->journalMu);
    auto it = g_host->journal.find(platform::narrow(commandId));
    if (it == g_host->journal.end()) {
        setOutString("", buf, capacity);
        return 0;
    }
    setOutString(it->second, buf, capacity);
    return static_cast<int>(it->second.size());
}

// Monotonic QueryPerformanceCounter-based microseconds shared with the engine process (same machine clock),
// used for cross-process latency measurement of the EA send invocation.
__declspec(dllexport) long long BridgeNowMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Auto-reset event signaled when inbound messages are queued. Native hosts (e.g. the test harness) can
// wait on it instead of polling; MQL5 EAs keep using their timer/tick events.
__declspec(dllexport) void* BridgeInboundEvent() { return g_host ? g_host->inboundEvent : nullptr; }

__declspec(dllexport) void BridgeShutdown() {
    std::lock_guard lock(g_initMu);
    Host* h = g_host;
    if (!h) return;
    h->running = false;
    {
        std::lock_guard lock2(h->mu);
        if (h->conn) h->conn->close();
    }
    h->outCv.notify_all();
    if (h->worker.joinable()) h->worker.join();
    g_host = nullptr;
    if (h->inboundEvent) CloseHandle(h->inboundEvent);
    delete h;
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
