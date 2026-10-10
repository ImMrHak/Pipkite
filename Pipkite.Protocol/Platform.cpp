#include "Platform.h"

#include <windows.h>
#include <sddl.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace sr::platform {

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(std::wstring_view w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring currentUserSid() {
    static const std::wstring cached = [] {
        HANDLE token = nullptr;
        std::wstring result = L"unknown";
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return result;
        DWORD len = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &len);
        std::vector<BYTE> buf(len);
        if (GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
            LPWSTR str = nullptr;
            if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &str)) {
                result = str;
                LocalFree(str);
            }
        }
        CloseHandle(token);
        return result;
    }();
    return cached;
}

std::wstring dataDir() {
    wchar_t env[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"PIPKITE_DATA_DIR", env, MAX_PATH);
    std::filesystem::path dir;
    if (n > 0 && n < MAX_PATH) {
        dir = env;
    } else {
        PWSTR local = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) {
            dir = std::filesystem::path(local) / L"Pipkite";
            CoTaskMemFree(local);
        } else {
            dir = std::filesystem::temp_directory_path() / L"Pipkite";
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.wstring();
}

std::wstring ensureSubdir(const std::wstring& name) {
    std::filesystem::path p = std::filesystem::path(dataDir()) / name;
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p.wstring();
}

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    std::filesystem::path p(std::wstring(buf, n));
    return p.parent_path().wstring();
}

std::optional<std::string> readFileUtf8(const std::wstring& path, std::size_t maxBytes) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(f.tellg());
    if (size > maxBytes) return std::nullopt;
    f.seekg(0);
    std::string s(size, '\0');
    f.read(s.data(), static_cast<std::streamsize>(size));
    return s;
}

bool writeFileAtomic(const std::wstring& path, std::string_view data) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

std::optional<crypto::Bytes> loadSecret(const std::wstring& name) {
    const std::wstring path = ensureSubdir(L"secrets") + L"\\" + name + L".bin";
    auto blob = readFileUtf8(path, 4096);
    if (!blob) return std::nullopt;
    return crypto::dpapiUnprotect(crypto::Bytes(blob->begin(), blob->end()));
}

std::optional<crypto::Bytes> loadOrCreateSecret(const std::wstring& name) {
    if (auto s = loadSecret(name)) return s;
    auto secret = crypto::randomBytes(32);
    auto prot = crypto::dpapiProtect(secret, L"Pipkite " + name);
    if (!prot) return std::nullopt;
    const std::wstring path = ensureSubdir(L"secrets") + L"\\" + name + L".bin";
    if (!writeFileAtomic(path, std::string_view(reinterpret_cast<const char*>(prot->data()), prot->size()))) return std::nullopt;
    return secret;
}

bool deleteSecret(const std::wstring& name) {
    const std::wstring path = ensureSubdir(L"secrets") + L"\\" + name + L".bin";
    return DeleteFileW(path.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
}

std::wstring instanceSuffix() {
    wchar_t buf[64];
    DWORD n = GetEnvironmentVariableW(L"PIPKITE_INSTANCE", buf, 64);
    if (n == 0 || n >= 64) return {};
    return L"-" + std::wstring(buf, n);
}

std::wstring pipeName(const std::wstring& suffix) { return L"\\\\.\\pipe\\Pipkite-" + currentUserSid() + instanceSuffix() + L"-" + suffix; }

std::string machineName() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &n)) return "unknown";
    return narrow(std::wstring_view(buf, n));
}

}  // namespace sr::platform
