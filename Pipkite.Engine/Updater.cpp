#include "Updater.h"

#include <windows.h>

#include <filesystem>

#include "Http.h"
#include "Pipkite.Core/Models.h"
#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Platform.h"

namespace sr::engine {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

int compareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) {
        std::vector<int> p;
        std::size_t start = 0;
        while (start <= v.size()) {
            auto dot = v.find('.', start);
            try {
                p.push_back(std::stoi(v.substr(start, dot - start)));
            } catch (...) {
                p.push_back(0);
            }
            if (dot == std::string::npos) break;
            start = dot + 1;
        }
        while (p.size() < 3) p.push_back(0);
        return p;
    };
    auto pa = parts(a), pb = parts(b);
    for (std::size_t i = 0; i < 3; ++i)
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    return 0;
}

std::string baseOf(const std::string& url) {
    auto slash = url.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : url.substr(0, slash + 1);
}

bool safeName(const std::string& n) { return !n.empty() && n.find_first_of("/\\:") == std::string::npos && n.find("..") == std::string::npos && n.size() < 128; }

}  // namespace

HttpHeaders Updater::licenseHeaders() const {
    // The publisher update service requires a valid license key; omit the header when none is configured.
    auto key = store_.getSetting("license.key").value_or("");
    if (key.empty()) return {};
    return {{"X-License-Key", key}};
}

Updater::Updater(db::Store& store, PluginManager& plugins, const IClock& clock, UpdaterHooks hooks) : store_(store), plugins_(plugins), clock_(clock), hooks_(std::move(hooks)) {}

Updater::~Updater() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

std::string Updater::manifestUrl() const {
    auto v = store_.getSetting("updates.manifestUrl");
    // No publisher update service is configured for this build; the check reports that honestly.
    return v ? *v : std::string{};
}

std::string Updater::channel() const { return store_.getSetting("updates.channel").value_or("stable"); }
bool Updater::autoCompatible() const { return store_.getSetting("updates.autoCompatible").value_or("true") == "true"; }

void Updater::setStatus(const std::string& state, const std::string& detail) {
    std::lock_guard lock(mu_);
    status_["state"] = state;
    status_["detail"] = detail;
    status_["at"] = clock_.nowMs();
    status_["channel"] = channel();
}

json Updater::status() const {
    std::lock_guard lock(mu_);
    json s = status_;
    s["installedVersion"] = kProductVersion;
    // A staged CORE update (new app/UI binaries) needs a restart to apply; parser/bridge updates hot-reload
    // and never set this. The UI uses restartPending to prompt the user "update now or later".
    const std::wstring pending = platform::ensureSubdir(L"versions") + L"\\pending.json";
    if (std::filesystem::exists(pending)) {
        if (auto txt = platform::readFileUtf8(pending, 64 * 1024)) {
            json pj = json::parse(*txt, nullptr, false);
            if (!pj.is_discarded()) {
                s["restartPending"] = true;
                s["pendingVersion"] = pj.value("version", "");
            }
        }
    }
    return s;
}

void Updater::startBackgroundCheck() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(false); });
}

void Updater::checkNowAsync() {
    std::thread([this] {
        auto st = checkOnce();
        if (!st) setStatus("failed", st.error().message);
    }).detach();
}

void Updater::run(bool once) {
    int backoffSec = 30;
    while (running_) {
        auto st = checkOnce();
        if (st || once) break;
        setStatus("retrying", st.error().message + "; next attempt in " + std::to_string(backoffSec) + " s");
        for (int i = 0; i < backoffSec * 10 && running_; ++i) Sleep(100);
        backoffSec = std::min(backoffSec * 2, 3600);
    }
}

Status Updater::checkOnce() {
    const std::string url = manifestUrl();
    if (url.empty()) {
        setStatus("not_configured", "No update service is configured; cached components are in use");
        return okStatus();
    }
    setStatus("checking", url);
    auto r = httpRequest("GET", url, "", "", 3000, 1024 * 1024, licenseHeaders());  // short initial timeout; launch already continued
    if (r.status != 200) {
        setStatus("unavailable", r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error);
        return failStatus("update_unavailable", "Update service unavailable (" + (r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error) + ")");
    }
    json manifest = json::parse(r.body, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()) {
        setStatus("failed", "malformed release manifest");
        return failStatus("manifest_invalid", "Release manifest is malformed");
    }
    return processManifest(manifest, baseOf(url));
}

Status Updater::processManifest(const json& manifest, const std::string& baseUrl) {
    const std::string signer = signing::verify(manifest, plugins_.trustedKeys());
    if (signer.empty()) {
        setStatus("rejected", "release manifest signature is not trusted");
        store_.addUpdateHistory(manifest.value("releaseId", "unknown"), "manifest", kProductVersion, manifest.value("version", ""), "failed",
                                {{"error", "untrusted signature"}}, false);
        return failStatus("signature_invalid", "Release manifest is not signed by a trusted publisher");
    }
    if (manifest.value("architecture", "x64") != "x64") return failStatus("arch_mismatch", "Release is not for x64");
    if (!manifest.value("channel", "stable").empty() && manifest.value("channel", "stable") != channel()) {
        setStatus("up_to_date", "No release for channel " + channel());
        return okStatus();
    }
    // Staged rollout: a stable per-install bucket (0-99) compared against the release percentage.
    const int rollout = manifest.value("rolloutPercent", 100);
    auto bucketStr = store_.getSetting("updates.bucket");
    int bucket = bucketStr ? std::stoi(*bucketStr) : static_cast<int>(crypto::randomBytes(1)[0] % 100);
    if (!bucketStr) store_.putSetting("updates.bucket", std::to_string(bucket));
    if (bucket >= rollout) {
        setStatus("up_to_date", "Release is rolling out gradually; this installation is not yet included");
        return okStatus();
    }
    int installed = 0, staged = 0, failed = 0;
    for (auto& c : manifest.value("components", json::array())) {
        const std::string type = c.value("type", "");
        if (c.value("minHostAbi", 1) > 1) {
            ++failed;
            continue;
        }
        if (type == "parser_module" || type == "bridge_impl") {
            if (!autoCompatible()) {
                if (hooks_.notify) hooks_.notify("Compatible module update " + c.value("id", "") + " " + c.value("version", "") + " is available");
                continue;
            }
            auto st = installModule(manifest, c, baseUrl);
            if (st) ++installed;
            else ++failed;
        } else if (type == "core") {
            if (compareVersions(c.value("version", "0"), kProductVersion) <= 0) continue;
            auto st = stageCore(manifest, c, baseUrl);
            if (st) ++staged;
            else ++failed;
        }
    }
    setStatus(failed ? "partial" : "checked",
              std::to_string(installed) + " module(s) activated, " + std::to_string(staged) + " core release(s) staged, " + std::to_string(failed) + " failed");
    return okStatus();
}

Result<std::wstring> Updater::download(const json& component, const std::string& baseUrl, const std::wstring& destDir) {
    std::error_code ec;
    fs::create_directories(destDir, ec);
    for (auto& f : component.value("files", json::array())) {
        const std::string name = f.value("name", "");
        if (!safeName(name)) return Error{"package_invalid", "Unsafe file name in package"};
        std::string url = f.value("url", name);
        if (url.find(':') == std::string::npos) url = baseUrl + url;
        auto r = httpRequest("GET", url, "", "", 30000, 256 * 1024 * 1024, licenseHeaders());
        if (r.status != 200) return Error{"download_failed", "Download failed for " + name};
        if (f.contains("size") && f["size"].get<std::uint64_t>() != r.body.size()) return Error{"size_mismatch", "Size mismatch for " + name};
        if (crypto::toHex(crypto::sha256(r.body)) != f.value("sha256", "")) return Error{"hash_mismatch", "Hash mismatch for " + name};  // corrupted/incompatible package rejected
        if (!platform::writeFileAtomic(destDir + L"\\" + platform::widen(name), r.body)) return Error{"write_failed", "Cannot write " + name};
    }
    return destDir;
}

Status Updater::installModule(const json& release, const json& c, const std::string& baseUrl) {
    const std::string id = c.value("id", ""), version = c.value("version", ""), type = c.value("type", "");
    const std::string releaseId = release.value("releaseId", "");
    if (!safeName(id) || !safeName(version)) return failStatus("package_invalid", "Invalid component identity");
    const std::wstring dir = type == "parser_module" ? platform::ensureSubdir(L"plugins\\" + platform::widen(id) + L"\\" + platform::widen(version))
                                                     : platform::ensureSubdir(L"updates\\bridge\\" + platform::widen(version));
    if (fs::exists(dir + L"\\manifest.json")) {
        auto v = plugins_.verify(dir + L"\\manifest.json");
        if (v) return okStatus();  // already installed; persisted separately from downloads
    }
    auto fail = [&](const std::string& why) {
        store_.addUpdateHistory(releaseId, id, "", version, "failed", {{"error", why}}, false);
        return failStatus("module_failed", why);
    };
    auto dl = download(c, baseUrl, dir);
    if (!dl) return fail(dl.error().message);
    store_.addUpdateHistory(releaseId, id, "", version, "staged", json::object(), false);
    auto verified = plugins_.verify(dir + L"\\manifest.json");
    if (!verified) return fail(verified.error().message);
    auto helper = plugins_.validateInHelper(dir + L"\\manifest.json");
    if (!helper) return fail(helper.error().message);

    json changelog = json::array();
    for (auto& e : release.value("changelog", json::array()))
        if (e.value("component", "") == id || e.value("component", "") == "*") changelog.push_back(e);
    json notes = {{"releaseId", releaseId},
                  {"component", id},
                  {"installedVersion", kProductVersion},
                  {"activeVersion", version},
                  {"changelog", changelog},
                  {"fixtures", helper.value().value("suite", json::object())},
                  {"restartRequired", false},
                  {"rollbackAvailable", true},
                  {"signer", verified.value().manifest.signer}};
    if (type == "parser_module") {
        auto parser = plugins_.loadParser(verified.value());
        if (!parser) return fail(parser.error().message);
        auto st = hooks_.activateParser ? hooks_.activateParser(id, version, parser.value(), notes) : failStatus("no_hook", "engine unavailable");
        if (!st) return fail(st.error().message);
    } else {
        auto active = plugins_.installBridgeImplementation(dir + L"\\manifest.json");
        if (!active) return fail(active.error().message);
        if (hooks_.upgradeBridge) hooks_.upgradeBridge(active.value());
        store_.addUpdateHistory(releaseId, id, "", version, "activated", notes, false);
    }
    return okStatus();
}

Status Updater::stageCore(const json& release, const json& c, const std::string& baseUrl) {
    const std::string version = c.value("version", "");
    if (!safeName(version)) return failStatus("package_invalid", "Invalid core version");
    const std::wstring dir = platform::ensureSubdir(L"versions\\" + platform::widen(version));
    auto dl = download(c, baseUrl, dir);
    if (!dl) {
        store_.addUpdateHistory(release.value("releaseId", ""), "core", kProductVersion, version, "failed", {{"error", dl.error().message}}, true);
        return dl.error();
    }
    json pending = {{"version", version}, {"dir", platform::narrow(dir)}, {"entry", c.value("entry", "Pipkite.Launcher.exe")}, {"releaseId", release.value("releaseId", "")},
                    {"requiresReconciliation", true}};
    platform::writeFileAtomic(platform::ensureSubdir(L"versions") + L"\\pending.json", pending.dump(2));
    json changelog = release.value("changelog", json::array());
    store_.addUpdateHistory(release.value("releaseId", ""), "core", kProductVersion, version, "staged", {{"changelog", changelog}, {"restartRequired", true}}, true);
    if (hooks_.notify) hooks_.notify("Pipkite " + version + " is staged and will be applied on the next safe restart");
    return okStatus();
}

}  // namespace sr::engine
