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

TEST(launcher_detects_any_broker_install_once) {
    fs::path root = tempDir(L"terminals");
    fs::path normal = tempDir(L"hfm");  // e.g. C:\Program Files\HFM Metatrader 5, data under %APPDATA%
    fs::path portable = tempDir(L"portable-install");
    fs::path neverOpened = tempDir(L"never-opened");
    fs::path notMt5 = tempDir(L"not-mt5");
    for (const fs::path& d : {normal, portable, neverOpened}) platform::writeFileAtomic((d / L"terminal64.exe").wstring(), "MZ");
    fs::create_directories(portable / L"MQL5");
    fs::create_directories(root / L"E3E3B02889D32F38295D39BF94B6AD4A\\MQL5");
    writeUtf16(root / L"E3E3B02889D32F38295D39BF94B6AD4A\\origin.txt", normal.wstring() + L"\r\n");

    // normal is also an install dir (uninstall entry): listed once, through its data folder
    auto t = mt5::detectTerminals(root, {normal, portable, neverOpened, notMt5, portable});
    auto is = [&](int i, const char* k) { return t[i][k].get<bool>(); };
    CHECK(t.size() == 3);
    CHECK(is(0, "exeFound") && !is(0, "portable") && !is(0, "needsFirstRun"));
    CHECK(t[0]["dataPath"] == platform::narrow((root / L"E3E3B02889D32F38295D39BF94B6AD4A").wstring()));
    CHECK(is(1, "portable") && t[1]["dataPath"] == platform::narrow(portable.wstring()));
    CHECK(is(2, "needsFirstRun") && t[2]["dataPath"] == "" && t[2]["exePath"] == platform::narrow((neverOpened / L"terminal64.exe").wstring()));
    CHECK(!is(2, "hasEA") && !is(2, "hasDll"));

    CHECK(mt5::detectTerminals(tempDir(L"no-terminals") / L"missing", {}).empty());
}

TEST(launcher_checks_a_chosen_folder) {
    fs::path root = tempDir(L"chosen-root");
    auto check = [](const nlohmann::json& r, std::size_t i) { return r["checks"][i]["ok"].get<bool>(); };

    auto none = mt5::checkFolder(tempDir(L"chosen-empty"), root);
    CHECK(!none["ok"].get<bool>() && none["terminal"].is_null() && !check(none, 0));

    fs::path mt4 = tempDir(L"chosen-mt4");
    platform::writeFileAtomic((mt4 / L"terminal.exe").wstring(), "MZ");
    CHECK(mt5::checkFolder(mt4, root)["checks"][0]["detail"].get<std::string>().find("MetaTrader 4") != std::string::npos);

    // a terminal64.exe without MetaTrader 5 version info is found but refused
    fs::path install = tempDir(L"chosen-install");
    platform::writeFileAtomic((install / L"terminal64.exe").wstring(), "MZ");
    auto fake = mt5::checkFolder(install, root);
    CHECK(!fake["ok"].get<bool>() && check(fake, 0) && !check(fake, 1));
    CHECK(fake["terminal"]["needsFirstRun"].get<bool>());

    // choosing the data folder instead leads to the install folder through origin.txt
    fs::path data = root / L"ABCDEF";
    fs::create_directories(data / L"MQL5");
    writeUtf16(data / L"origin.txt", install.wstring() + L"\r\n");
    auto viaData = mt5::checkFolder(data, root);
    CHECK(viaData["terminal"]["exePath"] == platform::narrow((install / L"terminal64.exe").wstring()));
    CHECK(viaData["terminal"]["dataPath"] == platform::narrow(data.wstring()));
    CHECK(viaData["checks"].size() == 4 && check(viaData, 3));  // its MQL5 folder is writable
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

    // A never-opened install (no data folder) whose terminal64.exe is gone fails the same way.
    req.dataPath.clear();
    req.exePath = tempDir(L"gone") / L"terminal64.exe";
    threw = false;
    try {
        mt5::launch(req);
    } catch (const std::runtime_error& e) {
        threw = std::string(e.what()).find("terminal64.exe") != std::string::npos;
    }
    CHECK(threw);
}
