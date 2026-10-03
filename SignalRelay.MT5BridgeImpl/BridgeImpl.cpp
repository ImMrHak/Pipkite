// Reloadable MT5 bridge implementation (version 1.0.0).
// Translates engine JSON envelopes to the EA's simple line format and back, so the MQL5 side never
// needs a JSON parser. Stateless: queues, authentication and journals belong to the stable host.
//
// EA line format (UTF-8): one "key=value" per line; first line is "type=<TYPE>".
// Nested JSON objects flatten to dotted keys (payload.symbol=XAUUSD); arrays use indices (payload.tp.0=...).
// Values escape backslash, newline and carriage return as \\ \n \r.

#include <windows.h>

#include <cstring>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "SignalRelayPluginAbi.h"

using nlohmann::json;

namespace {

const char* kVersion = "1.0.0";

std::string escape(const std::string& v) {
    std::string o;
    for (char c : v) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

std::string unescape(const std::string& v) {
    std::string o;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char n = v[++i];
            o += n == 'n' ? '\n' : n == 'r' ? '\r' : n;
        } else {
            o += v[i];
        }
    }
    return o;
}

void flatten(const json& j, const std::string& prefix, std::string& out, int depth) {
    if (depth > 6) return;
    if (j.is_object()) {
        for (auto& [k, v] : j.items()) flatten(v, prefix.empty() ? k : prefix + "." + k, out, depth + 1);
    } else if (j.is_array()) {
        out += prefix + ".count=" + std::to_string(j.size()) + "\n";
        for (std::size_t i = 0; i < j.size(); ++i) flatten(j[i], prefix + "." + std::to_string(i), out, depth + 1);
    } else if (j.is_string()) {
        out += prefix + "=" + escape(j.get<std::string>()) + "\n";
    } else if (j.is_boolean()) {
        out += prefix + "=" + (j.get<bool>() ? "true" : "false") + "\n";
    } else if (j.is_number_integer() || j.is_number_unsigned()) {
        out += prefix + "=" + j.dump() + "\n";
    } else if (j.is_number_float()) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.10g", j.get<double>());
        out += prefix + "=" + buf + "\n";
    } else if (j.is_null()) {
        out += prefix + "=\n";
    }
}

json inferValue(const std::string& v) {
    if (v == "true") return true;
    if (v == "false") return false;
    if (v.empty()) return nullptr;
    // Numbers only when the whole token is numeric and short enough to be exact.
    bool numeric = v.size() <= 17;
    bool dot = false;
    for (std::size_t i = 0; i < v.size() && numeric; ++i) {
        char c = v[i];
        if (c == '-' && i == 0 && v.size() > 1) continue;
        if (c == '.' && !dot) { dot = true; continue; }
        if (c < '0' || c > '9') numeric = false;
    }
    if (numeric) {
        try {
            if (dot) return std::stod(v);
            return static_cast<std::int64_t>(std::stoll(v));
        } catch (...) {
        }
    }
    return v;
}

int32_t emit(const std::string& s, char* out, int32_t cap) {
    if (s.size() > static_cast<std::size_t>(INT32_MAX)) return 0;
    if (static_cast<int32_t>(s.size()) > cap) return -static_cast<int32_t>(s.size());
    std::memcpy(out, s.data(), s.size());
    return static_cast<int32_t>(s.size());
}

int32_t toEa(const char* js, int32_t len, char* out, int32_t cap) {
    if (!js || len <= 0 || len > 64 * 1024) return 0;
    json j = json::parse(std::string(js, static_cast<std::size_t>(len)), nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("type")) return 0;
    std::string s = "type=" + escape(j.value("type", "")) + "\n";
    for (auto& [k, v] : j.items()) {
        if (k == "type") continue;
        flatten(v, k, s, 0);
    }
    return emit(s, out, cap);
}

// Identifier-like fields always stay strings (tickets and IDs must never lose precision or type).
const std::set<std::string> kStringKeys{"id", "accountKey", "commandId", "planId", "comment", "symbol", "name", "server", "terminalId", "reason", "message",
                                        "orderTicket", "requestId", "positionId", "state", "kind", "action", "side", "currency", "marginMode", "version", "error",
                                        "previous", "path", "sha256", "retcodeText"};

// Objects whose keys are exactly 0..n-1 become arrays ("quotes.0.symbol" -> quotes: [ {symbol} ]).
void arrayify(json& j, int depth) {
    if (depth > 8) return;
    if (j.is_object()) {
        for (auto& [k, v] : j.items()) arrayify(v, depth + 1);
        if (j.empty()) return;
        std::size_t n = j.size();
        for (std::size_t i = 0; i < n; ++i)
            if (!j.contains(std::to_string(i))) return;
        json arr = json::array();
        for (std::size_t i = 0; i < n; ++i) arr.push_back(j[std::to_string(i)]);
        j = std::move(arr);
    } else if (j.is_array()) {
        for (auto& v : j) arrayify(v, depth + 1);
    }
}

int32_t fromEa(const char* kv, int32_t len, char* out, int32_t cap) {
    if (!kv || len <= 0 || len > 64 * 1024) return 0;
    std::string text(kv, static_cast<std::size_t>(len));
    json root = json::object();
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq > 128) return 0;
        std::string key = line.substr(0, eq);
        std::string value = unescape(line.substr(eq + 1));
        // Walk dotted key into nested objects (arrays are sent by the EA as indexed objects).
        json* node = &root;
        std::size_t start = 0;
        int depth = 0;
        for (;;) {
            std::size_t dot = key.find('.', start);
            std::string part = key.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
            if (part.empty() || ++depth > 6) return 0;
            if (dot == std::string::npos) {
                (*node)[part] = (key == "type" || kStringKeys.count(part)) ? json(value) : inferValue(value);  // only the envelope type is forced; deal.type stays numeric
                break;
            }
            if (!(*node)[part].is_object()) (*node)[part] = json::object();
            node = &(*node)[part];
            start = dot + 1;
        }
    }
    if (!root.contains("type") || !root["type"].is_string()) return 0;
    if (root.contains("payload")) arrayify(root["payload"], 0);
    if (!root.contains("v")) root["v"] = 1;
    if (!root.contains("payload")) root["payload"] = json::object();
    return emit(root.dump(), out, cap);
}

const char* version() { return kVersion; }

int32_t healthcheck() {
    // Round-trip a representative message through both directions.
    const char* sample = R"({"v":1,"type":"EXECUTE","id":"x","payload":{"symbol":"XAUUSD","volume":0.01,"tp":[1,2]}})";
    char a[1024], b[2048];
    int32_t n = toEa(sample, static_cast<int32_t>(std::strlen(sample)), a, sizeof a);
    if (n <= 0) return 0;
    int32_t m = fromEa(a, n, b, sizeof b);
    return m > 0 ? 1 : 0;
}

const SrBridgeImplApi kApi{SR_BRIDGE_IMPL_ABI_VERSION, &version, &healthcheck, &toEa, &fromEa};

}  // namespace

extern "C" __declspec(dllexport) const SrBridgeImplApi* SrBridgeImplGetApi() { return &kApi; }

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) { return TRUE; }
