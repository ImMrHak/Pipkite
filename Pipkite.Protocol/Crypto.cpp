#include "Crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <wincrypt.h>

#include <fstream>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace sr::crypto {

namespace {

struct AlgHandle {
    BCRYPT_ALG_HANDLE h = nullptr;
    AlgHandle(LPCWSTR alg, ULONG flags = 0) {
        if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&h, alg, nullptr, flags))) h = nullptr;
    }
    ~AlgHandle() {
        if (h) BCryptCloseAlgorithmProvider(h, 0);
    }
};

struct KeyHandle {
    BCRYPT_KEY_HANDLE h = nullptr;
    ~KeyHandle() {
        if (h) BCryptDestroyKey(h);
    }
};

}  // namespace

Bytes randomBytes(std::size_t n) {
    Bytes b(n);
    if (n) BCryptGenRandom(nullptr, b.data(), static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return b;
}

Bytes sha256(const void* data, std::size_t len) {
    Bytes out(32);
    BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, static_cast<PUCHAR>(const_cast<void*>(data)), static_cast<ULONG>(len), out.data(), 32);
    return out;
}

std::optional<Bytes> sha256File(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (!BCRYPT_SUCCESS(BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &h, nullptr, 0, nullptr, 0, 0))) return std::nullopt;
    std::vector<char> buf(1 << 16);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto got = f.gcount();
        if (got > 0) BCryptHashData(h, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(got), 0);
    }
    Bytes out(32);
    BCryptFinishHash(h, out.data(), 32, 0);
    BCryptDestroyHash(h);
    return out;
}

Bytes hmacSha256(const Bytes& key, std::string_view data) {
    Bytes out(32);
    BCryptHash(BCRYPT_HMAC_SHA256_ALG_HANDLE, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
               static_cast<ULONG>(data.size()), out.data(), 32);
    return out;
}

bool constantTimeEqual(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff |= static_cast<std::uint8_t>(a[i] ^ b[i]);
    return diff == 0;
}

std::string toHex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (auto c : b) {
        s.push_back(d[c >> 4]);
        s.push_back(d[c & 15]);
    }
    return s;
}

std::optional<Bytes> fromHex(std::string_view hex) {
    if (hex.size() % 2) return std::nullopt;
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Bytes out;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = val(hex[i]), lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<std::uint8_t>(hi * 16 + lo));
    }
    return out;
}

std::string toBase64(const Bytes& b) {
    if (b.empty()) return {};
    DWORD len = 0;
    CryptBinaryToStringA(b.data(), static_cast<DWORD>(b.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
    std::string s(len, '\0');
    CryptBinaryToStringA(b.data(), static_cast<DWORD>(b.size()), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, s.data(), &len);
    s.resize(len);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

std::optional<Bytes> fromBase64(std::string_view s) {
    if (s.empty()) return Bytes{};
    DWORD len = 0;
    if (!CryptStringToBinaryA(s.data(), static_cast<DWORD>(s.size()), CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr)) return std::nullopt;
    Bytes out(len);
    if (!CryptStringToBinaryA(s.data(), static_cast<DWORD>(s.size()), CRYPT_STRING_BASE64, out.data(), &len, nullptr, nullptr)) return std::nullopt;
    out.resize(len);
    return out;
}

std::optional<Bytes> dpapiProtect(const Bytes& plain, std::wstring_view description) {
    DATA_BLOB in{static_cast<DWORD>(plain.size()), const_cast<BYTE*>(plain.data())};
    DATA_BLOB out{};
    std::wstring desc(description);
    if (!CryptProtectData(&in, desc.c_str(), nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    Bytes b(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return b;
}

std::optional<Bytes> dpapiUnprotect(const Bytes& blob) {
    DATA_BLOB in{static_cast<DWORD>(blob.size()), const_cast<BYTE*>(blob.data())};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    Bytes b(out.pbData, out.pbData + out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b;
}

std::optional<KeyPair> ecdsaGenerate() {
    AlgHandle alg(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!alg.h) return std::nullopt;
    KeyHandle key;
    if (!BCRYPT_SUCCESS(BCryptGenerateKeyPair(alg.h, &key.h, 256, 0))) return std::nullopt;
    if (!BCRYPT_SUCCESS(BCryptFinalizeKeyPair(key.h, 0))) return std::nullopt;
    auto exportBlob = [&](LPCWSTR type) -> std::optional<Bytes> {
        ULONG len = 0;
        if (!BCRYPT_SUCCESS(BCryptExportKey(key.h, nullptr, type, nullptr, 0, &len, 0))) return std::nullopt;
        Bytes b(len);
        if (!BCRYPT_SUCCESS(BCryptExportKey(key.h, nullptr, type, b.data(), len, &len, 0))) return std::nullopt;
        b.resize(len);
        return b;
    };
    auto pub = exportBlob(BCRYPT_ECCPUBLIC_BLOB);
    auto priv = exportBlob(BCRYPT_ECCPRIVATE_BLOB);
    if (!pub || !priv) return std::nullopt;
    return KeyPair{*pub, *priv};
}

std::optional<Bytes> ecdsaSign(const Bytes& privateBlob, std::string_view message) {
    AlgHandle alg(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!alg.h) return std::nullopt;
    KeyHandle key;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPRIVATE_BLOB, &key.h, const_cast<PUCHAR>(privateBlob.data()), static_cast<ULONG>(privateBlob.size()), 0)))
        return std::nullopt;
    Bytes digest = sha256(message);
    ULONG len = 0;
    if (!BCRYPT_SUCCESS(BCryptSignHash(key.h, nullptr, digest.data(), 32, nullptr, 0, &len, 0))) return std::nullopt;
    Bytes sig(len);
    if (!BCRYPT_SUCCESS(BCryptSignHash(key.h, nullptr, digest.data(), 32, sig.data(), len, &len, 0))) return std::nullopt;
    sig.resize(len);
    return sig;
}

bool ecdsaVerify(const Bytes& publicBlob, std::string_view message, const Bytes& signature) {
    AlgHandle alg(BCRYPT_ECDSA_P256_ALGORITHM);
    if (!alg.h || publicBlob.empty() || signature.empty()) return false;
    KeyHandle key;
    if (!BCRYPT_SUCCESS(BCryptImportKeyPair(alg.h, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key.h, const_cast<PUCHAR>(publicBlob.data()), static_cast<ULONG>(publicBlob.size()), 0)))
        return false;
    Bytes digest = sha256(message);
    return BCRYPT_SUCCESS(BCryptVerifySignature(key.h, nullptr, digest.data(), 32, const_cast<PUCHAR>(signature.data()), static_cast<ULONG>(signature.size()), 0));
}

namespace {

std::optional<Bytes> pbkdf2(std::string_view password, const Bytes& salt, std::uint32_t iterations) {
    Bytes key(32);
    if (!BCRYPT_SUCCESS(BCryptDeriveKeyPBKDF2(BCRYPT_HMAC_SHA256_ALG_HANDLE, reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())), static_cast<ULONG>(password.size()),
                                              const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()), iterations, key.data(), 32, 0)))
        return std::nullopt;
    return key;
}

bool aesGcm(bool encrypt, const Bytes& key, const Bytes& nonce, const Bytes& in, Bytes& out, Bytes& tag) {
    AlgHandle alg(BCRYPT_AES_ALGORITHM);
    if (!alg.h) return false;
    if (!BCRYPT_SUCCESS(BCryptSetProperty(alg.h, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_GCM)),
                                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))
        return false;
    KeyHandle k;
    if (!BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(alg.h, &k.h, nullptr, 0, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0))) return false;
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<PUCHAR>(nonce.data());
    info.cbNonce = static_cast<ULONG>(nonce.size());
    tag.resize(16);
    info.pbTag = tag.data();
    info.cbTag = 16;
    out.resize(in.size());
    ULONG done = 0;
    NTSTATUS st = encrypt ? BCryptEncrypt(k.h, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()), &info, nullptr, 0, out.data(), static_cast<ULONG>(out.size()), &done, 0)
                          : BCryptDecrypt(k.h, const_cast<PUCHAR>(in.data()), static_cast<ULONG>(in.size()), &info, nullptr, 0, out.data(), static_cast<ULONG>(out.size()), &done, 0);
    return BCRYPT_SUCCESS(st);
}

}  // namespace

std::optional<SealedBox> sealWithPassword(const Bytes& plain, std::string_view password, std::uint32_t iterations) {
    if (password.size() < 8) return std::nullopt;
    SealedBox box;
    box.salt = randomBytes(16);
    box.nonce = randomBytes(12);
    box.iterations = iterations;
    auto key = pbkdf2(password, box.salt, iterations);
    if (!key) return std::nullopt;
    if (!aesGcm(true, *key, box.nonce, plain, box.ciphertext, box.tag)) return std::nullopt;
    SecureZeroMemory(key->data(), key->size());
    return box;
}

std::optional<Bytes> openWithPassword(const SealedBox& box, std::string_view password) {
    auto key = pbkdf2(password, box.salt, box.iterations);
    if (!key) return std::nullopt;
    Bytes out;
    Bytes tag = box.tag;
    const bool ok = aesGcm(false, *key, box.nonce, box.ciphertext, out, tag);
    SecureZeroMemory(key->data(), key->size());
    if (!ok) return std::nullopt;
    return out;
}

}  // namespace sr::crypto
