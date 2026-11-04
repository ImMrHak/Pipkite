// Pipkite.Launcher.exe
// starts Pipkite.exe next to it (shortcuts point here). new versions are installed by the setup program.

#include <windows.h>

#include <string>

#include "Pipkite.Protocol/Platform.h"

using namespace sr;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    const std::wstring exe = platform::exeDir() + L"\\Pipkite.exe";
    std::wstring cmd = L"\"" + exe + L"\" " + (cmdLine ? cmdLine : L"");
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, platform::exeDir().c_str(), &si, &pi)) {
        MessageBoxW(nullptr, (L"Pipkite could not be started:\n" + exe).c_str(), L"Pipkite", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
