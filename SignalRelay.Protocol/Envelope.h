#pragma once
// Message envelope shared by the engine, the bridge host and the UI.
// {v, type, id, accountKey, seq, deadline, configVersion, payload}

#include <cstdint>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace sr::ipc {

inline constexpr int kWireVersion = 1;

namespace msg {
// Bridge <-> engine
inline constexpr const char* HELLO = "HELLO";
inline constexpr const char* CHALLENGE = "CHALLENGE";
inline constexpr const char* AUTH = "AUTH";
inline constexpr const char* WELCOME = "WELCOME";
inline constexpr const char* SNAPSHOT_REQUEST = "SNAPSHOT_REQUEST";
inline constexpr const char* ACCOUNT_SNAPSHOT = "ACCOUNT_SNAPSHOT";
inline constexpr const char* SYMBOL_SNAPSHOT = "SYMBOL_SNAPSHOT";
inline constexpr const char* QUOTE = "QUOTE";
inline constexpr const char* EXECUTE = "EXECUTE";
inline constexpr const char* COMMAND_STATUS = "COMMAND_STATUS";
inline constexpr const char* TRADE_EVENT = "TRADE_EVENT";
inline constexpr const char* HEARTBEAT = "HEARTBEAT";
inline constexpr const char* PAUSE = "PAUSE";
inline constexpr const char* ERROR_ = "ERROR";
inline constexpr const char* MANAGE = "MANAGE";  // cancel copier pending / close copier positions (explicit user action)
inline constexpr const char* RECONCILE_REQUEST = "RECONCILE_REQUEST";
inline constexpr const char* RECONCILE_REPORT = "RECONCILE_REPORT";
// UI <-> engine
inline constexpr const char* REQUEST = "REQUEST";
inline constexpr const char* RESPONSE = "RESPONSE";
inline constexpr const char* EVENT = "EVENT";
}  // namespace msg

struct Envelope {
    int v = kWireVersion;
    std::string type;
    std::string id;
    std::string accountKey;
    std::int64_t seq = 0;
    std::int64_t deadline = 0;
    std::int64_t configVersion = 0;
    nlohmann::json payload = nlohmann::json::object();

    std::string serialize() const;
};

// Parses and validates schema: known version, required string fields, object payload.
std::optional<Envelope> parseEnvelope(const std::string& text, std::string* error = nullptr);

}  // namespace sr::ipc
