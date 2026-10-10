#pragma once
// Minimal WinHTTP client with a hard deadline. Supports http(s):// and file: (local manifests/tests).

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sr::engine {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string error;
    double elapsedMs = 0;
};

// Optional extra request headers (e.g. the license key) as name/value pairs.
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

HttpResponse httpRequest(const std::string& method, const std::string& url, const std::string& body, const std::string& contentType, unsigned timeoutMs,
                         std::size_t maxBytes = 8 * 1024 * 1024, const HttpHeaders& extraHeaders = {});

}  // namespace sr::engine
