// SignalRelay.Engine.exe, native C++20 engine (never compiled under /clr).
//   (no args)                       run the engine in the current user session (started by the UI)
//   --console                       run with diagnostic console output
//   --validate-module <manifest> --out <file>   helper-process validation of a module (used by PluginManager)
//   --version                       print the version

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "Engine.h"
#include "SignalRelay.Core/Models.h"
#include "SignalRelay.Protocol/Platform.h"

using namespace sr;

namespace {

engine::Engine* g_engine = nullptr;

BOOL WINAPI onConsoleCtrl(DWORD) {
    if (g_engine) g_engine->requestStop();
    return TRUE;
}

// A staged full-profile restore is applied before the store opens; the previous database is kept as a
// rollback backup next to it.
void applyPendingRestore() {
    namespace fs = std::filesystem;
    const fs::path dir = platform::dataDir();
    const fs::path pending = dir / L"restore_pending.sqlite";
    if (!fs::exists(pending)) return;
    const fs::path db = dir / L"signalrelay.db";
    std::error_code ec;
    const auto stamp = std::to_wstring(GetTickCount64());
    if (fs::exists(db)) fs::rename(db, dir / (L"signalrelay.before-restore-" + stamp + L".db"), ec);
    fs::remove(dir / L"signalrelay.db-wal", ec);
    fs::remove(dir / L"signalrelay.db-shm", ec);
    fs::rename(pending, db, ec);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    // no loader error dialogs: tdjson/openssl can be missing or blocked by Smart App Control,
    // and a modal dialog would hang the engine. we just report telegram as unavailable.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    SetConsoleOutputCP(CP_UTF8);
    bool console = false;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--version") {
            std::printf("%s %s\n", kProductName, kProductVersion);
            return 0;
        }
        if (a == L"--validate-module" && i + 1 < argc) {
            std::wstring out;
            for (int k = i + 2; k + 1 < argc; ++k)
                if (std::wstring(argv[k]) == L"--out") out = argv[k + 1];
            if (out.empty()) return 2;
            return engine::PluginManager::runValidationHelper(argv[i + 1], out);
        }
        if (a == L"--console") console = true;
    }

    // One engine per Windows user session.
    const std::wstring mutexName = L"Local\\SignalRelay.Engine." + platform::currentUserSid() + platform::instanceSuffix();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (console) std::printf("SignalRelay engine is already running for this user.\n");
        return 3;
    }

    // Windows 11 applies power throttling (EcoQoS) to windowless background processes, which slows the
    // latency-critical path. Opt out of execution-speed throttling only; priority stays normal (no boost).
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof throttling);

    applyPendingRestore();
    engine::EngineOptions opt;
    opt.console = console;
    auto eng = std::make_unique<engine::Engine>(opt);
    g_engine = eng.get();
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);
    auto st = eng->start();
    if (!st) {
        std::fprintf(stderr, "SignalRelay engine failed to start: %s\n", st.error().message.c_str());
        platform::writeFileAtomic(platform::dataDir() + L"\\engine.lasterror.txt", st.error().message);
        return 1;
    }
    // Health marker for the launcher's startup healthcheck (last working version selection).
    platform::writeFileAtomic(platform::dataDir() + L"\\engine.healthy", std::string(kProductVersion) + "\n" + std::to_string(GetCurrentProcessId()));
    if (console) std::printf("%s engine %s running. Data: %s\n", kProductName, kProductVersion, platform::narrow(platform::dataDir()).c_str());
    eng->waitForStop();
    if (console) std::printf("Stopping (orderly shutdown)...\n");
    g_engine = nullptr;
    eng.reset();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
