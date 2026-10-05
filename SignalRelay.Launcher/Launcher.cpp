// SignalRelay.Launcher.exe
// starts the last working version. a staged core release (versions\pending.json) is tried first and
// rolled back if the engine isn't healthy in time.

#include <windows.h>

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "SignalRelay.Protocol/Platform.h"

using namespace sr;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool launch(const std::wstring& exe, const std::wstring& args, HANDLE* processOut = nullptr) {
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, fs::path(exe).parent_path().c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    if (processOut) *processOut = pi.hProcess;
    else CloseHandle(pi.hProcess);
    return true;
}

std::optional<json> readJson(const std::wstring& path) {
    auto t = platform::readFileUtf8(path, 1024 * 1024);
    if (!t) return std::nullopt;
    json j = json::parse(*t, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

// Waits for the engine health marker to report the expected version.
bool waitHealthy(const std::string& version, unsigned timeoutMs) {
    const std::wstring marker = platform::dataDir() + L"\\engine.healthy";
    const ULONGLONG end = GetTickCount64() + timeoutMs;
    while (GetTickCount64() < end) {
        if (auto t = platform::readFileUtf8(marker, 4096)) {
            if (t->rfind(version + "\n", 0) == 0) return true;
        }
        Sleep(500);
    }
    return false;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring args = cmdLine ? cmdLine : L"";
    const std::wstring versionsDir = platform::ensureSubdir(L"versions");
    std::wstring target = platform::exeDir() + L"\\SignalRelay.exe";

    // Last working side-by-side version, if one was promoted earlier.
    if (auto active = readJson(versionsDir + L"\\active.json")) {
        std::wstring candidate = platform::widen(active->value("dir", "")) + L"\\SignalRelay.exe";
        if (fs::exists(candidate)) target = candidate;
    }

    if (auto pending = readJson(versionsDir + L"\\pending.json")) {
        const std::string version = pending->value("version", "");
        const std::wstring dir = platform::widen(pending->value("dir", ""));
        const std::wstring exe = dir + L"\\SignalRelay.exe";
        if (!version.empty() && fs::exists(exe)) {
            // Stop the running engine first (orderly) is the UI's job; here we only select what to start.
            DeleteFileW((platform::dataDir() + L"\\engine.healthy").c_str());
            HANDLE proc = nullptr;
            const bool started = launch(exe, args, &proc);
            if (started && waitHealthy(version, 30000)) {
                CloseHandle(proc);
                json active = {{"version", version}, {"dir", pending->value("dir", "")}, {"promotedAtMs", static_cast<std::int64_t>(GetTickCount64())}};
                platform::writeFileAtomic(versionsDir + L"\\active.json", active.dump(2));
                DeleteFileW((versionsDir + L"\\pending.json").c_str());
                return 0;
            }
            // Healthcheck failed: stop the candidate UI, keep the previous version and record the failure.
            if (proc) {
                TerminateProcess(proc, 1);
                CloseHandle(proc);
            }
            json failed = *pending;
            failed["failedReason"] = "startup healthcheck did not pass within 30 s";
            platform::writeFileAtomic(versionsDir + L"\\failed-" + platform::widen(version) + L".json", failed.dump(2));
            DeleteFileW((versionsDir + L"\\pending.json").c_str());
        }
    }
    if (!launch(target, args)) {
        MessageBoxW(nullptr, (L"SignalRelay could not be started:\n" + target).c_str(), L"SignalRelay", MB_ICONERROR);
        return 1;
    }
    return 0;
}
