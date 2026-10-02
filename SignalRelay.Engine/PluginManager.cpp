#include "PluginManager.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>

#include "SignalRelay.Core/JsonModels.h"
#include "SignalRelay.Protocol/Crypto.h"
#include "SignalRelay.Protocol/Platform.h"
#include "SignalRelayPluginAbi.h"

namespace sr::engine {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

// A loaded native parser module. Destroying the last lease destroys the instance and unloads the DLL.
struct NativeModule {
    HMODULE h = nullptr;
    void* instance = nullptr;
    SrPluginParseFn parse = nullptr;
    SrPluginDestroyFn destroy = nullptr;
    std::string id, version;
    ~NativeModule() {
        if (instance && destroy) destroy(instance);
        if (h) FreeLibrary(h);
    }
};

class NativePluginParser final : public ISignalParser {
public:
    explicit NativePluginParser(std::shared_ptr<NativeModule> m) : m_(std::move(m)) {}
    Signal parse(std::string_view text, const ParseContext& ctx) const override {
        Signal s;
        s.source = ctx.source;
        s.revision = ctx.revision;
        s.publishedAtMs = ctx.publishedAtMs;
        s.receivedAtMs = ctx.receivedAtMs;
        s.rawText.assign(text);
        s.parserId = m_->id;
        s.parserVersion = m_->version;
        std::string out(16 * 1024, '\0');
        int32_t n = 0;
        for (int attempt = 0; attempt < 2; ++attempt) {
            n = m_->parse(m_->instance, text.data(), static_cast<int32_t>(text.size()), out.data(), static_cast<int32_t>(out.size()));
            if (n >= 0) break;
            out.assign(static_cast<std::size_t>(-n), '\0');
        }
        if (n <= 0) {
            s.status = ParseStatus::Invalid;
            s.kind = MessageKind::TradeSetup;
            s.diagnostics.push_back("Native parser module returned an error");
            return s;
        }
        json j = json::parse(out.substr(0, static_cast<std::size_t>(n)), nullptr, false);
        if (j.is_discarded()) {
            s.status = ParseStatus::Invalid;
            s.diagnostics.push_back("Native parser module produced malformed output");
            return s;
        }
        s.status = parseStatusFromString(j.value("status", "Invalid")).value_or(ParseStatus::Invalid);
        const std::string kind = j.value("kind", "TradeSetup");
        s.kind = kind == "Commentary" ? MessageKind::Commentary : kind == "Management" ? MessageKind::Management : kind == "NonText" ? MessageKind::NonText
                 : kind == "Empty"    ? MessageKind::Empty
                                      : MessageKind::TradeSetup;
        s.symbol = j.value("symbol", "");
        s.symbolAlias = j.value("symbolAlias", "");
        const std::string side = j.value("side", "");
        if (side == "BUY") s.side = Side::Buy;
        if (side == "SELL") s.side = Side::Sell;
        const std::string ok = j.value("orderKind", "MARKET");
        s.orderKind = ok == "LIMIT" ? OrderKind::Limit : ok == "STOP" ? OrderKind::Stop : OrderKind::Market;
        s.explicitKind = j.value("explicitKind", false);
        s.marketIntent = j.value("marketIntent", false);
        if (j.contains("entry") && j["entry"].is_object()) s.entry = PriceRange{j["entry"].value("low", 0.0), j["entry"].value("high", 0.0)};
        if (j.contains("stopLoss") && j["stopLoss"].is_number()) s.stopLoss = j["stopLoss"].get<double>();
        if (j.contains("lot") && j["lot"].is_number()) s.lot = j["lot"].get<double>();
        for (auto& t : j.value("targets", json::array())) s.targets.push_back({t.value("index", 0), t.value("price", 0.0), {}});
        for (auto& e : j.value("evidence", json::array())) s.evidence.push_back({e.value("field", ""), {e.value("begin", 0u), e.value("end", 0u)}});
        for (auto& d : j.value("diagnostics", json::array()))
            if (d.is_string()) s.diagnostics.push_back(d.get<std::string>());
        // The engine re-checks geometry itself; a module can never weaken deterministic validation.
        if (s.status == ParseStatus::Valid) {
            std::string geo = checkGeometry(s);
            if (!geo.empty()) {
                s.status = ParseStatus::Invalid;
                s.diagnostics.push_back(geo);
            }
        }
        s.parseMicros = j.value("parseMicros", 0.0);
        return s;
    }
    std::string id() const override { return m_->id; }
    std::string version() const override { return m_->version; }

private:
    std::shared_ptr<NativeModule> m_;
};

Result<ModuleManifest> readManifest(const std::wstring& path) {
    auto text = platform::readFileUtf8(path, 1024 * 1024);
    if (!text) return Error{"manifest_missing", "Manifest not found: " + platform::narrow(path)};
    json j = json::parse(*text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return Error{"manifest_invalid", "Manifest is not valid JSON"};
    ModuleManifest m;
    m.raw = j;
    m.dir = fs::path(path).parent_path().wstring();
    m.id = j.value("id", "");
    m.version = j.value("version", "");
    m.moduleType = j.value("moduleType", "");
    m.entry = j.value("entry", "");
    m.sha256 = j.value("sha256", "");
    for (auto& f : j.value("fixtures", json::array())) m.fixtures.push_back(Fixture::fromJson(f));
    return m;
}

}  // namespace

PluginManager::PluginManager(std::wstring installDir, std::wstring dataDir) : installDir_(std::move(installDir)), dataDir_(std::move(dataDir)) {
    keys_ = signing::loadTrustedKeys(installDir_ + L"\\trusted_keys");
}

bool PluginManager::isX64Pe(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    IMAGE_DOS_HEADER dos{};
    f.read(reinterpret_cast<char*>(&dos), sizeof dos);
    if (!f || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096) return false;
    f.seekg(dos.e_lfanew);
    DWORD sig = 0;
    IMAGE_FILE_HEADER fh{};
    f.read(reinterpret_cast<char*>(&sig), sizeof sig);
    f.read(reinterpret_cast<char*>(&fh), sizeof fh);
    return f && sig == IMAGE_NT_SIGNATURE && fh.Machine == IMAGE_FILE_MACHINE_AMD64 && (fh.Characteristics & IMAGE_FILE_DLL);
}

Result<VerifiedModule> PluginManager::verify(const std::wstring& manifestPath) const {
    auto m = readManifest(manifestPath);
    if (!m) return m.error();
    ModuleManifest man = m.value();
    if (man.raw.value("format", "") != "signalrelay-module-v1") return Error{"manifest_invalid", "Unsupported module manifest format"};
    if (man.raw.value("architecture", "") != "x64") return Error{"arch_mismatch", "Module is not built for x64"};
    if (man.moduleType != "parser" && man.moduleType != "bridge_impl") return Error{"type_rejected", "Module type '" + man.moduleType + "' is not allowed"};
    const int abi = man.raw.value("abiVersion", 0);
    if ((man.moduleType == "parser" && abi != static_cast<int>(SR_PLUGIN_ABI_VERSION)) || (man.moduleType == "bridge_impl" && abi != static_cast<int>(SR_BRIDGE_IMPL_ABI_VERSION)))
        return Error{"abi_mismatch", "Module ABI version " + std::to_string(abi) + " is not supported"};
    if (man.entry.empty() || man.entry.find_first_of("/\\:") != std::string::npos || man.entry.find("..") != std::string::npos)
        return Error{"manifest_invalid", "Module entry must be a plain file name"};
    if (keys_.empty()) return Error{"no_trusted_keys", "No trusted publisher keys are installed (trusted_keys\\*.pub)"};
    man.signer = signing::verify(man.raw, keys_);
    if (man.signer.empty()) return Error{"signature_invalid", "Module manifest is not signed by a trusted publisher"};
    const std::wstring entry = man.dir + L"\\" + platform::widen(man.entry);
    auto hash = crypto::sha256File(entry);
    if (!hash) return Error{"entry_missing", "Module file is missing"};
    if (crypto::toHex(*hash) != man.sha256) return Error{"hash_mismatch", "Module content hash does not match the signed manifest"};
    if (!isX64Pe(entry)) return Error{"arch_mismatch", "Module file is not an x64 DLL"};
    return VerifiedModule{man, entry};
}

Result<json> PluginManager::validateInHelper(const std::wstring& manifestPath, unsigned timeoutMs) const {
    wchar_t exe[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    const std::wstring outFile = platform::ensureSubdir(L"staging") + L"\\validate-" + std::to_wstring(GetTickCount64()) + L".json";
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --validate-module \"" + manifestPath + L"\" --out \"" + outFile + L"\"";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return Error{"helper_failed", "Cannot start validation helper"};
    DWORD wait = WaitForSingleObject(pi.hProcess, timeoutMs);
    DWORD code = 1;
    if (wait != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 99);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    auto text = platform::readFileUtf8(outFile, 4 * 1024 * 1024);
    std::error_code ec;
    fs::remove(outFile, ec);
    if (wait != WAIT_OBJECT_0) return Error{"helper_timeout", "Module validation timed out"};
    if (!text) return Error{"helper_failed", "Validation helper crashed (exit " + std::to_string(code) + ")"};
    json j = json::parse(*text, nullptr, false);
    if (j.is_discarded()) return Error{"helper_failed", "Validation helper produced malformed output"};
    if (code != 0 || !j.value("ok", false)) return Error{"validation_failed", j.value("error", std::string("module failed validation"))};
    return j;
}

int PluginManager::runValidationHelper(const std::wstring& manifestPath, const std::wstring& outPath) {
    json result = {{"ok", false}};
    PluginManager pm(platform::exeDir(), platform::dataDir());
    auto v = pm.verify(manifestPath);
    if (!v) {
        result["error"] = v.error().message;
    } else if (v.value().manifest.moduleType == "parser") {
        auto p = pm.loadParser(v.value());
        if (!p) {
            result["error"] = p.error().message;
        } else {
            auto fixtures = v.value().manifest.fixtures;
            for (auto& n : sharedNegativeCorpus()) fixtures.push_back(n);
            auto suite = runFixtures(*p.value(), fixtures);
            result["suite"] = suite.toJson();
            result["ok"] = suite.allPassed();
            if (!suite.allPassed()) result["error"] = std::to_string(suite.failed) + " fixture(s) failed";
        }
    } else {
        HMODULE h = LoadLibraryExW(v.value().entryPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        auto get = h ? reinterpret_cast<SrBridgeImplGetApiFn>(GetProcAddress(h, "SrBridgeImplGetApi")) : nullptr;
        const SrBridgeImplApi* api = get ? get() : nullptr;
        result["ok"] = api && api->abiVersion == SR_BRIDGE_IMPL_ABI_VERSION && api->healthcheck && api->healthcheck() == 1;
        if (!result["ok"].get<bool>()) result["error"] = "bridge implementation healthcheck failed";
        if (h) FreeLibrary(h);
    }
    platform::writeFileAtomic(outPath, result.dump());
    return result.value("ok", false) ? 0 : 1;
}

Result<std::shared_ptr<const ISignalParser>> PluginManager::loadParser(const VerifiedModule& vm) const {
    if (vm.manifest.moduleType != "parser") return Error{"type_rejected", "Not a parser module"};
    auto mod = std::make_shared<NativeModule>();
    // Restricted search path: the module's own immutable directory and System32 only.
    mod->h = LoadLibraryExW(vm.entryPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!mod->h) return Error{"load_failed", "LoadLibrary failed (" + std::to_string(GetLastError()) + ")"};
    auto query = reinterpret_cast<SrPluginQueryFn>(GetProcAddress(mod->h, "SrPluginQuery"));
    auto create = reinterpret_cast<SrPluginCreateFn>(GetProcAddress(mod->h, "SrPluginCreate"));
    mod->parse = reinterpret_cast<SrPluginParseFn>(GetProcAddress(mod->h, "SrPluginParse"));
    auto health = reinterpret_cast<SrPluginHealthcheckFn>(GetProcAddress(mod->h, "SrPluginHealthcheck"));
    mod->destroy = reinterpret_cast<SrPluginDestroyFn>(GetProcAddress(mod->h, "SrPluginDestroy"));
    if (!query || !create || !mod->parse || !health || !mod->destroy) return Error{"abi_mismatch", "Module does not export the SignalRelay plugin ABI"};
    const SrPluginInfo* info = query();
    if (!info || info->abiVersion != SR_PLUGIN_ABI_VERSION || std::string(info->moduleType) != "parser" || vm.manifest.id != info->id || vm.manifest.version != info->version)
        return Error{"abi_mismatch", "Module identity or ABI version does not match its manifest"};
    mod->id = info->id;
    mod->version = info->version;
    mod->instance = create("{}", 2);
    if (!mod->instance) return Error{"create_failed", "Module instance could not be created"};
    if (health(mod->instance) != 1) return Error{"health_failed", "Module healthcheck failed"};
    return std::shared_ptr<const ISignalParser>(std::make_shared<NativePluginParser>(mod));
}

std::vector<std::wstring> PluginManager::discoverParserManifests() const {
    std::vector<std::wstring> out;
    for (const auto& root : {installDir_ + L"\\plugins", dataDir_ + L"\\plugins"}) {
        std::error_code ec;
        for (auto& idDir : fs::directory_iterator(root, ec)) {
            if (!idDir.is_directory()) continue;
            for (auto& verDir : fs::directory_iterator(idDir.path(), ec)) {
                auto man = verDir.path() / L"manifest.json";
                if (fs::exists(man)) out.push_back(man.wstring());
            }
        }
    }
    return out;
}

Result<json> PluginManager::installBridgeImplementation(const std::wstring& manifestPath) const {
    auto v = verify(manifestPath);
    if (!v) return v.error();
    if (v.value().manifest.moduleType != "bridge_impl") return Error{"type_rejected", "Not a bridge implementation"};
    // Immutable, content-addressed directory <version>-<hash prefix>: an installed file is never overwritten
    // or unloaded in place, and a different build always lands in a new directory.
    const std::wstring destDir =
        platform::ensureSubdir(L"bridge\\impl\\" + platform::widen(v.value().manifest.version + "-" + v.value().manifest.sha256.substr(0, 12)));
    const std::wstring dest = destDir + L"\\bridge_impl.dll";
    auto existing = crypto::sha256File(dest);
    if (!existing || crypto::toHex(*existing) != v.value().manifest.sha256) {
        // Immutable version directories: an existing, different file is never overwritten in place.
        if (existing) return Error{"immutable_conflict", "Installed bridge implementation file was modified; refusing to overwrite it"};
        if (!CopyFileW(v.value().entryPath.c_str(), dest.c_str(), TRUE)) return Error{"copy_failed", "Cannot install bridge implementation"};
    }
    json active = {{"version", v.value().manifest.version}, {"path", platform::narrow(dest)}, {"sha256", v.value().manifest.sha256}, {"signer", v.value().manifest.signer}};
    const std::wstring activePath = platform::ensureSubdir(L"bridge") + L"\\active_impl.json";
    auto current = activeBridgeImplementation();
    if (!current || current->value("sha256", "") != active["sha256"].get<std::string>()) platform::writeFileAtomic(activePath, active.dump(2));
    return active;
}

std::optional<json> PluginManager::activeBridgeImplementation() const {
    auto text = platform::readFileUtf8(platform::ensureSubdir(L"bridge") + L"\\active_impl.json", 64 * 1024);
    if (!text) return std::nullopt;
    json j = json::parse(*text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

}  // namespace sr::engine
