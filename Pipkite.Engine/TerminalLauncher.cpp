#include "TerminalLauncher.h"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <stdexcept>
#include <vector>

#include "Pipkite.Protocol/Platform.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace sr::mt5 {
namespace {

// MetaTrader writes its .ini/.chr/origin.txt files as UTF-16LE with a BOM; accept UTF-8/ANSI too.
std::wstring readText(const fs::path& p) {
    auto bytes = platform::readFileUtf8(p.wstring(), 4 * 1024 * 1024);
    if (!bytes || bytes->empty()) return {};
    const std::string& b = *bytes;
    if (b.size() >= 2 && static_cast<unsigned char>(b[0]) == 0xFF && static_cast<unsigned char>(b[1]) == 0xFE)
        return std::wstring(reinterpret_cast<const wchar_t*>(b.data() + 2), (b.size() - 2) / 2);
    if (b.size() >= 2 && b[1] == '\0') return std::wstring(reinterpret_cast<const wchar_t*>(b.data()), b.size() / 2);
    return platform::widen(b);
}

std::wstring trim(std::wstring s) {
    while (!s.empty() && std::iswspace(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::iswspace(s[i])) ++i;
    return s.substr(i);
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring iniValue(const std::wstring& text, const std::wstring& key) {
    std::size_t pos = 0;
    const std::wstring k = lower(key) + L"=";
    while (pos < text.size()) {
        std::size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = trim(text.substr(pos, end - pos));
        if (lower(line).rfind(k, 0) == 0) return trim(line.substr(k.size()));
        pos = end + 1;
    }
    return {};
}

bool writeUtf16(const fs::path& p, const std::wstring& text) {
    std::string bytes = "\xFF\xFE";
    bytes.append(reinterpret_cast<const char*>(text.data()), text.size() * sizeof(wchar_t));
    return platform::writeFileAtomic(p.wstring(), bytes);
}

bool samePath(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    if (fs::equivalent(a, b, ec)) return true;
    return lower(fs::absolute(a, ec).lexically_normal().wstring()) == lower(fs::absolute(b, ec).lexically_normal().wstring());
}

std::wstring regString(HKEY key, const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

// Brokers ship MT5 under their own names (HFM, IC Markets, ...), so every uninstall entry is checked for terminal64.exe.
void addRegistryInstallDirs(std::vector<fs::path>& dirs) {
    const struct { HKEY root; REGSAM view; } hives[] = {
        {HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY}, {HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY}, {HKEY_CURRENT_USER, 0}};
    for (const auto& h : hives) {
        HKEY uninstall;
        if (RegOpenKeyExW(h.root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | h.view, &uninstall) != ERROR_SUCCESS) continue;
        wchar_t name[256];
        for (DWORD i = 0;; ++i) {
            DWORD n = static_cast<DWORD>(std::size(name));
            if (RegEnumKeyExW(uninstall, i, name, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            HKEY app;
            if (RegOpenKeyExW(uninstall, name, 0, KEY_QUERY_VALUE | h.view, &app) != ERROR_SUCCESS) continue;
            std::wstring location = regString(app, L"InstallLocation");
            std::wstring icon = regString(app, L"DisplayIcon");  // "C:\...\terminal64.exe,0" when InstallLocation is empty
            RegCloseKey(app);
            for (std::wstring* s : {&location, &icon}) s->erase(std::remove(s->begin(), s->end(), L'"'), s->end());
            if (const auto comma = icon.find(L','); comma != std::wstring::npos) icon.resize(comma);
            if (!location.empty()) dirs.emplace_back(trim(location));
            if (lower(fs::path(icon).filename().wstring()) == L"terminal64.exe") dirs.push_back(fs::path(trim(icon)).parent_path());
        }
        RegCloseKey(uninstall);
    }
}

// Installs without an uninstall entry (copied folders, portable setups) one level under Program Files.
void addProgramFilesInstallDirs(std::vector<fs::path>& dirs) {
    for (const KNOWNFOLDERID& id : {FOLDERID_ProgramFiles, FOLDERID_ProgramFilesX86, FOLDERID_UserProgramFiles}) {
        PWSTR root = nullptr;
        if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &root))) continue;
        const fs::path r(root);
        CoTaskMemFree(root);
        std::error_code ec;
        for (auto& d : fs::directory_iterator(r, ec))
            if (d.is_directory(ec) && fs::exists(d.path() / L"terminal64.exe", ec)) dirs.push_back(d.path());
    }
}

fs::path dataRoot() {
    fs::path root;
    PWSTR roaming = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) root = fs::path(roaming) / L"MetaQuotes" / L"Terminal";
    CoTaskMemFree(roaming);
    return root;
}

// Broker builds keep MetaQuotes' version info ("MetaTrader 5 Client Terminal"); name gets the first name found.
bool isMt5Program(const fs::path& exe, std::wstring& name) {
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
    if (size == 0) return false;
    std::vector<char> info(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, info.data())) return false;
    struct Translation { WORD lang, codePage; }* tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&tr), &len) || len < sizeof(Translation)) return false;
    for (const wchar_t* field : {L"ProductName", L"InternalName", L"FileDescription"}) {
        wchar_t query[96];
        swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s", tr->lang, tr->codePage, field);
        wchar_t* value = nullptr;
        UINT n = 0;
        if (!VerQueryValueW(info.data(), query, reinterpret_cast<void**>(&value), &n) || n == 0) continue;
        if (name.empty()) name = value;
        if (lower(value).find(L"metatrader 5") != std::wstring::npos) return true;
    }
    return false;
}

bool canWrite(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path probe = dir / L".pipkite-write-test";
    const bool ok = platform::writeFileAtomic(probe.wstring(), "ok");
    fs::remove(probe, ec);
    return ok;
}

void start(const fs::path& exe, std::wstring cmd) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi))
        throw std::runtime_error("Could not start MetaTrader (error " + std::to_string(GetLastError()) + ").");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

}  // namespace

fs::path exeForDataPath(const fs::path& dataPath) {
    if (fs::exists(dataPath / L"terminal64.exe")) return dataPath / L"terminal64.exe";  // portable install
    const std::wstring origin = trim(readText(dataPath / L"origin.txt"));
    if (origin.empty()) return {};
    return fs::path(origin) / L"terminal64.exe";
}

bool isRunning(const fs::path& exePath) {
    if (exePath.empty()) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"terminal64.exe") != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD n = static_cast<DWORD>(std::size(path));
        if (QueryFullProcessImageNameW(h, 0, path, &n)) found = samePath(fs::path(std::wstring(path, n)), exePath);
        CloseHandle(h);
    }
    CloseHandle(snap);
    return found;
}

bool activeProfileHasEa(const fs::path& dataPath) {
    std::wstring profile = iniValue(readText(dataPath / L"config\\common.ini"), L"ProfileLast");
    if (profile.empty()) profile = L"Default";
    std::error_code ec;
    for (auto& f : fs::directory_iterator(dataPath / L"MQL5\\Profiles\\Charts" / profile, ec)) {
        if (f.path().extension() != L".chr") continue;
        if (readText(f.path()).find(L"PipkiteEA") != std::wstring::npos) return true;
    }
    return false;
}

json detectTerminals(const fs::path& dataRoot, const std::vector<fs::path>& installDirs) {
    json arr = json::array();
    std::vector<fs::path> listed;  // terminal64.exe paths already in arr
    auto add = [&](const fs::path& dataPath, const fs::path& exe, bool portable) {
        const bool hasData = !dataPath.empty();
        arr.push_back({{"dataPath", platform::narrow(dataPath.wstring())},
                       {"origin", exe.empty() ? "" : platform::narrow(exe.parent_path().wstring())},
                       {"exePath", platform::narrow(exe.wstring())},
                       {"exeFound", !exe.empty() && fs::exists(exe)},
                       {"portable", portable},
                       {"needsFirstRun", !hasData},
                       {"running", isRunning(exe)},
                       {"hasEA", hasData && fs::exists(dataPath / L"MQL5\\Experts\\Pipkite\\PipkiteEA.ex5")},
                       {"hasDll", hasData && fs::exists(dataPath / L"MQL5\\Libraries\\MT5BridgeHost.dll")}});
        if (!exe.empty()) listed.push_back(exe);
    };
    std::error_code ec;
    for (auto& d : fs::directory_iterator(dataRoot, ec)) {
        if (!d.is_directory() || !fs::exists(d.path() / L"MQL5")) continue;
        add(d.path(), exeForDataPath(d.path()), false);
    }
    // Installs not covered by a data folder: portable ones keep MQL5 beside terminal64.exe, the rest were never opened.
    for (const fs::path& dir : installDirs) {
        const fs::path exe = dir / L"terminal64.exe";
        if (!fs::exists(exe, ec) || std::any_of(listed.begin(), listed.end(), [&](const fs::path& p) { return samePath(p, exe); })) continue;
        const bool portable = fs::exists(dir / L"MQL5", ec);
        add(portable ? dir : fs::path(), exe, portable);
    }
    return arr;
}

json detectTerminals(const std::vector<fs::path>& extraInstallDirs) {
    std::vector<fs::path> installDirs = extraInstallDirs;
    addRegistryInstallDirs(installDirs);
    addProgramFilesInstallDirs(installDirs);
    return detectTerminals(dataRoot(), installDirs);
}

json checkFolder(const fs::path& folder, const fs::path& terminalsRoot) {
    json checks = json::array();
    bool ok = true;
    auto check = [&](const char* name, bool passed, const std::string& detail) {
        checks.push_back({{"name", name}, {"ok", passed}, {"detail", detail}});
        ok = ok && passed;
    };
    fs::path exe = folder / L"terminal64.exe";
    std::error_code ec;
    if (!fs::exists(exe, ec)) {
        const fs::path viaOrigin = exeForDataPath(folder);  // the data folder was chosen instead of the install folder
        if (!viaOrigin.empty() && fs::exists(viaOrigin, ec)) exe = viaOrigin;
    }
    if (!fs::exists(exe, ec)) {
        check("MetaTrader 5 program", false,
              fs::exists(folder / L"terminal.exe", ec) ? "This folder has MetaTrader 4 (terminal.exe). Pipkite works with MetaTrader 5 only."
                                                       : "No terminal64.exe in this folder. Choose the folder MetaTrader 5 is installed in, for example "
                                                         "C:\\Program Files\\<your broker> MetaTrader 5.");
        return {{"ok", false}, {"terminal", nullptr}, {"checks", checks}};
    }
    check("MetaTrader 5 program", true, platform::narrow(exe.wstring()));
    std::wstring name;
    const bool mt5 = isMt5Program(exe, name);
    check("Is MetaTrader 5", mt5,
          mt5 ? platform::narrow(name)
              : name.empty() ? "terminal64.exe has no MetaTrader version information."
                             : "terminal64.exe reports \"" + platform::narrow(name) + "\", not MetaTrader 5.");

    json terminal = nullptr;
    for (const json& t : detectTerminals(terminalsRoot, {exe.parent_path()}))
        if (samePath(platform::widen(t["exePath"].get<std::string>()), exe)) terminal = t;
    if (terminal.is_null() || terminal["needsFirstRun"].get<bool>()) {
        check("Data folder", true, "Not created yet: MetaTrader creates it on its first start, which Start MetaTrader does for you.");
    } else {
        const std::string dataPath = terminal["dataPath"];
        check("Data folder", true, dataPath);
        const bool writable = canWrite(fs::path(platform::widen(dataPath)) / L"MQL5\\Libraries");
        check("Pipkite can install its EA and bridge", writable,
              writable ? "The MQL5 folder is writable."
                       : "Windows refuses writes to " + dataPath + "\\MQL5, so the EA and bridge can't be installed and MetaTrader can't reach Pipkite. "
                         "Use a MetaTrader folder your Windows account can write to (a portable MetaTrader inside Program Files is not).");
    }
    return {{"ok", ok}, {"terminal", terminal}, {"checks", checks}};
}

json checkFolder(const fs::path& folder) { return checkFolder(folder, dataRoot()); }

json installEa(const fs::path& installDir, const fs::path& dataPath) {
    if (!fs::exists(dataPath / L"MQL5")) throw std::runtime_error("Not an MT5 data folder (MQL5 subfolder missing)");
    std::error_code ec;
    fs::create_directories(dataPath / L"MQL5\\Experts\\Pipkite", ec);
    fs::create_directories(dataPath / L"MQL5\\Libraries", ec);
    const fs::path src = installDir / L"mql5";
    if (!fs::exists(src / L"PipkiteEA.ex5")) throw std::runtime_error("The compiled EA (mql5\\PipkiteEA.ex5) is missing from the Pipkite folder. Reinstall Pipkite.");
    json copied = json::array();
    for (const wchar_t* f : {L"PipkiteEA.ex5", L"PipkiteEA.mq5"}) {
        if (!fs::exists(src / f)) continue;
        fs::copy_file(src / f, dataPath / L"MQL5\\Experts\\Pipkite" / f, fs::copy_options::overwrite_existing, ec);
        if (ec) throw std::runtime_error("Could not copy " + platform::narrow(f) + ": " + ec.message());
        copied.push_back(platform::narrow(f));
    }
    const fs::path dllDest = dataPath / L"MQL5\\Libraries\\MT5BridgeHost.dll";
    fs::copy_file(installDir / L"MT5BridgeHost.dll", dllDest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        // A running EA keeps the stable host DLL loaded; an identical copy is fine, anything else needs a restart.
        if (!fs::exists(dllDest) || fs::file_size(dllDest, ec) != fs::file_size(installDir / L"MT5BridgeHost.dll", ec))
            throw std::runtime_error("Could not update MT5BridgeHost.dll while MetaTrader is using it. Close MetaTrader and try again.");
    } else {
        copied.push_back("MT5BridgeHost.dll");
    }
    return {{"copied", copied}};
}

json launch(const LaunchRequest& r) {
    if (r.dataPath.empty() && !r.exePath.empty()) {
        // A never-opened install has no data folder yet: MetaTrader creates it on its first start.
        if (!fs::exists(r.exePath)) throw std::runtime_error("terminal64.exe was not found. Reinstall MetaTrader 5 or choose another terminal.");
        if (!isRunning(r.exePath)) start(r.exePath, L"\"" + r.exePath.wstring() + L"\"");
        return {{"state", "firstRun"},
                {"attached", false},
                {"exePath", platform::narrow(r.exePath.wstring())},
                {"message", "MetaTrader is opening for the first time to create its data folder. Log in to your account, close MetaTrader, "
                            "then click Detect and Start again."}};
    }
    if (r.dataPath.empty() || !fs::exists(r.dataPath / L"MQL5")) throw std::runtime_error("Choose a MetaTrader 5 terminal first.");
    const fs::path exe = r.exePath.empty() ? exeForDataPath(r.dataPath) : r.exePath;
    if (exe.empty() || !fs::exists(exe)) throw std::runtime_error("terminal64.exe was not found for this data folder. Reinstall MetaTrader 5 or choose another terminal.");
    if (isRunning(exe))
        return {{"state", "running"},
                {"attached", false},
                {"exePath", platform::narrow(exe.wstring())},
                {"message", "MetaTrader is already open. If the Pipkite EA is not on a chart yet, close MetaTrader and click Start again."}};

    json installed = installEa(r.installDir, r.dataPath);
    const bool attach = !activeProfileHasEa(r.dataPath);
    std::wstring ini = L"[Experts]\r\nAllowLiveTrading=1\r\nAllowDllImport=1\r\nEnabled=1\r\nAccount=0\r\nProfile=0\r\n";
    if (attach) {
        std::wstring symbol = platform::widen(r.chartSymbol.empty() ? std::string("EURUSD") : r.chartSymbol);
        symbol.erase(std::remove_if(symbol.begin(), symbol.end(), [](wchar_t c) { return c == L'\r' || c == L'\n' || c == L'='; }), symbol.end());
        ini += L"[StartUp]\r\nExpert=Pipkite\\PipkiteEA\r\nSymbol=" + symbol + L"\r\nPeriod=H1\r\n";
    }
    std::error_code ec;
    fs::create_directories(r.appDataDir / L"mt5", ec);
    const fs::path iniPath = r.appDataDir / L"mt5" / L"startup.ini";
    if (!writeUtf16(iniPath, ini)) throw std::runtime_error("Could not write the MetaTrader startup configuration.");

    const bool portable = samePath(exe.parent_path(), r.dataPath);
    start(exe, L"\"" + exe.wstring() + L"\" /config:\"" + iniPath.wstring() + L"\"" + (portable ? L" /portable" : L""));
    return {{"state", "started"},
            {"attached", attach},
            {"exePath", platform::narrow(exe.wstring())},
            {"copied", installed["copied"]},
            {"message", attach ? "MetaTrader is starting with the Pipkite EA on a " + r.chartSymbol + " chart. Approve the pairing request when it appears."
                               : "MetaTrader is starting; the Pipkite EA is already on a chart in its profile."}};
}

}  // namespace sr::mt5
