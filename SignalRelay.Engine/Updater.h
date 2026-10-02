#pragma once
// update check on every launch, in the background (3s timeout, then backoff).
// manifest signature and file hashes are verified. parser/bridge modules hot-reload, core releases
// go through the launcher. never touches copying mode, risk or sources.

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "Http.h"
#include "PluginManager.h"
#include "SignalRelay.Core/Clock.h"
#include "SignalRelay.Core/ParserBuilder.h"
#include "SignalRelay.Storage/Store.h"

namespace sr::engine {

struct UpdaterHooks {
    // Called on the updater thread; implementations post work to the engine thread.
    std::function<Status(const std::string& parserId, const std::string& version, std::shared_ptr<const ISignalParser> parser, const nlohmann::json& notes)> activateParser;
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

    std::string manifestUrl() const;
    std::string channel() const;
    bool autoCompatible() const;

private:
    void run(bool once);
    Status checkOnce();
    Status processManifest(const nlohmann::json& manifest, const std::string& baseUrl);
    Status installModule(const nlohmann::json& release, const nlohmann::json& component, const std::string& baseUrl);
    Status stageCore(const nlohmann::json& release, const nlohmann::json& component, const std::string& baseUrl);
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
};

}  // namespace sr::engine
