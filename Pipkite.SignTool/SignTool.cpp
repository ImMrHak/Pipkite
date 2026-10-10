// Pipkite publisher signing tool.
//   keygen <keyDir>                         create publisher.pub + publisher.key (private key DPAPI-protected)
//   sign-module <manifest.json> <keyFile>   hash the manifest's entry file, then sign the manifest
//   sign-json <file.json> <keyFile>         sign an arbitrary manifest (e.g. update release manifest)
//   verify <manifest.json> <trustedKeyDir>  verify signature (and entry hash for modules)
// Private keys never ship with the application and must stay outside source control.

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Platform.h"
#include "Pipkite.Protocol/Signing.h"

using namespace sr;
namespace fs = std::filesystem;

namespace {

int usage() {
    std::puts("usage: Pipkite.SignTool keygen <dir> | sign-module <manifest> <key> | sign-json <file> <key> | verify <manifest> <trustedDir> | export-raw <key>");
    return 2;
}

std::optional<crypto::Bytes> loadPrivate(const std::wstring& path) {
    auto text = platform::readFileUtf8(path, 16 * 1024);
    if (!text) return std::nullopt;
    auto blob = crypto::fromBase64(*text);
    if (!blob) return std::nullopt;
    return crypto::dpapiUnprotect(*blob);
}

std::optional<nlohmann::json> loadJson(const std::wstring& path) {
    auto text = platform::readFileUtf8(path);
    if (!text) return std::nullopt;
    auto j = nlohmann::json::parse(*text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return usage();
    const std::wstring cmd = argv[1];
    if (cmd == L"keygen") {
        fs::create_directories(argv[2]);
        const std::wstring pub = std::wstring(argv[2]) + L"\\publisher.pub";
        const std::wstring key = std::wstring(argv[2]) + L"\\publisher.key";
        if (fs::exists(key)) {
            std::puts("key already exists; not overwritten");
            return 0;
        }
        auto kp = crypto::ecdsaGenerate();
        if (!kp) return std::puts("key generation failed"), 1;
        auto prot = crypto::dpapiProtect(kp->privateBlob, L"Pipkite publisher key");
        if (!prot) return std::puts("DPAPI protection failed"), 1;
        platform::writeFileAtomic(pub, crypto::toBase64(kp->publicBlob));
        platform::writeFileAtomic(key, crypto::toBase64(*prot));
        std::printf("created %ls\n", pub.c_str());
        return 0;
    }
    if (cmd == L"export-raw" && argc >= 3) {
        // Print the raw (un-DPAPI) CNG ECCPRIVATE blob as base64 so a non-Windows signer (e.g. the Java backend)
        // can sign release manifests with the SAME publisher key the app already trusts. Handle with care.
        auto priv = loadPrivate(argv[2]);
        if (!priv) return std::puts("cannot read private key"), 1;
        std::printf("%s\n", crypto::toBase64(*priv).c_str());
        return 0;
    }
    if ((cmd == L"sign-module" || cmd == L"sign-json") && argc >= 4) {
        auto manifest = loadJson(argv[2]);
        if (!manifest) return std::puts("cannot read manifest"), 1;
        auto priv = loadPrivate(argv[3]);
        if (!priv) return std::puts("cannot read private key"), 1;
        if (cmd == L"sign-module") {
            fs::path entry = fs::path(argv[2]).parent_path() / platform::widen(manifest->value("entry", ""));
            auto hash = crypto::sha256File(entry.wstring());
            if (!hash) return std::printf("entry file not found: %ls\n", entry.c_str()), 1;
            (*manifest)["sha256"] = crypto::toHex(*hash);
        }
        if (!signing::sign(*manifest, *priv)) return std::puts("signing failed"), 1;
        platform::writeFileAtomic(argv[2], manifest->dump(2));
        std::printf("signed %ls\n", argv[2]);
        return 0;
    }
    if (cmd == L"verify" && argc >= 4) {
        auto manifest = loadJson(argv[2]);
        if (!manifest) return std::puts("cannot read manifest"), 1;
        auto keys = signing::loadTrustedKeys(argv[3]);
        std::string who = signing::verify(*manifest, keys);
        if (who.empty()) return std::puts("INVALID signature"), 1;
        if (manifest->contains("entry")) {
            fs::path entry = fs::path(argv[2]).parent_path() / platform::widen(manifest->value("entry", ""));
            auto hash = crypto::sha256File(entry.wstring());
            if (!hash || crypto::toHex(*hash) != manifest->value("sha256", "")) return std::puts("INVALID entry hash"), 1;
        }
        std::printf("valid signature by '%s'\n", who.c_str());
        return 0;
    }
    return usage();
}
