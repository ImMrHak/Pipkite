#pragma once
// update check on every launch, in the background (3s timeout, then backoff), and every 30 minutes after.
// manifest signature and file hashes are verified. parser/bridge modules hot-reload. new app versions come
// from GitHub releases: the UI offers them and runs the installer. never touches copying mode, risk or sources.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "Http.h"
#include "PluginManager.h"
#include "Pipkite.Core/Clock.h"
#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Storage/Store.h"

namespace sr::engine {

struct UpdaterHooks {
    // Called on the updater thread; implementations post work to the engine thread.
    std::function<Status(const std::string& parserId, const std::string& version, std::shared_ptr<const ISignalParser> parser, const nlohmann::json& notes)> activateParser;
    // A delivered parser configuration (template JSON + optional fixtures); must pass them before activating.
    std::function<Status(const std::string& parserId, const std::string& version, const nlohmann::json& templateJson, const nlohmann::json& fixtures,
                         const nlohmann::json& notes)>
        activateTemplate;
    std::function<void(const nlohmann::json& activeImpl)> upgradeBridge;
    std::function<std::vector<std::string>()> savedSamples;
    std::function<void(const std::string& message)> notify;
};

class Updater {
public:
    Updater(db::Store& store, PluginManager& plugins, const IClock& clock, UpdaterHooks hooks);
    ~Updater();

    void startBackgroundCheck();
    void checkNowAsync();
    nlohmann::json status() const;
    // Downloads the newer version's installer and checks it is signed like this copy. Returns its path.
    Result<std::wstring> downloadAppUpdate();

    std::string manifestUrl() const;
    std::string channel() const;
    bool autoCompatible() const;

private:
    void run(bool once);
    Status checkOnce();
    Status processManifest(const nlohmann::json& manifest, const std::string& baseUrl);
    Status installModule(const nlohmann::json& release, const nlohmann::json& component, const std::string& baseUrl);
    Status installTemplate(const nlohmann::json& release, const nlohmann::json& component, const std::string& baseUrl);
    void checkAppRelease(bool force);
    Result<std::wstring> download(const nlohmann::json& component, const std::string& baseUrl, const std::wstring& destDir);
    HttpHeaders licenseHeaders() const;
    void setStatus(const std::string& state, const std::string& detail);

    db::Store& store_;
    PluginManager& plugins_;
    const IClock& clock_;
    UpdaterHooks hooks_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    mutable std::mutex mu_;
    nlohmann::json status_ = {{"state", "idle"}};
    nlohmann::json appUpdate_;  // newer GitHub release, null when up to date (guarded by mu_)
    std::atomic<std::int64_t> lastAppCheckMs_{0};
};

}  // namespace sr::engine
