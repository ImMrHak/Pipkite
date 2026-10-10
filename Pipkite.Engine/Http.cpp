#include "Http.h"

#include <windows.h>
#include <winhttp.h>

#include <chrono>

#include "Pipkite.Protocol/Platform.h"

#pragma comment(lib, "winhttp.lib")

namespace sr::engine {

namespace {

struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

HttpResponse httpRequest(const std::string& method, const std::string& url, const std::string& body, const std::string& contentType, unsigned timeoutMs, std::size_t maxBytes,
                         const HttpHeaders& extraHeaders) {
    HttpResponse r;
    const auto t0 = std::chrono::steady_clock::now();
    auto done = [&]() -> HttpResponse {
        r.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return r;
    };
    if (url.rfind("file:", 0) == 0) {
        std::string path = url.substr(5);
        while (!path.empty() && path[0] == '/') path.erase(0, 1);
        auto text = platform::readFileUtf8(platform::widen(path), maxBytes);
        if (!text) {
            r.error = "file not found";
            return done();
        }
        r.status = 200;
        r.body = std::move(*text);
        return done();
    }
    std::wstring wurl = platform::widen(url);
    URL_COMPONENTS uc{sizeof uc};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        r.error = "invalid URL";
        return done();
    }
    Handle session(WinHttpOpen(L"Pipkite/1.0.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.h) {
        r.error = "WinHttpOpen failed";
        return done();
    }
    const int t = static_cast<int>(timeoutMs);
    WinHttpSetTimeouts(session.h, t, t, t, t);
    Handle connect(WinHttpConnect(session.h, host, uc.nPort, 0));
    if (!connect.h) {
        r.error = "connect failed";
        return done();
    }
    const DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    Handle req(WinHttpOpenRequest(connect.h, platform::widen(method).c_str(), path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!req.h) {
        r.error = "request failed";
        return done();
    }
    std::wstring headers;
    if (!contentType.empty()) headers = L"Content-Type: " + platform::widen(contentType) + L"\r\n";
    for (auto& [name, value] : extraHeaders)
        if (!name.empty() && !value.empty()) headers += platform::widen(name) + L": " + platform::widen(value) + L"\r\n";
    BOOL ok = WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : static_cast<DWORD>(-1L),
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0);
    if (!ok || !WinHttpReceiveResponse(req.h, nullptr)) {
        const DWORD e = GetLastError();
        r.error = e == ERROR_WINHTTP_TIMEOUT ? "timeout" : "network error " + std::to_string(e);
        return done();
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
        if (r.body.size() + avail > maxBytes) {
            r.error = "response too large";
            r.body.clear();
            return done();
        }
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req.h, chunk.data(), avail, &read)) break;
        r.body.append(chunk.data(), read);
    }
    return done();
}

}  // namespace sr::engine
