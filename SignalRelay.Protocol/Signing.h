#pragma once
// ECDSA P-256 signatures for module and release manifests.

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Crypto.h"

namespace sr::signing {

struct TrustedKey {
    std::string name;
    crypto::Bytes publicBlob;
};

// Canonical bytes: the manifest without "signature", keys sorted, compact.
std::string canonical(const nlohmann::json& manifest);
bool sign(nlohmann::json& manifest, const crypto::Bytes& privateBlob);
// Returns the name of the key that verified the manifest, or empty when none did.
std::string verify(const nlohmann::json& manifest, const std::vector<TrustedKey>& keys);
// Loads *.pub files (base64 CNG public blobs) from a directory.
std::vector<TrustedKey> loadTrustedKeys(const std::wstring& dir);

}  // namespace sr::signing
