#pragma once
// Reviewed platform cryptography only (Windows CNG + DPAPI). No home-made primitives.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sr::crypto {

using Bytes = std::vector<std::uint8_t>;

Bytes randomBytes(std::size_t n);
Bytes sha256(const void* data, std::size_t len);
inline Bytes sha256(std::string_view s) { return sha256(s.data(), s.size()); }
std::optional<Bytes> sha256File(const std::wstring& path);
Bytes hmacSha256(const Bytes& key, std::string_view data);
bool constantTimeEqual(const Bytes& a, const Bytes& b);

std::string toHex(const Bytes& b);
std::optional<Bytes> fromHex(std::string_view hex);
std::string toBase64(const Bytes& b);
std::optional<Bytes> fromBase64(std::string_view s);

// Current-user DPAPI.
std::optional<Bytes> dpapiProtect(const Bytes& plain, std::wstring_view description);
std::optional<Bytes> dpapiUnprotect(const Bytes& blob);

// ECDSA P-256 over SHA-256. Keys are exported as CNG ECCPUBLIC/ECCPRIVATE blobs (base64 in files).
struct KeyPair {
    Bytes publicBlob;
    Bytes privateBlob;
};
std::optional<KeyPair> ecdsaGenerate();
std::optional<Bytes> ecdsaSign(const Bytes& privateBlob, std::string_view message);
bool ecdsaVerify(const Bytes& publicBlob, std::string_view message, const Bytes& signature);

// Password-based authenticated encryption for portable backups:
// PBKDF2-HMAC-SHA256 (stored salt/iterations) -> AES-256-GCM (stored nonce, 16-byte tag).
struct SealedBox {
    Bytes salt, nonce, tag, ciphertext;
    std::uint32_t iterations = 0;
};
std::optional<SealedBox> sealWithPassword(const Bytes& plain, std::string_view password, std::uint32_t iterations = 310000);
std::optional<Bytes> openWithPassword(const SealedBox& box, std::string_view password);

}  // namespace sr::crypto
