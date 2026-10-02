// UI-facing side of the engine: authenticated local pipe, request routing and snapshot publishing.
// The UI owns presentation only; every trading decision stays here.

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <regex>

#include "Engine.h"
#include "Http.h"
#include "TerminalLauncher.h"
#include "SignalRelay.Core/JsonModels.h"
#include "SignalRelay.Core/StateMachine.h"
#include "SignalRelay.Core/Uuid.h"
#include "SignalRelay.Protocol/Crypto.h"
#include "SignalRelay.Protocol/Platform.h"
#include "SignalRelay.Storage/Backup.h"

namespace sr::engine {

using nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct UiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& m) { throw UiError(m); }

std::int64_t startOfDayMs(std::int64_t nowMs, int utcOffsetMinutes) {
    const std::int64_t shifted = nowMs + static_cast<std::int64_t>(utcOffsetMinutes) * 60000;
    return shifted - (shifted % 86400000LL) - static_cast<std::int64_t>(utcOffsetMinutes) * 60000;
}

json summaryJson(const ProfitSummary& s) {
    return {{"net", s.net},           {"profit", s.profit}, {"commission", s.commission}, {"swap", s.swap},        {"fee", s.fee},     {"deposits", s.deposits},
            {"withdrawals", s.withdrawals}, {"deals", s.dealCount}, {"unresolved", s.unresolvedCount}, {"wins", s.wins}, {"losses", s.losses}};
}

const std::set<std::string> kUiSettings{"ui.theme",         "ui.minimizeToTray",   "ui.startWithWindows", "ui.notifications",     "ui.firstRunComplete", "ui.language",
                                        "retention.rawTextDays", "retention.metadataDays", "updates.channel", "updates.autoCompatible", "updates.manifestUrl", "support.requestUrl", "license.key", "license.portalUrl", "ai.storeLocally",
                                        "diagnostics.verbose", "engine.keepAwake", "backup.scheduleDays", "backup.directory", "license.status",
                                        "mt5.autoLaunch", "mt5.dataPath", "mt5.exePath", "mt5.chartSymbol", "telegram.apiId", "telegram.apiHash", "telegram.phone"};

std::string redactForSupport(std::string text) {
    // Keep format (labels, numbers, layout); remove identifying handles, links, phone numbers, e-mails.
    text = std::regex_replace(text, std::regex(R"((https?://|t\.me/|www\.)\S+)"), "<link>");
    text = std::regex_replace(text, std::regex(R"(@[A-Za-z0-9_]{3,})"), "@<handle>");
    text = std::regex_replace(text, std::regex(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})"), "<email>");
    text = std::regex_replace(text, std::regex(R"(\+?\d[\d\s\-()]{9,}\d)"), "<phone>");
    return text;
}

}  // namespace

// connection

void Engine::onUiConnection(std::shared_ptr<ipc::PipeConnection> c) {
    int stage = 0;
    std::string nonceU, nonceE;
    c->readLoop([&](std::string&& frame) {
        auto env = ipc::parseEnvelope(frame);
        if (!env) return c->close();
        if (stage == 0) {
            if (env->type != ipc::msg::HELLO) return c->close();
            nonceU = env->payload.value("nonce", "");
            if (nonceU.size() != 32) return c->close();
            nonceE = crypto::toHex(crypto::randomBytes(16));
            ipc::Envelope ch;
            ch.type = ipc::msg::CHALLENGE;
            ch.payload = {{"nonce", nonceE}, {"proof", crypto::toHex(crypto::hmacSha256(*uiSecret_, "engine|" + nonceU + "|" + nonceE))}};
            c->send(ch.serialize());
            stage = 1;
            return;
        }
        if (stage == 1) {
            auto expect = crypto::hmacSha256(*uiSecret_, "ui|" + nonceE + "|" + nonceU);
            auto proof = crypto::fromHex(env->payload.value("proof", ""));
            if (env->type != ipc::msg::AUTH || !proof || !crypto::constantTimeEqual(*proof, expect)) return c->close();
            stage = 2;
            ipc::Envelope w;
            w.type = ipc::msg::WELCOME;
            w.payload = {{"engineVersion", kProductVersion}};
            c->send(w.serialize());
            {
                std::lock_guard lock(uiMu_);
                uiClients_.push_back(c);
            }
            post([this] { lastSnapshot_.clear(); });  // force a full snapshot to the new client
            return;
        }
        if (env->type != ipc::msg::REQUEST) return;
        const std::string id = env->id;
        const std::string method = env->payload.value("method", "");
        const json params = env->payload.value("params", json::object());
        auto respond = [this, c, id, method, params] {
            ipc::Envelope r;
            r.type = ipc::msg::RESPONSE;
            r.id = id;
            try {
                r.payload = {{"ok", true}, {"result", handleUiRequest(method, params)}};
            } catch (const std::exception& e) {
                r.payload = {{"ok", false}, {"error", e.what()}};
            }
            std::string body = r.serialize();
            if (body.size() > ipc::kMaxFrame - 1024) {
                // Frames are capped at 64 KiB; ask the UI to narrow the query instead of silently truncating.
                r.payload = {{"ok", false}, {"error", "Response too large for one frame; narrow the query (smaller page or date range)"}};
                body = r.serialize();
            }
            c->send(body, 5000);
        };
        static const std::set<std::string> slow{"data.backup", "data.importPreview", "data.exportReport", "ai.test", "mt5.installEA", "mt5.launch", "mt5.detectTerminals", "parser.requestCustom", "license.validate", "telegram.deleteSession", "parser.test",
                                                 "history.page", "history.count", "history.detail"};
        if (slow.count(method)) std::thread(respond).detach();
        else respond();
    });
    std::lock_guard lock(uiMu_);
    uiClients_.erase(std::remove(uiClients_.begin(), uiClients_.end(), c), uiClients_.end());
}

void Engine::broadcast(const std::string& topic, const json& data) {
    ipc::Envelope e;
    e.type = ipc::msg::EVENT;
    e.payload = {{"topic", topic}, {"data", data}};
    const std::string s = e.serialize();
    std::vector<std::shared_ptr<ipc::PipeConnection>> clients;
    {
        std::lock_guard lock(uiMu_);
        clients = uiClients_;
    }
    for (auto& c : clients)
        if (c->open()) c->send(s, 250);
}

void Engine::publishSnapshot() {
    bool any;
    {
        std::lock_guard lock(uiMu_);
        any = !uiClients_.empty();
    }
    if (!any) return;
    json snap = snapshot();
    std::string s = snap.dump();
    while (s.size() > ipc::kMaxFrame - 2048 && !snap["feed"].empty()) {  // stay inside one 64 KiB frame
        snap["feed"].erase(snap["feed"].size() - 1);
        s = snap.dump();
    }
    if (s == lastSnapshot_) return;  // repaint only on change; UI never redraws the whole window per signal
    lastSnapshot_ = std::move(s);
    broadcast("snapshot", snap);
}

json Engine::snapshot() {
    const std::int64_t now = clock_.nowMs();
    json feed = json::array();
    // Unique keys, newest first, capped to keep the UI subscription bounded.
    std::set<std::string> seen;
    for (auto it = feedOrder_.rbegin(); it != feedOrder_.rend() && feed.size() < 40; ++it) {
        if (!seen.insert(*it).second) continue;
        auto f = feed_.find(*it);
        if (f == feed_.end()) continue;
        json item = f->second;
        item.erase("timeline");  // detail views fetch the timeline on demand
        item.erase("aiDetail");
        item.erase("fills");
        if (item.value("reason", "").size() > 200) item["reason"] = item.value("reason", "").substr(0, 200) + "...";
        feed.push_back(item);
    }
    while (feedOrder_.size() > 600) {
        const std::string k = feedOrder_.front();
        feedOrder_.pop_front();
        if (std::find(feedOrder_.begin(), feedOrder_.end(), k) == feedOrder_.end()) feed_.erase(k);
    }
    json errors = json::array();
    for (auto& e : errors_) {
        errors.push_back(e);
        if (errors.size() >= 20) break;
    }
    json account = account_ ? toJson(*account_) : json(nullptr);
    ProfitFilter today;
    today.fromMs = startOfDayMs(now, policy_.account.lossResetUtcOffsetMinutes);
    ProfitFilter week = today;
    week.fromMs = today.fromMs - 6 * 86400000LL;
    ProfitFilter wholeToday = today;
    wholeToday.copierOnly = false;
    json staleQuotes = json::array();
    for (auto& [sym, q] : quotes_)
        if (now - q.receivedAtMs > policy_.quoteMaxAgeMs) staleQuotes.push_back(sym);
    ULARGE_INTEGER avail{};
    GetDiskFreeSpaceExW(dataDir_.c_str(), &avail, nullptr, nullptr);
    json stats = store_->stats();
    stats["waiting"] = waiting_.size();
    stats["inflight"] = inflight_.size();
    stats["gates"] = gates_.size();
    return {{"version", kProductVersion},
            {"at", now},
            {"mode", toString(policy_.executionMode)},
            {"paused", paused_},
            {"userPaused", userPaused_},
            {"reconciled", reconciled_},
            {"overload", overload_.load()},
            {"dispatchBlockedReason", canDispatchReason()},
            {"configVersion", policy_.configVersion},
            {"telegram",
             {{"state", toString(telegram_->authState())},
              {"user", telegram_->userDisplayName()},
              {"error", telegram_->lastError()},
              {"library", telegram_->libraryPath()},
              {"hasCredentials", telegram_->hasCredentials()},
              {"floodWaitUntil", telegram_->floodWaitUntilMs()},
              {"sources", policy_.sources.size()}}},
            {"bridge",
             {{"connected", bridge_ != nullptr},
              {"paired", bridgePaired_},
              {"healthy", bridgeHealthy_},
              {"status", bridgeStatusText_},
              {"heartbeatAgoMs", bridge_ ? now - lastHeartbeatMs_ : -1},
              {"simulated", bridgeHello_.value("simulated", false)},
              {"implVersion", bridgeHello_.value("implVersion", "")},
              {"pairingPending", pairingPending_ ? toJson(*pairingPending_) : json(nullptr)},
              {"upgrading", bridgeUpgrading_},
              {"symbols", symbols_.size()}}},
            {"terminal", {{"status", terminalStatus()}, {"autoLaunch", store_->getSetting("mt5.autoLaunch").value_or("false") == "true"}}},
            {"boundAccount", boundAccount_ ? toJson(*boundAccount_) : json(nullptr)},
            {"account", account},
            {"feed", feed},
            {"errors", errors},
            {"metrics", {{"parseUs", parseUs_.summary()}, {"decisionMs", decisionMs_.summary()}, {"journalMs", journalMs_.summary()}, {"journalWaitMs", journalWaitMs_.summary()}, {"journalExecMs", journalExecMs_.summary()}, {"journalBodyMs", journalBodyMs_.summary()}, {"journalFlushMs", journalFlushMs_.summary()}, {"e2eMs", e2eMs_.summary()}, {"brokerMs", brokerMs_.summary()}}},
            {"counts", stats},
            {"profit",
             {{"currency", account_ ? account_->currency : ""},
              {"balance", account_ ? account_->balance : 0.0},
              {"equity", account_ ? account_->equity : 0.0},
              {"freeMargin", account_ ? account_->freeMargin : 0.0},
              {"floating", account_ ? account_->equity - account_->balance : 0.0},
              {"copierOpenLots", account_ ? account_->copierOpenLots : 0.0},
              {"copierOpenPositions", account_ ? account_->copierOpenPositions : 0},
              {"copierToday", summaryJson(ledger_.summarize(today))},
              {"copierWeek", summaryJson(ledger_.summarize(week))},
              {"accountToday", summaryJson(ledger_.summarize(wholeToday))},
              {"fresh", account_ && bridgeHealthy_ && now - account_->updatedAtMs < 5000},
              {"updatedAtMs", account_ ? account_->updatedAtMs : 0}}},
            {"health",
             {{"diskFreeMb", avail.QuadPart / (1024 * 1024)},
              {"journalHealthy", store_->healthy()},
              {"journalError", store_->lastWriteError()},
              {"staleQuotes", staleQuotes},
              {"terminalTradeAllowed", account_ ? account_->tradeAllowed : false}}},
            {"updates", updater_ ? updater_->status() : json::object()}};
}

// routing

json Engine::handleUiRequest(const std::string& method, const json& params) {
    // Read-only queries run on the connection thread (the store is thread-safe).
    if (method == "history.count" || method == "history.page") {
        db::HistoryFilter f;
        f.text = params.value("text", "");
        f.state = params.value("state", "");
        f.chatId = jsonInt64(params.value("chatId", json("0")));
        f.fromMs = params.value("fromMs", static_cast<std::int64_t>(0));
        f.toMs = params.value("toMs", static_cast<std::int64_t>(0));
        if (method == "history.count") return {{"count", store_->historyCount(f)}};
        return store_->historyPage(f, params.value("offset", static_cast<std::int64_t>(0)), std::clamp(params.value("limit", 100), 1, 100));
    }
    if (method == "history.detail")
        return store_->signalDetail(jsonInt64(params.value("chatId", json("0"))), jsonInt64(params.value("messageId", json("0"))), params.value("revision", 0));
    if (method == "profit.curve") {
        const std::int64_t from = params.value("fromMs", static_cast<std::int64_t>(0));
        json curve = store_->equityCurve(from, 400);
        ProfitFilter f;
        f.fromMs = from;
        f.symbol = params.value("symbol", "");
        f.sourceChatId = jsonInt64(params.value("chatId", json("0")));
        json realized = json::array();
        auto pts = callSync([&] { return ledger_.cumulativeRealized(f, 400); });
        for (auto& p : pts) realized.push_back({p.timeMs, p.value});
        curve["realized"] = realized;
        return curve;
    }
    if (method == "updates.history") return store_->updateHistory(std::clamp(params.value("limit", 20), 1, 20));
    if (method == "updates.markShown") {
        store_->markUpdateNotesShown(params.value("id", static_cast<std::int64_t>(0)));
        return {{"ok", true}};
    }
    if (method == "data.exportReport") {
        auto st = db::exportReport(*store_, platform::widen(params.value("path", "")), params.value("csv", true));
        if (!st) fail(st.error().message);
        return {{"ok", true}};
    }
    if (method == "data.importPreview") {
        auto pv = db::stageImport(platform::widen(params.value("path", "")), params.value("password", ""), platform::ensureSubdir(L"staging\\import"));
        if (!pv) fail(pv.error().message);
        json out = {{"manifest", pv.value().manifest}, {"counts", pv.value().counts}, {"integrity", pv.value().integrity}, {"hasConfig", !pv.value().configJson.is_null()},
                    {"hasDatabase", !pv.value().stagedDbPath.empty()}};
        callSync([&] {
            stagedImport_ = pv.value();
            return 0;
        });
        return out;
    }
    if (method == "ai.test") {
        // Sends a synthetic example only (no user data) to verify the endpoint and schema.
        Signal s;
        s.symbol = "XAUUSD";
        s.side = Side::Buy;
        s.entry = PriceRange{4295, 4300};
        s.stopLoss = 4287;
        s.targets = {{1, 4305, {}}, {2, 4310, {}}};
        const std::string reqId = newUuid();
        const std::string endpoint = callSync([this] { return policy_.ai.endpoint; });
        auto r = httpRequest("POST", endpoint, buildAiRequest(s, reqId).dump(), "application/json", 3000, 64 * 1024);
        if (r.status != 200) return {{"verdict", "Unavailable"}, {"error", r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error}, {"latencyMs", r.elapsedMs}};
        auto a = parseAiResponse(r.body, reqId);
        return {{"verdict", toString(a.verdict)}, {"reasons", a.reasons}, {"modelVersion", a.modelVersion}, {"score", a.score ? json(*a.score) : json(nullptr)},
                {"latencyMs", r.elapsedMs}, {"error", a.unavailableReason}};
    }
    if (method == "parser.test") {
        auto t = ParserTemplate::fromJson(params.value("template", json::object()));
        if (!t) fail(t.error().message);
        auto c = CompiledTemplate::compile(t.value());
        if (!c) fail(c.error().message);
        std::vector<Fixture> fixtures;
        for (auto& f : params.value("fixtures", json::array())) fixtures.push_back(Fixture::fromJson(f));
        const std::size_t own = fixtures.size();
        if (params.value("includeNegativeCorpus", true))
            for (auto& n : sharedNegativeCorpus()) fixtures.push_back(n);
        TemplateSignalParser parser(c.value());
        auto suite = runFixtures(parser, fixtures);
        json j = suite.toJson();
        j["ownFixtures"] = own;
        return j;
    }
    if (method == "parser.learn") {
        auto t = ParserTemplate::fromJson(params.value("template", json::object()));
        if (!t) fail(t.error().message);
        std::vector<HighlightLabel> labels;
        for (auto& l : params.value("labels", json::array())) labels.push_back({l.value("field", ""), l.value("begin", 0u), l.value("end", 0u), l.value("canonical", "")});
        auto r = learnFromHighlights(t.value(), params.value("sample", ""), labels);
        return {{"template", r.updated.toJson()}, {"learned", r.learned}, {"warnings", r.warnings}};
    }
    if (method == "license.validate") {
        // Check a license key against the backend (GET /licenses/validate). Valid only if the backend accepts it.
        const std::string key = params.value("key", "");
        if (key.empty()) return {{"valid", false}, {"error", "Enter a license key"}};
        const std::string endpoint = callSync([this] { return policy_.ai.endpoint; });
        auto pos = endpoint.find("/ai/suggest");
        const std::string url = pos != std::string::npos ? endpoint.substr(0, pos) + "/licenses/validate" : endpoint + "/licenses/validate";
        auto r = httpRequest("GET", url, "", "", 8000, 64 * 1024, {{"X-License-Key", key}});
        if (r.status == 200) {
            json info = json::parse(r.body, nullptr, false);
            return {{"valid", true}, {"info", info.is_discarded() ? json::object() : info}};
        }
        if (r.status == 401) return {{"valid", false}, {"error", "License key is invalid or expired"}};
        return {{"valid", false}, {"error", r.error.empty() ? "Backend returned HTTP " + std::to_string(r.status) : r.error}};
    }
    if (method == "parser.requestCustom") {
        // redacted parser request for one channel. always saved locally, POSTed if a URL is set.
        // the parser comes back later through the updater and hot-reloads.
        db::HistoryFilter f;
        f.chatId = jsonInt64(params.value("chatId", json("0")));
        json samples = json::array();
        for (auto& r : store_->historyPage(f, 0, 15)) {
            std::string prev = r.value("preview", "");
            if (!prev.empty()) samples.push_back(redactForSupport(prev));
        }
        for (auto& s : params.value("extraSamples", json::array()))
            if (s.is_string()) samples.push_back(redactForSupport(s.get<std::string>()));
        json req = {{"requestId", newUuid()},
                    {"app", kProductVersion},
                    {"createdAtMs", clock_.nowMs()},
                    {"chatId", params.value("chatId", "")},
                    {"channelTitle", params.value("title", "")},
                    {"note", redactForSupport(params.value("note", ""))},
                    {"samples", samples}};
        const std::wstring path = platform::ensureSubdir(L"requests") + L"\\" + platform::widen(req["requestId"].get<std::string>()) + L".json";
        platform::writeFileAtomic(path, req.dump(2));
        json out = {{"requestId", req["requestId"]}, {"saved", platform::narrow(path)}, {"sampleCount", samples.size()}, {"submitted", false}};
        std::string url = store_->getSetting("support.requestUrl").value_or("");
        if (url.empty()) {
            // Default to the same backend host as the AI endpoint so the request reaches the backend without
            // extra configuration: .../api/v1/ai/suggest -> .../api/v1/support/custom-parser-requests.
            const std::string endpoint = callSync([this] { return policy_.ai.endpoint; });
            auto pos = endpoint.find("/ai/suggest");
            if (pos != std::string::npos) url = endpoint.substr(0, pos) + "/support/custom-parser-requests";
        }
        if (!url.empty()) {
            const std::string key = store_->getSetting("license.key").value_or("");
            auto resp = httpRequest("POST", url, req.dump(), "application/json", 8000, 8 * 1024 * 1024, {{"X-License-Key", key}});
            out["submitted"] = resp.status >= 200 && resp.status < 300;
            out["status"] = resp.status;
            if (!resp.error.empty()) out["error"] = resp.error;
        }
        return out;
    }
    if (method == "parser.supportPackage") {
        json samples = json::array();
        for (auto& s : params.value("samples", json::array()))
            if (s.is_string()) samples.push_back(redactForSupport(s.get<std::string>()));
        return {{"samples", samples},
                {"template", params.value("template", json::object())},
                {"notice",
                 "Only these redacted samples are included. SignalRelay does not upload or store support requests; the channel you choose has its own retention policy, which is not "
                 "covered by the AI backend's no-retention promise."}};
    }
    if (method == "data.backup") {
        json cfg = callSync([this] {
            json c = {{"schemaVersion", kConfigSchemaVersion}, {"policy", policy_.toJson()}};
            json parsers = json::array();
            for (auto& v : parsers_.list())
                if (v.origin == "local" && !v.templateJson.is_null()) parsers.push_back({{"template", v.templateJson}, {"active", v.active}, {"fixtures", [&] {
                                                                                             json fx = json::array();
                                                                                             for (auto& f : store_->loadFixtures(v.parserId)) fx.push_back(f.toJson());
                                                                                             return fx;
                                                                                         }()}});
            c["parsers"] = parsers;
            return c;
        });
        const auto scope = params.value("scope", "full") == "configuration" ? db::ExportScope::ConfigurationOnly : db::ExportScope::FullHistory;
        auto r = db::createBackup(*store_, platform::widen(params.value("path", "")), scope, params.value("password", ""), cfg);
        if (!r) fail(r.error().message);
        return {{"bytes", r.value().bytes}, {"manifest", r.value().manifest}};
    }
    if (method == "mt5.installEA") {
        try {
            json r = mt5::installEa(fs::path(installDir_), platform::widen(params.value("dataPath", "")));
            r["next"] = "Use Start MetaTrader to open the terminal with the EA attached, or attach Experts\\SignalRelay\\SignalRelayEA to one chart yourself.";
            return r;
        } catch (const std::runtime_error& e) {
            fail(e.what());
        }
    }
    if (method == "mt5.launch") {
        // Remembers the chosen terminal, then installs the EA and starts MetaTrader once (never restarts it).
        const std::string dataPath = params.value("dataPath", store_->getSetting("mt5.dataPath").value_or(""));
        const std::string exePath = params.value("exePath", store_->getSetting("mt5.exePath").value_or(""));
        const std::string symbol = params.value("chartSymbol", store_->getSetting("mt5.chartSymbol").value_or("EURUSD"));
        store_->putSetting("mt5.dataPath", dataPath);
        store_->putSetting("mt5.exePath", exePath);
        store_->putSetting("mt5.chartSymbol", symbol);
        return launchTerminal(dataPath, exePath, symbol);
    }
    if (method == "telegram.deleteSession") {
        std::string err;
        if (!telegram_->deleteSession(err)) fail(err);
        return {{"ok", true}};
    }
    return callSync([&] { return handleOnEngineThread(method, params); });
}

json Engine::parserTemplateJson(const std::string& id, const std::string& version) {
    for (auto& v : parsers_.list())
        if (v.parserId == id && (version.empty() ? v.active : v.version == version)) return v.templateJson;
    return nullptr;
}

json Engine::handleOnEngineThread(const std::string& method, const json& params) {
    const std::int64_t now = clock_.nowMs();
    if (method == "getSnapshot") return snapshot();

    if (method == "source.testParse") {
        // Read-only: run a sample message through a channel's active parser so setup can be checked in the
        // wizard. Never dispatches or records anything.
        std::int64_t chatId = 0;
        try {
            chatId = std::stoll(params.value("chatId", "0"));
        } catch (...) {
            chatId = 0;
        }
        const std::string text = params.value("text", "");
        const SourceBinding* b = policy_.findSource(chatId);
        if (!b) return {{"parsed", false}, {"message", "This channel is not a saved source yet."}};
        auto lease = parserFor(*b);
        if (!lease) return {{"parsed", false}, {"message", "No active parser for this channel yet - request one on the previous step."}};
        ParseContext ctx{{chatId, 0}, 0, now, now, false};
        Signal s = lease->parser->parse(text, ctx);
        return {{"parsed", true}, {"parser", lease->parserId + "@" + lease->version}, {"signal", toJson(s)}};
    }

    // engine control
    if (method == "engine.setMode") {
        const std::string mode = params.value("mode", "preview");
        ExecutionPolicy p = policy_;
        if (mode == "automatic") {
            if (!boundAccount_) fail("Pair an MT5 account before enabling Automatic mode");
            if (params.value("confirmLogin", "") != std::to_string(boundAccount_->login)) fail("Type the destination account number to confirm Automatic mode");
            p.executionMode = ExecutionMode::Automatic;
        } else {
            p.executionMode = ExecutionMode::Preview;
        }
        if (auto st = savePolicy(p, true); !st) fail(st.error().message);
        notify("info", std::string("Execution mode: ") + toString(p.executionMode));
        return {{"mode", toString(p.executionMode)}};
    }
    if (method == "engine.pause") {
        userPaused_ = true;
        paused_ = true;
        store_->putSetting("engine.userPaused", "true");
        sendToBridge(ipc::msg::PAUSE, {{"paused", true}});
        reevaluateWaiting();  // rejects unsubmitted waiting entries; submitted outcomes still reconcile
        return {{"paused", true}, {"note", "Already-submitted requests may still fill. Existing positions are untouched."}};
    }
    if (method == "engine.resume") {
        userPaused_ = false;
        store_->putSetting("engine.userPaused", "false");
        sendToBridge(ipc::msg::PAUSE, {{"paused", false}});
        if (reconciled_ && bridgePaired_) paused_ = false;
        overload_ = false;
        return {{"paused", paused_}, {"note", paused_ ? "Copying resumes after the terminal is paired and reconciled" : "Copying resumed"}};
    }
    if (method == "engine.shutdown") {
        std::thread([this] {
            Sleep(200);
            requestStop();
        }).detach();
        return {{"ok", true}};
    }

    // Telegram
    if (method == "telegram.setCredentials") {
        std::string err;
        const std::int64_t apiId = jsonInt64(params.value("apiId", json("0")));
        const std::string apiHash = params.value("apiHash", "");
        if (!telegram_->setCredentials(apiId, apiHash, err)) fail(err);
        // Stored (local DB) so the UI can show what the user entered. The authoritative copy is DPAPI-encrypted.
        store_->putSetting("telegram.apiId", std::to_string(apiId));
        store_->putSetting("telegram.apiHash", apiHash);
        return {{"ok", true}};
    }
    if (method == "telegram.phone") {
        const std::string phone = params.value("phone", "");
        telegram_->submitPhone(phone);
        store_->putSetting("telegram.phone", phone);  // remembered so it shows again; the login code is never stored
        return {{"ok", true}};
    }
    if (method == "telegram.code") {
        telegram_->submitCode(params.value("code", ""));
        return {{"ok", true}};
    }
    if (method == "telegram.password") {
        telegram_->submitPassword(params.value("password", ""));
        return {{"ok", true}};
    }
    if (method == "telegram.logout") {
        telegram_->logout();
        return {{"ok", true}};
    }
    if (method == "telegram.chats") {
        json arr = json::array();
        for (auto& c : telegram_->chats()) {
            const SourceBinding* b = policy_.findSource(c.chatId);
            arr.push_back({{"chatId", std::to_string(c.chatId)}, {"title", c.title}, {"type", c.type}, {"selected", b != nullptr}});
        }
        return arr;
    }

    // sources
    if (method == "sources.list") {
        json arr = json::array();
        for (auto& s : policy_.sources) {
            json j = s.toJson();
            auto lease = parserFor(s);
            j["activeParserVersion"] = lease ? lease->version : "";
            arr.push_back(j);
        }
        return arr;
    }
    if (method == "sources.save") {
        ExecutionPolicy p = policy_;
        p.sources.clear();
        for (auto& sj : params.value("sources", json::array())) {
            auto b = SourceBinding::fromJson(sj);
            if (!b) fail(b.error().message);
            p.sources.push_back(b.value());
        }
        if (auto st = savePolicy(p, true); !st) fail(st.error().message);
        return {{"configVersion", policy_.configVersion}};
    }
    if (method == "sources.bulkApply") {
        // Applies only the provided fields; per-source sizing/target overrides are never overwritten.
        std::set<std::int64_t> ids;
        for (auto& id : params.value("chatIds", json::array())) ids.insert(jsonInt64(id));
        ExecutionPolicy p = policy_;
        for (auto& s : p.sources) {
            if (!ids.count(s.chatId)) continue;
            if (params.contains("template")) {
                s.parserId = params["template"].get<std::string>();
                s.parserVersion.clear();
            }
            if (params.contains("entryMode")) {
                auto em = entryModeFromString(params["entryMode"].get<std::string>());
                if (!em) fail("Unknown entry mode");
                s.entryMode = *em;
            }
            if (params.contains("aiMode")) {
                auto ai = aiModeFromString(params["aiMode"].get<std::string>());
                if (!ai) fail("Unknown AI mode");
                s.aiMode = *ai;
            }
            if (params.contains("enabled")) s.enabled = params["enabled"].get<bool>();
        }
        if (auto st = savePolicy(p, true); !st) fail(st.error().message);
        return {{"updated", ids.size()}};
    }

    // parsers
    if (method == "parser.list") {
        json arr = json::array();
        for (auto& v : parsers_.list()) {
            arr.push_back({{"id", v.parserId},
                           {"version", v.version},
                           {"origin", v.origin},
                           {"active", v.active},
                           {"createdAtMs", v.createdAtMs},
                           {"activatedAtMs", v.activatedAtMs},
                           {"name", v.templateJson.is_null() ? v.parserId + " (native module)" : v.templateJson.value("name", v.parserId)},
                           {"native", v.templateJson.is_null()},
                           {"fixtures", store_->loadFixtures(v.parserId).size()},
                           {"history", parsers_.activationHistory(v.parserId)}});
        }
        return arr;
    }
    if (method == "parser.get") {
        json t = parserTemplateJson(params.value("id", ""), params.value("version", ""));
        json fx = json::array();
        for (auto& f : store_->loadFixtures(params.value("id", ""))) fx.push_back(f.toJson());
        return {{"template", t}, {"fixtures", fx}};
    }
    if (method == "parser.starter") {
        const std::string which = params.value("starter", "gold");
        ParserTemplate t = which == "forex" ? starterForexTemplate() : which == "strict" ? starterStrictTemplate() : starterGoldTemplate();
        for (auto& preset : params.value("presets", json::array())) {
            const std::string p = preset.get<std::string>();
            if (p == "arabic") applyArabicPreset(t);
            if (p == "russian") applyRussianPreset(t);
            if (p == "spanish") applySpanishPreset(t);
        }
        return t.toJson();
    }
    if (method == "parser.sandbox") {
        std::shared_ptr<const ISignalParser> parser;
        if (params.contains("template") && params["template"].is_object()) {
            auto t = ParserTemplate::fromJson(params["template"]);
            if (!t) fail(t.error().message);
            auto c = CompiledTemplate::compile(t.value());
            if (!c) fail(c.error().message);
            parser = std::make_shared<TemplateSignalParser>(c.value());
        } else {
            // no id: same rule as a channel without its own parser (the single installed one)
            const std::string id = params.value("id", std::string());
            auto g = id.empty() ? parserFor(SourceBinding{}) : parsers_.lease(id);
            if (!g) fail("Parser is not active");
            parser = g->parser;
        }
        ParseContext ctx;
        ctx.source = {jsonInt64(params.value("chatId", json("0"))), 0};
        ctx.publishedAtMs = now;
        ctx.receivedAtMs = now;
        Signal s = parser->parse(params.value("text", ""), ctx);
        json out = {{"signal", toJson(s)}, {"outcome", outcomeOf(s)}};
        // Show what would be sent, never submitting anything: evaluate in Preview semantics.
        SourceBinding tmp;
        const SourceBinding* b = policy_.findSource(ctx.source.chatId);
        if (!b) {
            tmp.chatId = 1;
            tmp.title = "Sandbox";
            b = &tmp;
        }
        ExecutionPolicy saved = policy_;
        policy_.executionMode = ExecutionMode::Preview;
        Decision d = evaluate(s, *b, false);
        policy_ = saved;
        out["preview"] = toJson(d);
        return out;
    }
    if (method == "parser.saveFixtures") {
        std::vector<Fixture> fx;
        for (auto& f : params.value("fixtures", json::array())) fx.push_back(Fixture::fromJson(f));
        if (auto st = store_->saveFixtures(params.value("id", ""), fx); !st) fail(st.error().message);
        return {{"saved", fx.size()}};
    }
    if (method == "parser.publish") {
        auto t = ParserTemplate::fromJson(params.value("template", json::object()));
        if (!t) fail(t.error().message);
        std::vector<Fixture> fx;
        for (auto& f : params.value("fixtures", json::array())) fx.push_back(Fixture::fromJson(f));
        // A new version must pass its fixture suite and the shared negative corpus before it can be published.
        auto c = CompiledTemplate::compile(t.value());
        if (!c) fail(c.error().message);
        TemplateSignalParser tp(c.value());
        std::vector<Fixture> all = fx;
        for (auto& n : sharedNegativeCorpus()) all.push_back(n);
        auto suite = runFixtures(tp, all);
        if (!suite.allPassed()) fail(std::to_string(suite.failed) + " fixture(s) fail; fix them before publishing");
        auto info = parsers_.publish(t.value(), "local", now);
        if (!info) fail(info.error().message);
        store_->saveParserVersion(info.value());
        store_->saveFixtures(t.value().id, fx);
        return {{"id", info.value().parserId}, {"version", info.value().version}, {"suite", suite.toJson()}};
    }
    if (method == "parser.activate") {
        if (auto st = activateParserVersion(params.value("id", ""), params.value("version", ""), {{"releaseId", "local"}}, true); !st) fail(st.error().message);
        return {{"ok", true}};
    }
    if (method == "parser.rollback") {
        const std::string id = params.value("id", "");
        auto history = parsers_.activationHistory(id);
        auto r = parsers_.rollback(id, now);
        if (!r) fail(r.error().message);
        for (auto& info : parsers_.list())
            if (info.parserId == id && info.version == r.value()) store_->saveParserVersion(info);
        store_->addUpdateHistory("local", id, history.empty() ? "" : history.back(), r.value(), "rolled-back", {{"rolledBackAtMs", now}}, false);
        notify("info", "Parser " + id + " rolled back to " + r.value());
        return {{"version", r.value()}};
    }
    if (method == "parser.compare") {
        auto a = parsers_.leaseVersion(params.value("id", ""), params.value("from", ""));
        auto b = parsers_.leaseVersion(params.value("id", ""), params.value("to", ""));
        if (!a || !b) fail("Both versions must be published");
        std::vector<std::string> samples;
        for (auto& s : params.value("samples", json::array()))
            if (s.is_string()) samples.push_back(s.get<std::string>());
        json arr = json::array();
        for (auto& d : compareParsers(*a->parser, *b->parser, samples)) arr.push_back({{"sample", d.sample}, {"before", d.before}, {"after", d.after}, {"changed", d.changedFields}});
        return arr;
    }
    if (method == "parser.import") {
        auto t = ParserTemplate::fromJson(params.value("template", json::object()));
        if (!t) fail(t.error().message);
        auto info = parsers_.publish(t.value(), "local", now);
        if (!info) fail(info.error().message + " (use Duplicate as new parser to keep both)");
        store_->saveParserVersion(info.value());
        return {{"id", info.value().parserId}, {"version", info.value().version}};
    }

    // policy
    if (method == "policy.get") return policy_.toJson();
    if (method == "policy.save") {
        auto p = ExecutionPolicy::fromJson(params.value("policy", json::object()));
        if (!p) fail(p.error().message);
        ExecutionPolicy next = p.value();
        if (!params["policy"].contains("sources")) next.sources = policy_.sources;
        next.executionMode = policy_.executionMode;  // mode changes only through the explicit Automatic toggle
        if (auto st = savePolicy(next, true); !st) fail(st.error().message);
        return {{"configVersion", policy_.configVersion}};
    }

    // MetaTrader
    if (method == "mt5.status") {
        json syms = json::array();
        for (auto& [n, s] : symbols_) {
            json j = toJson(s);
            if (auto q = quotes_.find(n); q != quotes_.end()) j["quote"] = toJson(q->second);
            syms.push_back(j);
        }
        return {{"symbols", syms}, {"catalog", symbolCatalog_}, {"bridgeHello", bridgeHello_}, {"activeImpl", plugins_->activeBridgeImplementation().value_or(json(nullptr))}};
    }
    if (method == "mt5.approvePairing") {
        if (!pairingPending_) fail("No pairing request is pending");
        if (params.value("confirmLogin", "") != std::to_string(pairingPending_->login)) fail("Type the account number shown to approve pairing");
        if (auto st = store_->saveAccountBinding(*pairingPending_); !st) fail(st.error().message);
        boundAccount_ = *pairingPending_;
        pairingPending_.reset();
        bridgePaired_ = bridge_ != nullptr;
        bridgeStatusText_ = "Paired";
        paused_ = true;  // stays paused until reconciliation completes
        if (bridge_) {
            ipc::Envelope w;
            w.type = ipc::msg::WELCOME;
            w.payload = {{"paired", true}, {"engineVersion", kProductVersion}};
            bridge_->send(w.serialize());
            requestReconciliation();
        }
        return {{"bound", toJson(*boundAccount_)}};
    }
    if (method == "mt5.unbind") {
        store_->clearAccountBindings();
        boundAccount_.reset();
        bridgePaired_ = false;
        paused_ = true;
        if (bridge_) {
            AccountKey k{bridgeHello_.value("terminalId", ""), jsonInt64(bridgeHello_.value("login", json("0"))), bridgeHello_.value("server", "")};
            pairingPending_ = k;
        }
        return {{"ok", true}};
    }
    if (method == "mt5.detectTerminals") return mt5::detectTerminals();
    if (method == "mt5.manage") {
        const std::string action = params.value("action", "");
        if (action != "cancel_pending" && action != "close_positions") fail("Unknown action");
        if (!boundAccount_ || params.value("confirmLogin", "") != std::to_string(boundAccount_->login)) fail("Type the bound account number to confirm");
        if (!bridge_) fail("MT5 terminal is not connected");
        sendToBridge(ipc::msg::MANAGE, {{"action", action}, {"magic", kCopierMagic}, {"expectedCount", params.value("expectedCount", 0)}});
        notify("warning", action == "cancel_pending" ? "Canceling copier-owned pending orders" : "Closing copier-owned positions");
        return {{"sent", true}};
    }
    if (method == "mt5.symbolCheck") {
        json arr = json::array();
        std::set<std::string> names;
        for (auto& [k, v] : policy_.symbolMap) names.insert(v);
        for (auto& [k, v] : policy_.symbolLimits) names.insert(k);
        for (auto& n : names) {
            json r = {{"symbol", n}};
            auto s = symbols_.find(n);
            r["available"] = s != symbols_.end();
            auto l = policy_.symbolLimits.find(n);
            r["limitsConfigured"] = l != policy_.symbolLimits.end() && l->second.maxSpreadPrice > 0;
            if (s != symbols_.end() && l != policy_.symbolLimits.end()) {
                std::vector<std::string> warn;
                if (l->second.maxSpreadPrice < s->second.tickSize) warn.push_back("max spread is below one tick");
                if (l->second.maxEntryDeviationPrice > 0 && l->second.maxEntryDeviationPrice < s->second.tickSize) warn.push_back("deviation is below one tick");
                if (l->second.maxLots > 0 && l->second.maxLots < s->second.volumeMin) warn.push_back("max lots is below the broker minimum");
                r["warnings"] = warn;
            }
            arr.push_back(r);
        }
        return arr;
    }

    // profits
    if (method == "profit.summary") {
        ProfitFilter f;
        f.fromMs = params.value("fromMs", startOfDayMs(now, policy_.account.lossResetUtcOffsetMinutes));
        f.toMs = params.value("toMs", INT64_MAX);
        f.symbol = params.value("symbol", "");
        f.sourceChatId = jsonInt64(params.value("chatId", json("0")));
        ProfitFilter whole = f;
        whole.copierOnly = false;
        return {{"copier", summaryJson(ledger_.summarize(f))}, {"account", summaryJson(ledger_.summarize(whole))}, {"currency", account_ ? account_->currency : ""}};
    }

    // settings
    if (method == "settings.get") {
        json out = json::object();
        for (auto& k : kUiSettings)
            if (auto v = store_->getSetting(k)) out[k] = *v;
        out["dataDir"] = platform::narrow(dataDir_);
        out["installDir"] = platform::narrow(installDir_);
        return out;
    }
    if (method == "settings.save") {
        const json values = params.value("values", json::object());  // keep alive: items() only references it
        for (auto& [k, v] : values.items()) {
            if (!kUiSettings.count(k)) fail("Unknown setting " + k);
            store_->putSetting(k, v.is_string() ? v.get<std::string>() : v.dump());
        }
        keepAwakeSetting_ = store_->getSetting("engine.keepAwake").value_or("true") == "true";
        licenseKey_ = store_->getSetting("license.key").value_or("");
        return {{"ok", true}, {"appliesNow", true}};
    }

    // data
    if (method == "data.importApply") {
        if (!stagedImport_) fail("Preview an archive first");
        const std::string mode = params.value("mode", "config");
        auto& pv = *stagedImport_;
        // Imports never place orders: admissions are paused and restored records are historical evidence only.
        paused_ = true;
        userPaused_ = true;
        store_->putSetting("engine.userPaused", "true");
        if (mode == "config") {
            if (pv.configJson.is_null()) fail("Archive has no configuration payload");
            int parsersAdded = 0;
            for (auto& pj : pv.configJson.value("parsers", json::array())) {
                auto t = ParserTemplate::fromJson(pj.value("template", json::object()));
                if (!t) continue;
                auto info = parsers_.publish(t.value(), "local", now);
                if (!info) continue;  // locally edited versions are never overwritten
                store_->saveParserVersion(info.value());
                std::vector<Fixture> fx;
                for (auto& f : pj.value("fixtures", json::array())) fx.push_back(Fixture::fromJson(f));
                if (!fx.empty()) store_->saveFixtures(t.value().id, fx);
                ++parsersAdded;
            }
            int sourcesAdded = 0;
            if (pv.configJson.contains("policy")) {
                auto imported = ExecutionPolicy::fromJson(pv.configJson["policy"]);
                if (!imported) fail(imported.error().message);
                ExecutionPolicy p = policy_;
                for (auto& s : imported.value().sources)
                    if (!p.findSource(s.chatId)) {
                        p.sources.push_back(s);
                        ++sourcesAdded;
                    }
                for (auto& [k, v] : imported.value().symbolMap)
                    if (!p.symbolMap.count(k)) p.symbolMap[k] = v;
                p.executionMode = ExecutionMode::Preview;  // restore into Preview
                if (auto st = savePolicy(p, true); !st) fail(st.error().message);
            }
            stagedImport_.reset();
            return {{"parsersAdded", parsersAdded}, {"sourcesAdded", sourcesAdded}, {"note", "Revalidate broker symbol mappings and pair MT5 on this PC"}};
        }
        if (pv.stagedDbPath.empty()) fail("Archive has no database snapshot");
        if (!store_->unresolvedCommands().empty()) fail("Full restore replaces the local profile and is refused while unresolved commands exist");
        const std::wstring pending = dataDir_ + L"\\restore_pending.sqlite";
        if (!CopyFileW(pv.stagedDbPath.c_str(), pending.c_str(), FALSE)) fail("Cannot stage the restore");
        stagedImport_.reset();
        return {{"restartRequired", true},
                {"note", "The full profile is applied on the next start (a rollback backup of the current database is kept). It restores into Preview and requires MT5 "
                         "pairing and reconciliation before Automatic."}};
    }
    if (method == "data.erase") {
        if (auto st = store_->eraseHistory(); !st) fail(st.error().message);
        feed_.clear();
        feedOrder_.clear();
        ledger_ = ProfitLedger{};
        return {{"ok", true}};
    }
    if (method == "data.purge") {
        auto r = store_->purgeRetention(std::stoi(store_->getSetting("retention.rawTextDays").value_or("30")), std::stoi(store_->getSetting("retention.metadataDays").value_or("180")));
        if (!r) fail(r.error().message);
        return r.value();
    }

    // updates
    if (method == "updates.status") return updater_->status();
    if (method == "updates.check") {
        updater_->checkNowAsync();
        return {{"started", true}};
    }

    // simulation & diagnostics
    if (method == "sim.inject") {
        IncomingMessage m;
        m.chatId = jsonInt64(params.value("chatId", json("0")));
        if (!policy_.findSource(m.chatId)) fail("Select an allowlisted source to simulate a message");
        m.messageId = now;  // unique synthetic id
        m.publishedAtMs = now;
        m.receivedAtMs = now;
        m.receivedMonoUs = clock_.monotonicUs();
        m.text = params.value("text", "");
        m.simulated = true;
        processMessage(m);
        return {{"messageId", std::to_string(m.messageId)}};
    }
    if (method == "diagnostics.health") {
        json s = snapshot();
        auto integrity = store_->integrityCheck();
        return {{"health", s["health"]},
                {"integrity", integrity ? integrity.value() : integrity.error().message},
                {"dataDir", platform::narrow(dataDir_)},
                {"telegram", s["telegram"]},
                {"bridge", s["bridge"]},
                {"trustedKeys", plugins_->trustedKeys().size()},
                {"dispatchBlockedReason", canDispatchReason()}};
    }
    fail("Unknown method " + method);
}

}  // namespace sr::engine
