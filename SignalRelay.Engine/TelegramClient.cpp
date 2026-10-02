#include "TelegramClient.h"

#include <windows.h>

#include <filesystem>

#include "SignalRelay.Protocol/Crypto.h"
#include "SignalRelay.Protocol/Platform.h"

namespace sr::engine {

using nlohmann::json;

namespace {

std::string messageText(const json& content, bool& hasMedia, std::string& mediaKind) {
    const std::string type = content.value("@type", "");
    hasMedia = false;
    if (type == "messageText") return content["text"].value("text", "");
    hasMedia = true;
    mediaKind = type.size() > 7 ? type.substr(7) : type;  // messagePhoto -> Photo
    if (content.contains("caption") && content["caption"].is_object()) return content["caption"].value("text", "");
    return {};
}

}  // namespace

TelegramClient::TelegramClient(const IClock& clock, std::wstring dataDir) : clock_(clock), dataDir_(std::move(dataDir)) {}

TelegramClient::~TelegramClient() { stop(); }

bool TelegramClient::loadLibrary() {
    if (lib_) return true;
    // tdjson is optional. silence loader dialogs while probing (bad/missing dll -> "TDLib not installed")
    // and look for its dependencies next to it.
    const UINT prevMode = ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    struct ModeGuard {
        UINT m;
        ~ModeGuard() { ::SetErrorMode(m); }
    } guard{prevMode};
    ::SetErrorMode(prevMode | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    const std::wstring candidates[] = {platform::exeDir() + L"\\tdjson.dll", platform::exeDir() + L"\\tdlib\\tdjson.dll", L"tdjson.dll"};
    for (auto& c : candidates) {
        const bool hasPath = c.find(L'\\') != std::wstring::npos;
        // LOAD_LIBRARY_SEARCH_* resolves dependencies from the DLL's own directory + the default dirs.
        DWORD flags = hasPath ? (LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) : 0;
        HMODULE h = LoadLibraryExW(c.c_str(), nullptr, flags);
        if (!h && hasPath) h = LoadLibraryExW(c.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);  // older Windows fallback
        if (!h) continue;
        createClient_ = reinterpret_cast<int (*)()>(GetProcAddress(h, "td_create_client_id"));
        send_ = reinterpret_cast<void (*)(int, const char*)>(GetProcAddress(h, "td_send"));
        receive_ = reinterpret_cast<const char* (*)(double)>(GetProcAddress(h, "td_receive"));
        execute_ = reinterpret_cast<const char* (*)(const char*)>(GetProcAddress(h, "td_execute"));
        if (createClient_ && send_ && receive_ && execute_) {
            lib_ = h;
            libraryPath_ = platform::narrow(c);
            return true;
        }
        FreeLibrary(h);
    }
    return false;
}

void TelegramClient::start(std::function<void(IncomingMessage)> onMessage) {
    onMessage_ = std::move(onMessage);
    if (!loadLibrary()) {
        state_ = TelegramAuthState::LibraryMissing;
        setError("Telegram support (tdjson.dll) could not be loaded. It may be missing, blocked by antivirus, or missing its runtime files (reinstall SignalRelay). Preview and the simulated source still work. See docs/telegram-tdlib.md.");
        return;
    }
    if (!hasCredentials()) {
        state_ = TelegramAuthState::NotConfigured;
        return;
    }
    if (running_) return;
    // Quiet TDLib's own logging (never log session material).
    execute_(R"({"@type":"setLogVerbosityLevel","new_verbosity_level":1})");
    clientId_ = createClient_();
    running_ = true;
    state_ = TelegramAuthState::WaitParameters;
    thread_ = std::thread([this] { loop(); });
    send({{"@type", "getOption"}, {"name", "version"}});
}

void TelegramClient::stop() {
    if (!running_.exchange(false)) return;
    if (send_) send({{"@type", "close"}});
    if (thread_.joinable()) thread_.join();
}

std::vector<ChatInfo> TelegramClient::chats() const {
    std::lock_guard lock(mu_);
    std::vector<ChatInfo> out;
    for (auto& [id, c] : chats_)
        if (c.type != "private") out.push_back(c);
    return out;
}

bool TelegramClient::hasCredentials() const { return platform::loadSecret(L"telegram_api").has_value(); }

bool TelegramClient::setCredentials(std::int64_t apiId, const std::string& apiHash, std::string& error) {
    if (apiId <= 0 || apiHash.size() != 32) {
        error = "api_id must be a positive number and api_hash 32 hex characters (from my.telegram.org)";
        return false;
    }
    json j = {{"apiId", apiId}, {"apiHash", apiHash}};
    std::string s = j.dump();
    auto prot = crypto::dpapiProtect(crypto::Bytes(s.begin(), s.end()), L"SignalRelay telegram api");
    if (!prot) {
        error = "DPAPI protection failed";
        return false;
    }
    const std::wstring path = platform::ensureSubdir(L"secrets") + L"\\telegram_api.bin";
    if (!platform::writeFileAtomic(path, std::string_view(reinterpret_cast<const char*>(prot->data()), prot->size()))) {
        error = "cannot store credentials";
        return false;
    }
    if (state_ == TelegramAuthState::LibraryMissing) return true;
    if (!running_) {
        start(onMessage_);
    } else if (state_ != TelegramAuthState::Ready) {
        // tdlib only reads api_id/hash at startup, so restart it to pick up corrected credentials.
        // (already authorized = they were fine)
        stop();
        start(onMessage_);
    }
    return true;
}

std::string TelegramClient::encryptionKey() {
    auto k = platform::loadOrCreateSecret(L"tdlib_db_key");
    return k ? crypto::toBase64(*k) : std::string{};
}

void TelegramClient::send(const json& request) {
    if (!send_ || clientId_ == 0) return;
    send_(clientId_, request.dump().c_str());
}

void TelegramClient::submitPhone(const std::string& phone) { send({{"@type", "setAuthenticationPhoneNumber"}, {"phone_number", phone}}); }
void TelegramClient::submitCode(const std::string& code) { send({{"@type", "checkAuthenticationCode"}, {"code", code}}); }
void TelegramClient::submitPassword(const std::string& password) { send({{"@type", "checkAuthenticationPassword"}, {"password", password}}); }
void TelegramClient::logout() { send({{"@type", "logOut"}}); }

bool TelegramClient::deleteSession(std::string& error) {
    stop();
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(dataDir_) / L"tdlib", ec);
    if (ec) {
        error = "could not delete local Telegram data: " + ec.message();
        return false;
    }
    platform::deleteSecret(L"tdlib_db_key");
    {
        std::lock_guard lock(mu_);
        chats_.clear();
        me_.clear();
    }
    state_ = hasCredentials() ? TelegramAuthState::Closed : TelegramAuthState::NotConfigured;
    return true;
}

void TelegramClient::setAllowlist(const std::set<std::int64_t>& ids) {
    std::lock_guard lock(mu_);
    allowlist_ = ids;
}

std::string TelegramClient::lastError() const {
    std::lock_guard lock(mu_);
    return lastError_;
}

std::string TelegramClient::userDisplayName() const {
    std::lock_guard lock(mu_);
    return me_;
}

void TelegramClient::setError(const std::string& e) {
    std::lock_guard lock(mu_);
    lastError_ = e;
}

void TelegramClient::loop() {
    while (running_) {
        const char* r = receive_(1.0);
        if (!r) continue;
        json j = json::parse(r, nullptr, false);
        if (j.is_discarded()) continue;
        try {
            handle(j);
        } catch (const std::exception& e) {
            setError(std::string("TDLib update handling error: ") + e.what());
        }
    }
}

void TelegramClient::onAuthState(const json& st) {
    const std::string t = st.value("@type", "");
    if (t == "authorizationStateWaitTdlibParameters") {
        state_ = TelegramAuthState::WaitParameters;
        auto secret = platform::loadSecret(L"telegram_api");
        if (!secret) {
            state_ = TelegramAuthState::NotConfigured;
            return;
        }
        json cred = json::parse(std::string(secret->begin(), secret->end()), nullptr, false);
        const std::string db = platform::narrow(dataDir_) + "\\tdlib";
        send({{"@type", "setTdlibParameters"},
              {"use_test_dc", false},
              {"database_directory", db},
              {"files_directory", db + "\\files"},
              {"database_encryption_key", encryptionKey()},
              {"use_file_database", false},
              {"use_chat_info_database", true},
              {"use_message_database", true},
              {"use_secret_chats", false},
              {"api_id", cred.value("apiId", 0)},
              {"api_hash", cred.value("apiHash", "")},
              {"system_language_code", "en"},
              {"device_model", "Windows PC"},
              {"system_version", "Windows"},
              {"application_version", "SignalRelay 1.0.0"}});
    } else if (t == "authorizationStateWaitPhoneNumber") {
        state_ = TelegramAuthState::WaitPhone;
    } else if (t == "authorizationStateWaitCode") {
        state_ = TelegramAuthState::WaitCode;
    } else if (t == "authorizationStateWaitPassword") {
        state_ = TelegramAuthState::WaitPassword;
    } else if (t == "authorizationStateReady") {
        state_ = TelegramAuthState::Ready;
        setError("");
        send({{"@type", "getMe"}});
        send({{"@type", "loadChats"}, {"chat_list", {{"@type", "chatListMain"}}}, {"limit", 500}});
    } else if (t == "authorizationStateLoggingOut") {
        state_ = TelegramAuthState::LoggingOut;
    } else if (t == "authorizationStateClosed") {
        state_ = TelegramAuthState::Closed;
        running_ = false;
    } else if (t == "authorizationStateWaitOtherDeviceConfirmation" || t == "authorizationStateWaitRegistration" || t == "authorizationStateWaitEmailAddress" ||
               t == "authorizationStateWaitEmailCode") {
        state_ = TelegramAuthState::Error;
        setError("This login flow step (" + t + ") must be completed in an official Telegram app first.");
    }
}

void TelegramClient::handle(const json& u) {
    const std::string type = u.value("@type", "");
    if (type == "updateAuthorizationState") {
        onAuthState(u["authorization_state"]);
    } else if (type == "updateNewChat") {
        const json& c = u["chat"];
        ChatInfo info;
        info.chatId = c.value("id", static_cast<std::int64_t>(0));
        info.title = c.value("title", "");
        const json& ct = c["type"];
        const std::string t = ct.value("@type", "");
        if (t == "chatTypeSupergroup") info.type = ct.value("is_channel", false) ? "channel" : "supergroup";
        else if (t == "chatTypeBasicGroup") info.type = "group";
        else info.type = "private";
        std::lock_guard lock(mu_);
        chats_[info.chatId] = info;
    } else if (type == "updateChatTitle") {
        std::lock_guard lock(mu_);
        auto it = chats_.find(u.value("chat_id", static_cast<std::int64_t>(0)));
        if (it != chats_.end()) it->second.title = u.value("title", "");  // ID binding unaffected by renames
    } else if (type == "updateNewMessage") {
        onMessage(u["message"], 0);
    } else if (type == "updateMessageContent") {
        auto key = std::make_pair(u.value("chat_id", static_cast<std::int64_t>(0)), u.value("message_id", static_cast<std::int64_t>(0)));
        std::int64_t editDate = 0;
        {
            std::lock_guard lock(mu_);
            auto it = pendingEditDate_.find(key);
            if (it != pendingEditDate_.end()) {
                editDate = it->second;
                pendingEditDate_.erase(it);
            } else {
                pendingEditContent_[key] = u["new_content"];
            }
        }
        if (editDate) {
            json m = {{"id", key.second}, {"chat_id", key.first}, {"date", editDate}, {"content", u["new_content"]}};
            onMessage(m, editDate);
        }
    } else if (type == "updateMessageEdited") {
        auto key = std::make_pair(u.value("chat_id", static_cast<std::int64_t>(0)), u.value("message_id", static_cast<std::int64_t>(0)));
        const std::int64_t editDate = u.value("edit_date", static_cast<std::int64_t>(0));
        json content;
        {
            std::lock_guard lock(mu_);
            auto it = pendingEditContent_.find(key);
            if (it != pendingEditContent_.end()) {
                content = it->second;
                pendingEditContent_.erase(it);
            } else {
                pendingEditDate_[key] = editDate;
            }
        }
        if (!content.is_null()) {
            json m = {{"id", key.second}, {"chat_id", key.first}, {"date", editDate}, {"content", content}};
            onMessage(m, editDate);
        }
    } else if (type == "user") {
        std::lock_guard lock(mu_);
        me_ = u.value("first_name", "") + (u.contains("usernames") && u["usernames"].is_object() && !u["usernames"]["active_usernames"].empty()
                                               ? " (@" + u["usernames"]["active_usernames"][0].get<std::string>() + ")"
                                               : "");
    } else if (type == "error") {
        const int code = u.value("code", 0);
        const std::string msg = u.value("message", "");
        if (code == 429 || msg.rfind("FLOOD_WAIT_", 0) == 0 || msg.find("Too Many Requests") != std::string::npos) {
            int secs = 30;
            auto pos = msg.find_last_of('_');
            if (pos != std::string::npos) {
                try {
                    secs = std::stoi(msg.substr(pos + 1));
                } catch (...) {
                }
            }
            floodWaitUntil_ = clock_.nowMs() + static_cast<std::int64_t>(secs) * 1000;  // respect flood wait; no retries meanwhile
            setError("Telegram asked to wait " + std::to_string(secs) + " s (flood control)");
        } else if (msg == "PHONE_CODE_INVALID" || msg == "PHONE_CODE_EXPIRED") {
            setError(msg == "PHONE_CODE_INVALID" ? "The login code is invalid" : "The login code expired; request a new one");
        } else if (msg == "PASSWORD_HASH_INVALID") {
            setError("The two-step verification password is incorrect");
        } else if (msg == "AUTH_KEY_UNREGISTERED" || msg == "SESSION_REVOKED" || msg == "USER_DEACTIVATED") {
            state_ = TelegramAuthState::Closed;
            setError("The Telegram session was revoked; sign in again");
        } else if (!msg.empty()) {
            setError("Telegram: " + msg);  // TDLib error text never contains codes or passwords we sent
        }
    }
}

void TelegramClient::onMessage(const json& m, std::int64_t editVersion) {
    const std::int64_t chatId = m.value("chat_id", static_cast<std::int64_t>(0));
    {
        std::lock_guard lock(mu_);
        if (!allowlist_.count(chatId)) return;  // logical subscription by chat ID, never by title
    }
    IncomingMessage im;
    im.chatId = chatId;
    im.messageId = m.value("id", static_cast<std::int64_t>(0));
    im.editVersion = editVersion;
    im.publishedAtMs = m.value("date", static_cast<std::int64_t>(0)) * 1000;
    im.receivedAtMs = clock_.nowMs();
    im.receivedMonoUs = clock_.monotonicUs();
    if (m.contains("sender_id") && m["sender_id"].is_object()) {
        const json& s = m["sender_id"];
        im.senderId = s.contains("user_id") ? s.value("user_id", static_cast<std::int64_t>(0)) : s.value("chat_id", static_cast<std::int64_t>(0));
    }
    if (m.contains("reply_to") && m["reply_to"].is_object()) im.replyToMessageId = m["reply_to"].value("message_id", static_cast<std::int64_t>(0));
    if (m.contains("content")) im.text = messageText(m["content"], im.hasMedia, im.mediaKind);
    // Messages published before this live session started (sync after reconnect) are historical.
    const std::int64_t start = sessionStartMs_.load();
    if (editVersion == 0 && start > 0 && im.publishedAtMs + 5000 < start) im.historical = true;
    if (onMessage_) onMessage_(std::move(im));
}

}  // namespace sr::engine
