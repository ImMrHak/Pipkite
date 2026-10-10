#include "Envelope.h"

namespace sr::ipc {

std::string Envelope::serialize() const {
    nlohmann::json j = {{"v", v}, {"type", type}, {"id", id}, {"accountKey", accountKey}, {"seq", seq}, {"deadline", deadline}, {"configVersion", configVersion}, {"payload", payload}};
    return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::optional<Envelope> parseEnvelope(const std::string& text, std::string* error) {
    auto fail = [&](const char* why) -> std::optional<Envelope> {
        if (error) *error = why;
        return std::nullopt;
    };
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("frame is not a JSON object");
    if (!j.contains("v") || !j["v"].is_number_integer() || j["v"].get<int>() != kWireVersion) return fail("unsupported protocol version");
    if (!j.contains("type") || !j["type"].is_string() || j["type"].get<std::string>().empty() || j["type"].get<std::string>().size() > 32) return fail("missing type");
    if (j.contains("id") && !j["id"].is_string()) return fail("id must be a string");
    if (j.contains("accountKey") && !j["accountKey"].is_string()) return fail("accountKey must be a string");
    if (j.contains("payload") && !j["payload"].is_object()) return fail("payload must be an object");
    for (const char* k : {"seq", "deadline", "configVersion"})
        if (j.contains(k) && !j[k].is_number_integer()) return fail("numeric header field has the wrong type");
    Envelope e;
    e.v = j["v"].get<int>();
    e.type = j["type"].get<std::string>();
    e.id = j.value("id", "");
    e.accountKey = j.value("accountKey", "");
    e.seq = j.value("seq", static_cast<std::int64_t>(0));
    e.deadline = j.value("deadline", static_cast<std::int64_t>(0));
    e.configVersion = j.value("configVersion", static_cast<std::int64_t>(0));
    if (j.contains("payload")) e.payload = j["payload"];
    return e;
}

}  // namespace sr::ipc
