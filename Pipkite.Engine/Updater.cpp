#include "Updater.h"

#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <filesystem>

#include "AppRelease.h"
#include "Http.h"
#include "Pipkite.Core/Models.h"
#include "Pipkite.Protocol/Crypto.h"
#include "Pipkite.Protocol/Platform.h"

namespace sr::engine {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::string baseOf(const std::string& url) {
    auto slash = url.find_last_of("/\\");
    return slash == std::string::npos ? std::string{} : url.substr(0, slash + 1);
}

bool safeName(const std::string& n) { return !n.empty() && n.find_first_of("/\\:") == std::string::npos && n.find("..") == std::string::npos && n.size() < 128; }

// Tests point the release check at a local server; a normal install always asks GitHub.
std::string latestReleaseUrl() {
    wchar_t hooks[4] = {}, url[512] = {};
    if (GetEnvironmentVariableW(L"PIPKITE_TEST_HOOKS", hooks, 4) == 1 && hooks[0] == L'1') {
        const DWORD n = GetEnvironmentVariableW(L"PIPKITE_RELEASES_URL", url, 512);
        if (n > 0 && n < 512) return platform::narrow(url);
    }
    return kLatestReleaseUrl;
}

// Installed by the setup program (portable copies have no uninstaller and update by hand).
bool installedCopy() { return fs::exists(platform::exeDir() + L"\\unins000.exe"); }

// The Authenticode signature is intact and chains to a trusted root (the installer trusts Pipkite's).
bool signatureValid(const std::wstring& path) {
    WINTRUST_FILE_INFO file{sizeof file};
    file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{sizeof data};
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG r = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return r == ERROR_SUCCESS;
}

// SHA-1 thumbprint of the certificate that signed a file, "" when it isn't signed.
std::string signerThumbprint(const std::wstring& path) {
    HCERTSTORE store = nullptr;
    HCRYPTMSG msg = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(), CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr,
                          &store, &msg, nullptr))
        return {};
    std::string out;
    DWORD size = 0;
    if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &size)) {
        std::vector<BYTE> buf(size);
        if (CryptMsgGetParam(msg, CMSG_SIGNER_INFO_PARAM, 0, buf.data(), &size)) {
            const auto* signer = reinterpret_cast<const CMSG_SIGNER_INFO*>(buf.data());
            CERT_INFO wanted{};
            wanted.Issuer = signer->Issuer;
            wanted.SerialNumber = signer->SerialNumber;
            if (PCCERT_CONTEXT cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &wanted, nullptr)) {
                crypto::Bytes hash(20);
                DWORD n = static_cast<DWORD>(hash.size());
                if (CertGetCertificateContextProperty(cert, CERT_HASH_PROP_ID, hash.data(), &n)) {
                    hash.resize(n);
                    out = crypto::toHex(hash);
                }
                CertFreeCertificateContext(cert);
            }
        }
    }
    CryptMsgClose(msg);
    CertCloseStore(store, 0);
    return out;
}

std::wstring selfPath() {
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

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
    if (!appUpdate_.is_null()) {
        s["appUpdate"] = appUpdate_;
        s["appUpdate"]["installable"] = installedCopy() && !appUpdate_.value("setupUrl", "").empty();
    }
    return s;
}

void Updater::checkAppRelease(bool force) {
    // GitHub allows 60 unauthenticated requests an hour per address: every 6 hours is plenty.
    const std::int64_t now = clock_.nowMs();
    if (!force && lastAppCheckMs_ != 0 && now - lastAppCheckMs_ < 6LL * 3600 * 1000) return;
    lastAppCheckMs_ = now;
    auto r = httpRequest("GET", latestReleaseUrl(), "", "", 10000, 512 * 1024, {{"Accept", "application/vnd.github+json"}});
    if (r.status != 200) return;  // offline or rate-limited: try again next time
    json info;
    try {
        info = appReleaseInfo(json::parse(r.body), kProductVersion);
    } catch (...) {
        return;
    }
    bool fresh = false;
    {
        std::lock_guard lock(mu_);
        fresh = !info.is_null() && (appUpdate_.is_null() || appUpdate_.value("version", "") != info.value("version", ""));
        appUpdate_ = info;
    }
    if (fresh && hooks_.notify) hooks_.notify("Pipkite " + info.value("version", "") + " is available. Update from the bar at the top of the window.");
}

Result<std::wstring> Updater::downloadAppUpdate() {
    json info;
    {
        std::lock_guard lock(mu_);
        info = appUpdate_;
    }
    if (info.is_null()) return Error{"no_update", "No newer version of Pipkite was found."};
    if (!installedCopy()) return Error{"portable", "This is the portable version: download the new one from the release page."};
    const std::string name = info.value("setupName", ""), url = info.value("setupUrl", "");
    if (url.empty() || !safeName(name)) return Error{"no_installer", "This release has no installer to download."};
    const std::string mine = signerThumbprint(selfPath());
    if (mine.empty()) return Error{"unsigned", "This copy of Pipkite isn't signed, so it can't check the update. Download it from the release page."};
    auto r = httpRequest("GET", url, "", "", 120000, 256 * 1024 * 1024, {});
    if (r.status != 200 || r.body.empty()) return Error{"download_failed", "Couldn't download the update. Check your connection and try again."};
    const std::wstring path = platform::ensureSubdir(L"updates") + L"\\" + platform::widen(name);
    if (!platform::writeFileAtomic(path, r.body)) return Error{"write_failed", "Couldn't save the update."};
    // Only run an installer signed with the same certificate as this copy of Pipkite.
    if (signerThumbprint(path) != mine || !signatureValid(path)) {
        DeleteFileW(path.c_str());
        return Error{"untrusted", "The downloaded installer isn't signed by Pipkite, so it wasn't run."};
    }
    return path;
}

void Updater::startBackgroundCheck() {
    if (running_.exchange(true)) return;
    thread_ = std::thread([this] { run(false); });
}

void Updater::checkNowAsync() {
    std::thread([this] {
        auto st = checkOnce();
        if (!st) setStatus("failed", st.error().message);
        checkAppRelease(true);
    }).detach();
}

void Updater::run(bool once) {
    // Parsers are delivered while the app runs (setup lets users continue right after requesting one), so after a
    // successful check keep checking every 30 minutes; failures retry sooner, backing off up to an hour.
    constexpr int kRecheckSec = 30 * 60;
    int backoffSec = 30;
    while (running_) {
        auto st = checkOnce();
        checkAppRelease(false);
        if (once) break;
        int waitSec = kRecheckSec;
        if (st) {
            backoffSec = 30;
        } else {
            setStatus("retrying", st.error().message + "; next attempt in " + std::to_string(backoffSec) + " s");
            waitSec = backoffSec;
            backoffSec = std::min(backoffSec * 2, 3600);
        }
        for (int i = 0; i < waitSec * 10 && running_; ++i) Sleep(100);
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
    int installed = 0, failed = 0;
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
        } else if (type == "parser_template") {
            auto st = installTemplate(manifest, c, baseUrl);
            if (st) ++installed;
            else ++failed;
        }
    }
    setStatus(failed ? "partial" : "checked",
              std::to_string(installed) + " module(s) activated, " + std::to_string(failed) + " failed");
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

Status Updater::installTemplate(const json& release, const json& c, const std::string& baseUrl) {
    // Folder on the backend: template.json (+ fixtures.json, manifest.json with type/version). Whatever version the
    // backend serves is the one to run, so serving an older one rolls back. Applied once per served version, so a
    // local rollback isn't undone by the next check.
    const std::string id = c.value("id", ""), version = c.value("version", ""), releaseId = release.value("releaseId", "");
    if (!safeName(id) || !safeName(version)) return failStatus("package_invalid", "Invalid component identity");
    const std::string appliedKey = "updates.template." + id;
    if (store_.getSetting(appliedKey).value_or("") == version) return okStatus();
    // a version that failed here fails the same way next time: tried again only after an app update or a new version
    const std::string failedKey = "updates.templateFailed." + id, failedValue = version + "|" + kProductVersion;
    if (store_.getSetting(failedKey).value_or("") == failedValue) return okStatus();
    auto fail = [&](const std::string& why) {
        store_.putSetting(failedKey, failedValue);
        store_.addUpdateHistory(releaseId, id, "", version, "failed", {{"error", why}}, false);
        return failStatus("template_failed", why);
    };
    const std::wstring dir = platform::ensureSubdir(L"parsers\\" + platform::widen(id) + L"\\" + platform::widen(version));
    auto dl = download(c, baseUrl, dir);
    if (!dl) return fail(dl.error().message);
    auto readJson = [&](const wchar_t* name) {
        auto text = platform::readFileUtf8(dir + L"\\" + name, 1024 * 1024);
        return text ? json::parse(*text, nullptr, false) : json();
    };
    json tmpl = readJson(L"template.json");
    if (!tmpl.is_object()) return fail("template.json is missing or malformed");
    json fixtures = readJson(L"fixtures.json");
    if (!fixtures.is_array()) fixtures = json::array();
    json notes = {{"releaseId", releaseId}, {"component", id}, {"activeVersion", version}, {"restartRequired", false}, {"rollbackAvailable", true}};
    auto st = hooks_.activateTemplate ? hooks_.activateTemplate(id, version, tmpl, fixtures, notes) : failStatus("no_hook", "engine unavailable");
    if (!st) return fail(st.error().message);
    store_.putSetting(appliedKey, version);
    store_.addUpdateHistory(releaseId, id, "", version, "activated", notes, false);
    return okStatus();
}

}  // namespace sr::engine
