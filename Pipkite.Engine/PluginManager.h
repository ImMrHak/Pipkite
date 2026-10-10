#pragma once
// loads plugins/<id>/<version>/module.dll after checking: signature, hash, x64 PE, type, ABI,
// then fixtures in a helper process. modules are only freed once their generation lease is released.

#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/Result.h"
#include "Pipkite.Protocol/Signing.h"

namespace sr::engine {

struct ModuleManifest {
    nlohmann::json raw;
    std::wstring dir;
    std::string id, version, moduleType, entry, sha256, signer;
    std::vector<Fixture> fixtures;
};

struct VerifiedModule {
    ModuleManifest manifest;
    std::wstring entryPath;
};

class PluginManager {
public:
    PluginManager(std::wstring installDir, std::wstring dataDir);

    const std::vector<signing::TrustedKey>& trustedKeys() const { return keys_; }
    // Full static verification (no code loaded into this process).
    Result<VerifiedModule> verify(const std::wstring& manifestPath) const;
    // Runs "Pipkite.Engine.exe --validate-module <manifest>" and parses its JSON verdict.
    Result<nlohmann::json> validateInHelper(const std::wstring& manifestPath, unsigned timeoutMs = 15000) const;
    // Loads a verified parser module and wraps it as an ISignalParser holding a module lease.
    Result<std::shared_ptr<const ISignalParser>> loadParser(const VerifiedModule& m) const;

    // Discovers parser modules in installDir\plugins and dataDir\plugins.
    std::vector<std::wstring> discoverParserManifests() const;

    // Verifies the shipped bridge implementation and installs it under dataDir\bridge\impl\<version>
    // (immutable path), then records it as active for the host DLL.
    Result<nlohmann::json> installBridgeImplementation(const std::wstring& manifestPath) const;
    std::optional<nlohmann::json> activeBridgeImplementation() const;

    // In-process validation used by the helper process entry point.
    static int runValidationHelper(const std::wstring& manifestPath, const std::wstring& outPath);

    static bool isX64Pe(const std::wstring& path);

private:
    std::wstring installDir_, dataDir_;
    std::vector<signing::TrustedKey> keys_;
};

}  // namespace sr::engine
