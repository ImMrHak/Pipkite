#pragma once
// telegram user session over tdjson (loaded at runtime). push updates only, no history polling.
// never logs codes, passwords or api secrets.

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "Pipkite.Core/Interfaces.h"

namespace sr::engine {

class TelegramClient final : public ITelegramSource {
public:
    TelegramClient(const IClock& clock, std::wstring dataDir);
    ~TelegramClient() override;

    void start(std::function<void(IncomingMessage)> onMessage) override;
    void stop() override;
    TelegramAuthState authState() const override { return state_.load(); }
    std::vector<ChatInfo> chats() const override;

    // Credentials are the user's own api_id/api_hash from my.telegram.org; stored DPAPI-protected.
    bool setCredentials(std::int64_t apiId, const std::string& apiHash, std::string& error);
    bool hasCredentials() const;
    void submitPhone(const std::string& phone);
    void submitCode(const std::string& code);
    void submitPassword(const std::string& password);
    void logout();
    bool deleteSession(std::string& error);  // removes local TDLib data; trade history untouched
    void setAllowlist(const std::set<std::int64_t>& chatIds);
    void setSessionStartMs(std::int64_t ms) { sessionStartMs_ = ms; }

    std::string lastError() const;
    std::string libraryPath() const { return libraryPath_; }
    std::int64_t floodWaitUntilMs() const { return floodWaitUntil_.load(); }
    std::string userDisplayName() const;

private:
    void loop();
    void send(const nlohmann::json& request);
    void handle(const nlohmann::json& update);
    void onAuthState(const nlohmann::json& st);
    void onMessage(const nlohmann::json& message, std::int64_t editVersion);
    void setError(const std::string& e);
    bool loadLibrary();
    std::string encryptionKey();

    const IClock& clock_;
    std::wstring dataDir_;
    std::string libraryPath_;
    void* lib_ = nullptr;
    int (*createClient_)() = nullptr;
    void (*send_)(int, const char*) = nullptr;
    const char* (*receive_)(double) = nullptr;
    const char* (*execute_)(const char*) = nullptr;
    int clientId_ = 0;

    std::atomic<TelegramAuthState> state_{TelegramAuthState::NotConfigured};
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::function<void(IncomingMessage)> onMessage_;
    mutable std::mutex mu_;
    std::map<std::int64_t, ChatInfo> chats_;
    std::set<std::int64_t> allowlist_;
    std::map<std::pair<std::int64_t, std::int64_t>, nlohmann::json> pendingEditContent_;
    std::map<std::pair<std::int64_t, std::int64_t>, std::int64_t> pendingEditDate_;
    std::string lastError_;
    std::string me_;
    std::atomic<std::int64_t> floodWaitUntil_{0};
    std::atomic<std::int64_t> sessionStartMs_{0};
};

}  // namespace sr::engine
