// MetaTrader launcher file logic (no terminal is started here): locating terminal64.exe from a data folder,
// detecting an already attached EA in the active chart profile, and installing the EA files.
#include <windows.h>

#include <filesystem>
#include <string>

#include "Pipkite.Engine/TerminalLauncher.h"
#include "Pipkite.Protocol/Platform.h"
#include "TestFramework.h"

using namespace sr;
namespace fs = std::filesystem;

namespace {

fs::path tempDir(const wchar_t* name) {
    wchar_t buf[MAX_PATH];
    GetTempPathW(MAX_PATH, buf);
    fs::path p = fs::path(buf) / (std::wstring(L"sr-launcher-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

// MetaTrader's own files are UTF-16LE with a BOM.
void writeUtf16(const fs::path& p, const std::wstring& text) {
    fs::create_directories(p.parent_path());
    std::string bytes = "\xFF\xFE";
    bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    platform::writeFileAtomic(p.wstring(), bytes);
}

}  // namespace

TEST(launcher_finds_terminal_exe_from_origin_or_portable_folder) {
    fs::path data = tempDir(L"origin");
    fs::path install = tempDir(L"install");
    fs::create_directories(data / L"MQL5");
    writeUtf16(data / L"origin.txt", install.wstring() + L"\r\n");
    CHECK(mt5::exeForDataPath(data) == install / L"terminal64.exe");

    // Portable installs keep MQL5 next to terminal64.exe; the data folder is the install folder.
    fs::path portable = tempDir(L"portable");
    fs::create_directories(portable / L"MQL5");
    platform::writeFileAtomic((portable / L"terminal64.exe").wstring(), "MZ");
    CHECK(mt5::exeForDataPath(portable) == portable / L"terminal64.exe");

    CHECK(mt5::exeForDataPath(tempDir(L"empty")).empty());
    CHECK(!mt5::isRunning(install / L"terminal64.exe"));
}

TEST(launcher_detects_ea_only_in_the_active_profile) {
    fs::path data = tempDir(L"profile");
    writeUtf16(data / L"config\\common.ini", L"[Charts]\r\nProfileLast=Euro\r\nProfile=1\r\n");
    writeUtf16(data / L"MQL5\\Profiles\\Charts\\Default\\chart01.chr", L"<chart>\r\n<expert>\r\nname=PipkiteEA\r\n</expert>\r\n</chart>\r\n");
    writeUtf16(data / L"MQL5\\Profiles\\Charts\\Euro\\chart01.chr", L"<chart>\r\nsymbol=EURUSD\r\n</chart>\r\n");
    CHECK(!mt5::activeProfileHasEa(data));  // only the inactive "Default" profile has it: attach on start

    writeUtf16(data / L"MQL5\\Profiles\\Charts\\Euro\\chart02.chr", L"<chart>\r\n<expert>\r\nname=PipkiteEA\r\npath=Experts\\Pipkite\\PipkiteEA.ex5\r\n</expert>\r\n</chart>\r\n");
    CHECK(mt5::activeProfileHasEa(data));  // already attached: never add a second copier EA

    fs::path noConfig = tempDir(L"noconfig");  // missing common.ini falls back to the Default profile
    writeUtf16(noConfig / L"MQL5\\Profiles\\Charts\\Default\\chart01.chr", L"name=PipkiteEA\r\n");
    CHECK(mt5::activeProfileHasEa(noConfig));
}

TEST(launcher_installs_ea_and_bridge_and_rejects_non_mt5_folders) {
    fs::path install = tempDir(L"srinstall");
    fs::create_directories(install / L"mql5");
    platform::writeFileAtomic((install / L"mql5\\PipkiteEA.ex5").wstring(), "EX5");
    platform::writeFileAtomic((install / L"mql5\\PipkiteEA.mq5").wstring(), "MQ5");
    platform::writeFileAtomic((install / L"MT5BridgeHost.dll").wstring(), "DLL");
    fs::path data = tempDir(L"mt5data");
    fs::create_directories(data / L"MQL5");

    auto r = mt5::installEa(install, data);
    CHECK(r["copied"].size() == 3);
    CHECK(fs::exists(data / L"MQL5\\Experts\\Pipkite\\PipkiteEA.ex5"));
    CHECK(fs::exists(data / L"MQL5\\Libraries\\MT5BridgeHost.dll"));

    bool threw = false;
    try {
        mt5::installEa(install, tempDir(L"notmt5"));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // Starting requires a real terminal64.exe; a data folder without one fails with a clear message.
    mt5::LaunchRequest req;
    req.installDir = install;
    req.appDataDir = tempDir(L"appdata");
    req.dataPath = data;
    threw = false;
    try {
        mt5::launch(req);
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("terminal64.exe") != std::string::npos;
    }
    CHECK(threw);
}
