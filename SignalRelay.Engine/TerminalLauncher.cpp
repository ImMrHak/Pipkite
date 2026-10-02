#include "TerminalLauncher.h"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <stdexcept>

#include "SignalRelay.Protocol/Platform.h"

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
        if (readText(f.path()).find(L"SignalRelayEA") != std::wstring::npos) return true;
    }
    return false;
}

json detectTerminals() {
    json arr = json::array();
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return arr;
    fs::path root = fs::path(roaming) / L"MetaQuotes" / L"Terminal";
    CoTaskMemFree(roaming);
    std::error_code ec;
    for (auto& d : fs::directory_iterator(root, ec)) {
        if (!d.is_directory() || !fs::exists(d.path() / L"MQL5")) continue;
        const fs::path exe = exeForDataPath(d.path());
        arr.push_back({{"dataPath", platform::narrow(d.path().wstring())},
                       {"origin", exe.empty() ? "" : platform::narrow(exe.parent_path().wstring())},
                       {"exePath", platform::narrow(exe.wstring())},
                       {"exeFound", !exe.empty() && fs::exists(exe)},
                       {"portable", false},
                       {"running", isRunning(exe)},
                       {"hasEA", fs::exists(d.path() / L"MQL5\\Experts\\SignalRelay\\SignalRelayEA.ex5")},
                       {"hasDll", fs::exists(d.path() / L"MQL5\\Libraries\\MT5BridgeHost.dll")}});
    }
    return arr;
}

json installEa(const fs::path& installDir, const fs::path& dataPath) {
    if (!fs::exists(dataPath / L"MQL5")) throw std::runtime_error("Not an MT5 data folder (MQL5 subfolder missing)");
    std::error_code ec;
    fs::create_directories(dataPath / L"MQL5\\Experts\\SignalRelay", ec);
    fs::create_directories(dataPath / L"MQL5\\Libraries", ec);
    const fs::path src = installDir / L"mql5";
    if (!fs::exists(src / L"SignalRelayEA.ex5")) throw std::runtime_error("The compiled EA (mql5\\SignalRelayEA.ex5) is missing from the SignalRelay folder. Reinstall SignalRelay.");
    json copied = json::array();
    for (const wchar_t* f : {L"SignalRelayEA.ex5", L"SignalRelayEA.mq5"}) {
        if (!fs::exists(src / f)) continue;
        fs::copy_file(src / f, dataPath / L"MQL5\\Experts\\SignalRelay" / f, fs::copy_options::overwrite_existing, ec);
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
    if (r.dataPath.empty() || !fs::exists(r.dataPath / L"MQL5")) throw std::runtime_error("Choose a MetaTrader 5 terminal first.");
    const fs::path exe = r.exePath.empty() ? exeForDataPath(r.dataPath) : r.exePath;
    if (exe.empty() || !fs::exists(exe)) throw std::runtime_error("terminal64.exe was not found for this data folder. Reinstall MetaTrader 5 or choose another terminal.");
    if (isRunning(exe))
        return {{"state", "running"},
                {"attached", false},
                {"exePath", platform::narrow(exe.wstring())},
                {"message", "MetaTrader is already open. If the SignalRelay EA is not on a chart yet, close MetaTrader and click Start again."}};

    json installed = installEa(r.installDir, r.dataPath);
    const bool attach = !activeProfileHasEa(r.dataPath);
    std::wstring ini = L"[Experts]\r\nAllowLiveTrading=1\r\nAllowDllImport=1\r\nEnabled=1\r\nAccount=0\r\nProfile=0\r\n";
    if (attach) {
        std::wstring symbol = platform::widen(r.chartSymbol.empty() ? std::string("EURUSD") : r.chartSymbol);
        symbol.erase(std::remove_if(symbol.begin(), symbol.end(), [](wchar_t c) { return c == L'\r' || c == L'\n' || c == L'='; }), symbol.end());
        ini += L"[StartUp]\r\nExpert=SignalRelay\\SignalRelayEA\r\nSymbol=" + symbol + L"\r\nPeriod=H1\r\n";
    }
    std::error_code ec;
    fs::create_directories(r.appDataDir / L"mt5", ec);
    const fs::path iniPath = r.appDataDir / L"mt5" / L"startup.ini";
    if (!writeUtf16(iniPath, ini)) throw std::runtime_error("Could not write the MetaTrader startup configuration.");

    const bool portable = samePath(exe.parent_path(), r.dataPath);
    std::wstring cmd = L"\"" + exe.wstring() + L"\" /config:\"" + iniPath.wstring() + L"\"" + (portable ? L" /portable" : L"");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi))
        throw std::runtime_error("Could not start MetaTrader (error " + std::to_string(GetLastError()) + ").");
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return {{"state", "started"},
            {"attached", attach},
            {"exePath", platform::narrow(exe.wstring())},
            {"copied", installed["copied"]},
            {"message", attach ? "MetaTrader is starting with the SignalRelay EA on a " + r.chartSymbol + " chart. Approve the pairing request when it appears."
                               : "MetaTrader is starting; the SignalRelay EA is already on a chart in its profile."}};
}

}  // namespace sr::mt5
