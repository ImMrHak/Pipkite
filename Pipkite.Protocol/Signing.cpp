#include "Signing.h"

#include <filesystem>

#include "Platform.h"

namespace sr::signing {

std::string canonical(const nlohmann::json& manifest) {
    nlohmann::json copy = manifest;
    copy.erase("signature");
    return copy.dump();  // nlohmann::json objects are key-sorted -> deterministic
}

bool sign(nlohmann::json& manifest, const crypto::Bytes& privateBlob) {
    auto sig = crypto::ecdsaSign(privateBlob, canonical(manifest));
    if (!sig) return false;
    manifest["signature"] = crypto::toBase64(*sig);
    return true;
}

std::string verify(const nlohmann::json& manifest, const std::vector<TrustedKey>& keys) {
    if (!manifest.contains("signature") || !manifest["signature"].is_string()) return {};
    auto sig = crypto::fromBase64(manifest["signature"].get<std::string>());
    if (!sig || sig->empty()) return {};
    const std::string body = canonical(manifest);
    for (auto& k : keys)
        if (crypto::ecdsaVerify(k.publicBlob, body, *sig)) return k.name;
    return {};
}

std::vector<TrustedKey> loadTrustedKeys(const std::wstring& dir) {
    std::vector<TrustedKey> out;
    std::error_code ec;
    for (auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != L".pub") continue;
        auto text = platform::readFileUtf8(entry.path().wstring(), 4096);
        if (!text) continue;
        std::string t = *text;
        while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
        auto blob = crypto::fromBase64(t);
        if (blob && !blob->empty()) out.push_back({platform::narrow(entry.path().stem().wstring()), *blob});
    }
    return out;
}

}  // namespace sr::signing
