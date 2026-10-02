#include "Engine.h"
#include "TerminalLauncher.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>

#include "Http.h"
#include "SignalRelay.Core/JsonModels.h"
#include "SignalRelay.Core/StateMachine.h"
#include "SignalRelay.Core/Uuid.h"
#include "SignalRelay.Protocol/Crypto.h"
#include "SignalRelay.Protocol/Platform.h"

namespace sr::engine {

using nlohmann::json;

namespace {

std::string signalKey(std::int64_t chatId, std::int64_t messageId) { return std::to_string(chatId) + ":" + std::to_string(messageId); }

int localMinuteOfDay(std::int64_t nowMs) {
    std::time_t t = static_cast<std::time_t>(nowMs / 1000);
    std::tm lt{};
    localtime_s(&lt, &t);
    return lt.tm_hour * 60 + lt.tm_min;
}

std::uint64_t diskFreeMb(const std::wstring& dir) {
    ULARGE_INTEGER avail{};
    if (!GetDiskFreeSpaceExW(dir.c_str(), &avail, nullptr, nullptr)) return 0;
    return avail.QuadPart / (1024 * 1024);
}

}  // namespace

// LatencyStats

void LatencyStats::add(double v) {
    if (!std::isfinite(v) || v < 0) return;
    samples_.push_back(v);
    if (samples_.size() > 2000) samples_.pop_front();
}

json LatencyStats::summary() const {
    if (samples_.empty()) return {{"n", 0}};
    std::vector<double> s(samples_.begin(), samples_.end());
    std::sort(s.begin(), s.end());
    auto pct = [&](double p) { return s[std::min(s.size() - 1, static_cast<std::size_t>(std::ceil(p * static_cast<double>(s.size())) - 1))]; };
    return {{"n", s.size()}, {"p50", pct(0.50)}, {"p95", pct(0.95)}, {"p99", pct(0.99)}, {"max", s.back()}};
}

// lifecycle

Engine::Engine(EngineOptions opt) : opt_(opt), planner_([] { return newUuid(); }) {}

Engine::~Engine() {
    stopping_ = true;
    if (updater_) updater_.reset();
    if (telegram_) telegram_->stop();
    if (uiServer_) uiServer_->stop();
    if (bridgeServer_) bridgeServer_->stop();
    tasks_.close();
    aiQueue_.close();
    if (decisionThread_.joinable()) decisionThread_.join();
    if (aiThread_.joinable()) aiThread_.join();
    if (autoLaunchThread_.joinable()) autoLaunchThread_.join();
}

Status Engine::start() {
    dataDir_ = platform::dataDir();
    installDir_ = platform::exeDir();
    auto st = db::Store::open(platform::narrow(dataDir_ + L"\\signalrelay.db"), clock_);
    if (!st) return st.error();
    store_ = std::move(st.value());
    pairingSecret_ = platform::loadOrCreateSecret(L"pairing");
    uiSecret_ = platform::loadOrCreateSecret(L"ui");
    if (!pairingSecret_ || !uiSecret_) return failStatus("secrets", "Cannot create DPAPI-protected local secrets");
    plugins_ = std::make_unique<PluginManager>(installDir_, dataDir_);
    sessionStartMs_ = clock_.nowMs();
    store_->setWatermark("telegram.liveSessionStart", sessionStartMs_);

    loadConfiguration();
    initParsers();
    installBridgeImplementation();
    for (auto& d : store_->loadDeals(0)) ledger_.addDeal(d);
    boundAccount_ = store_->activeAccountBinding();
    userPaused_ = store_->getSetting("engine.userPaused").value_or("false") == "true";
    keepAwakeSetting_ = store_->getSetting("engine.keepAwake").value_or("true") == "true";
    licenseKey_ = store_->getSetting("license.key").value_or("");
    if (!policy_.ai.localModelPath.empty()) {
        if (auto text = platform::readFileUtf8(platform::widen(policy_.ai.localModelPath), 4 * 1024 * 1024)) {
            auto m = LocalLinearModel::load(*text);
            if (m) localModel_ = m.value();
        }
    }

    decisionThread_ = std::thread([this] { decisionLoop(); });
    aiThread_ = std::thread([this] { aiLoop(); });

    telegram_ = std::make_unique<TelegramClient>(clock_, dataDir_);
    telegram_->setSessionStartMs(sessionStartMs_);
    callSync([this] {
        updateTelegramAllowlist();
        return 0;
    });
    telegram_->start([this](IncomingMessage m) { onIncoming(std::move(m)); });

    bridgeServer_ = std::make_unique<ipc::PipeServer>(platform::pipeName(L"bridge"), 2, [this](std::shared_ptr<ipc::PipeConnection> c) { onBridgeConnection(std::move(c)); });
    uiServer_ = std::make_unique<ipc::PipeServer>(platform::pipeName(L"ui"), 8, [this](std::shared_ptr<ipc::PipeConnection> c) { onUiConnection(std::move(c)); });
    bridgeServer_->start();
    uiServer_->start();

    // Opt-in: start the remembered MetaTrader terminal with the EA once, after the bridge pipe is listening.
    // A terminal that is already running is left alone (its EA reconnects by itself).
    if (store_->getSetting("mt5.autoLaunch").value_or("false") == "true") {
        autoLaunchThread_ = std::thread([this] {
            for (int i = 0; i < 20 && !stopping_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stopping_) return;
            try {
                launchTerminal(store_->getSetting("mt5.dataPath").value_or(""), store_->getSetting("mt5.exePath").value_or(""),
                               store_->getSetting("mt5.chartSymbol").value_or("EURUSD"));
            } catch (const std::exception&) {
                // launchTerminal already recorded the reason for the UI
            }
        });
    }

    UpdaterHooks hooks;
    hooks.activateParser = [this](const std::string& id, const std::string& version, std::shared_ptr<const ISignalParser> parser, const json& notes) -> Status {
        return callSync([&, parser]() -> Status {
            auto reg = parsers_.registerNative(id, version, parser, "package", clock_.nowMs());
            if (!reg && reg.error().code != "version_exists") return reg;
            return activateParserVersion(id, version, notes, false);
        });
    };
    hooks.upgradeBridge = [this](const json& impl) {
        post([this, impl] {
            bridgeUpgrading_ = true;
            notify("info", "Upgrading bridge implementation to " + impl.value("version", "") + "; new dispatch briefly inhibited");
            sendToBridge("BRIDGE_UPGRADE", impl);
        });
    };
    hooks.savedSamples = [this] {
        std::vector<std::string> out;
        for (auto& r : store_->historyPage({}, 0, 50))
            if (!r.value("preview", "").empty()) out.push_back(r.value("preview", ""));
        return out;
    };
    hooks.notify = [this](const std::string& m) { post([this, m] { notify("info", m); }); };
    // Default the update service to the same backend as the AI endpoint, so parsers pushed by the admin are
    // pulled without the user configuring a URL (.../ai/suggest -> .../updates/manifest). User-set value wins.
    if (store_->getSetting("updates.manifestUrl").value_or("").empty()) {
        const std::string ep = policy_.ai.endpoint;
        if (auto pos = ep.find("/ai/suggest"); pos != std::string::npos) store_->putSetting("updates.manifestUrl", ep.substr(0, pos) + "/updates/manifest");
    }
    updater_ = std::make_unique<Updater>(*store_, *plugins_, clock_, hooks);
    updater_->startBackgroundCheck();  // nonblocking; never delays Telegram or execution readiness
    return okStatus();
}

void Engine::waitForStop() { stopPromise_.get_future().wait(); }

void Engine::requestStop() {
    if (!stopRequested_.exchange(true)) stopPromise_.set_value();
}

bool Engine::post(std::function<void()> task) { return tasks_.tryPush(std::move(task)); }

void Engine::decisionLoop() {
    decisionThreadId_ = std::this_thread::get_id();
    std::int64_t lastTick = 0, lastPublish = 0;
    while (!stopping_) {
        if (auto t = tasks_.popFor(std::chrono::milliseconds(40))) {
            try {
                (*t)();
            } catch (const std::exception& e) {
                addError(std::string("internal error: ") + e.what());
            }
        }
        const std::int64_t now = clock_.nowMs();
        if (now - lastTick >= 100) {
            lastTick = now;
            reevaluateWaiting();
            checkHealth();
        }
        if (now - lastPublish >= 150) {  // UI paint rate ~6-7 Hz; execution is never batched
            lastPublish = now;
            publishSnapshot();
        }
        if (account_ && now - lastEquitySnapshotMs_ >= 10000) {
            lastEquitySnapshotMs_ = now;
            store_->saveProfitSnapshot(now, account_->key.str(), account_->balance, account_->equity, account_->equity - account_->balance);
        }
        if (now - lastRetentionMs_ >= 6 * 3600 * 1000LL) {
            lastRetentionMs_ = now;
            const int rawDays = std::stoi(store_->getSetting("retention.rawTextDays").value_or("30"));
            const int metaDays = std::stoi(store_->getSetting("retention.metadataDays").value_or("180"));
            store_->purgeRetention(rawDays, metaDays);
        }
    }
}

// configuration

void Engine::loadConfiguration() {
    if (auto j = store_->latestPolicy()) {
        auto p = ExecutionPolicy::fromJson(*j);
        if (p) {
            policy_ = p.value();
            policy_.executionMode = ExecutionMode::Automatic;  // no Preview/Automatic toggle: always live, gated only by pairing + Pause
            store_->replaceSources(policy_.toJson()["sources"]);
            return;
        }
        addError("Stored configuration is invalid (" + p.error().message + "); defaults loaded");
    }
    policy_ = ExecutionPolicy{};
    policy_.executionMode = ExecutionMode::Automatic;  // live as soon as an MT5 account is paired; Pause stops new trades
    store_->savePolicy(policy_.toJson(), policy_.configVersion);
}

Status Engine::savePolicy(ExecutionPolicy p, bool bumpVersion) {
    if (auto v = p.validate(); !v) return v;
    if (bumpVersion) p.configVersion = policy_.configVersion + 1;
    auto st = store_->savePolicy(p.toJson(), p.configVersion);
    if (!st) return st;
    store_->replaceSources(p.toJson()["sources"]);
    policy_ = std::move(p);
    updateTelegramAllowlist();
    // Changing a rule revalidates waiting candidates immediately under the new configuration version.
    reevaluateWaiting();
    return okStatus();
}

void Engine::updateTelegramAllowlist() {
    std::set<std::int64_t> ids;
    for (auto& s : policy_.sources)
        if (s.enabled) ids.insert(s.chatId);
    if (telegram_) telegram_->setAllowlist(ids);
}

void Engine::initParsers() {
    const auto now = clock_.nowMs();
    // no built-in parsers, they all come from the backend per license.
    // a source without one gets ParseStatus::Unsupported (-> "request a custom parser" in the UI).
    std::vector<ParserVersionInfo> activeLater;
    for (auto& v : store_->loadParserVersions()) {
        if (v.templateJson.is_null()) {
            if (v.active) activeLater.push_back(v);
            continue;
        }
        auto t = ParserTemplate::fromJson(v.templateJson);
        if (!t) continue;
        parsers_.publish(t.value(), v.origin, v.createdAtMs);
        if (v.active) activeLater.push_back(v);
    }
    // Native parser modules: verified statically, fixture-validated in a helper process, then loaded.
    for (auto& manifest : plugins_->discoverParserManifests()) {
        auto verified = plugins_->verify(manifest);
        if (!verified) {
            addError("Parser module rejected: " + verified.error().message + " (" + platform::narrow(manifest) + ")");
            continue;
        }
        auto helper = plugins_->validateInHelper(manifest);
        if (!helper) {
            addError("Parser module " + verified.value().manifest.id + " failed validation: " + helper.error().message);
            continue;
        }
        auto parser = plugins_->loadParser(verified.value());
        if (!parser) {
            addError("Parser module " + verified.value().manifest.id + " could not load: " + parser.error().message);
            continue;
        }
        const auto& m = verified.value().manifest;
        if (parsers_.registerNative(m.id, m.version, parser.value(), "plugin", now)) {
            if (!parsers_.lease(m.id)) parsers_.activate(m.id, m.version, now);
        }
    }
    for (auto& v : activeLater) parsers_.activate(v.parserId, v.version, now);
}

std::shared_ptr<const ParserGeneration> Engine::parserFor(const SourceBinding& b) const {
    if (!b.parserVersion.empty()) {
        if (auto g = parsers_.leaseVersion(b.parserId, b.parserVersion)) return g;
    }
    if (!b.parserId.empty()) {
        if (auto g = parsers_.lease(b.parserId)) return g;
    }
    // if exactly one parser is installed, channels without their own use it, so a freshly delivered
    // parser works without binding it first. with 2+ we don't guess.
    std::string only;
    for (auto& info : parsers_.list()) {
        if (!info.active) continue;
        if (only.empty()) only = info.parserId;
        else if (only != info.parserId) { only.clear(); break; }
    }
    if (!only.empty()) return parsers_.lease(only);
    return {};
}

Status Engine::activateParserVersion(const std::string& id, const std::string& version, const json& notes, bool requireFixtures) {
    auto candidate = parsers_.leaseVersion(id, version);
    if (!candidate) return failStatus("not_found", "Parser " + id + " " + version + " is not published");
    json fixtureResult;
    if (requireFixtures) {
        auto fixtures = store_->loadFixtures(id);
        for (auto& n : sharedNegativeCorpus()) fixtures.push_back(n);
        auto suite = runFixtures(*candidate->parser, fixtures);
        fixtureResult = suite.toJson();
        if (!suite.allPassed()) return failStatus("fixtures_failed", std::to_string(suite.failed) + " fixture(s) fail; the active version was kept");
    }
    auto previous = parsers_.lease(id);
    json diffs = json::array();
    if (previous && previous->version != version) {
        std::vector<std::string> samples;
        for (auto& r : store_->historyPage({}, 0, 30))
            if (!r.value("preview", "").empty()) samples.push_back(r.value("preview", ""));
        for (auto& f : store_->loadFixtures(id)) samples.push_back(f.text);
        if (samples.size() > 40) samples.resize(40);
        for (auto& d : compareParsers(*previous->parser, *candidate->parser, samples))
            if (!d.changedFields.empty()) diffs.push_back({{"sample", d.sample.substr(0, 200)}, {"before", d.before}, {"after", d.after}, {"changed", d.changedFields}});
    }
    auto st = parsers_.activate(id, version, clock_.nowMs());
    if (!st) return st;
    for (auto& info : parsers_.list()) {
        if (info.parserId == id && info.version == version) store_->saveParserVersion(info);
    }
    json fullNotes = notes.is_object() ? notes : json::object();
    fullNotes["parserDiffs"] = diffs;
    if (!fixtureResult.is_null()) fullNotes["fixtures"] = fixtureResult;
    fullNotes["previousVersion"] = previous ? previous->version : "";
    fullNotes["activatedAtMs"] = clock_.nowMs();
    store_->addUpdateHistory(fullNotes.value("releaseId", "local"), id, previous ? previous->version : "", version, "activated", fullNotes, false);
    // Waiting candidates may be re-parsed under the new parser only before submission, recording changed fields.
    for (auto& [key, c] : waiting_) {
        const SourceBinding* b = policy_.findSource(c.chatId);
        if (!b || b->parserId != id) continue;
        ParseContext ctx{c.signal.source, c.signal.revision, c.signal.publishedAtMs, c.signal.receivedAtMs, false};
        Signal re = candidate->parser->parse(c.signal.rawText, ctx);
        auto before = toJson(c.signal), after = toJson(re);
        json changed = json::array();
        for (const char* f : {"status", "side", "symbol", "entry", "stopLoss", "targets"})
            if (before[f] != after[f]) changed.push_back(f);
        c.timeline.push_back({{"stage", "reparsed"}, {"atMs", clock_.nowMs()}, {"detail", "parser " + id + " " + version}, {"changedFields", changed}});
        c.signal = re;
    }
    notify("info", "Parser " + id + " " + version + " is now active");
    return okStatus();
}

json Engine::launchTerminal(const std::string& dataPath, const std::string& exePath, const std::string& chartSymbol) {
    mt5::LaunchRequest r;
    r.installDir = installDir_;
    r.appDataDir = dataDir_;
    r.dataPath = platform::widen(dataPath);
    r.exePath = platform::widen(exePath);
    r.chartSymbol = chartSymbol.empty() ? "EURUSD" : chartSymbol;
    try {
        json res = mt5::launch(r);
        setTerminalStatus(res.value("message", ""));
        return res;
    } catch (const std::exception& e) {
        setTerminalStatus(std::string("Could not start MetaTrader: ") + e.what());
        post([this, m = std::string(e.what())] { addError("MetaTrader start failed: " + m); });
        throw;
    }
}

void Engine::setTerminalStatus(const std::string& text) {
    std::lock_guard lock(terminalMu_);
    terminalStatus_ = text;
}

std::string Engine::terminalStatus() {
    std::lock_guard lock(terminalMu_);
    return terminalStatus_;
}

void Engine::installBridgeImplementation() {
    std::error_code ec;
    for (auto& dir : std::filesystem::directory_iterator(installDir_ + L"\\bridge\\impl", ec)) {
        auto manifest = dir.path() / L"manifest.json";
        if (!std::filesystem::exists(manifest)) continue;
        auto r = plugins_->installBridgeImplementation(manifest.wstring());
        if (!r) addError("Bridge implementation not installed: " + r.error().message);
    }
}

// message pipeline

void Engine::onIncoming(IncomingMessage m) {
    if (pendingMessages_.load() >= 1024) {
        // Bounded admission: never silently discard. Record the message durably and inhibit copying.
        overload_ = true;
        m.historical = true;
        store_->insertRawMessage(m);
        return;
    }
    ++pendingMessages_;
    if (!post([this, m = std::move(m)] {
            --pendingMessages_;
            processMessage(m);
        })) {
        --pendingMessages_;
        overload_ = true;
        store_->insertRawMessage(m);
    }
}

void Engine::processMessage(const IncomingMessage& m) {
    const SourceBinding* b = policy_.findSource(m.chatId);
    if (!b || !b->enabled) return;  // only allowlisted sources are processed
    if (!dedup_.firstSeen(m.chatId, m.messageId, m.editVersion)) return;
    auto ins = store_->admitRawMessage(m);  // read-only duplicate check; the insert is flushed in the background
    if (!ins) addError("Journal failure while storing a message: " + ins.error().message);
    else if (!ins.value()) return;  // replayed revision already recorded

    json timeline = json::array();
    timeline.push_back({{"stage", "received"}, {"atMs", m.receivedAtMs}, {"monoUs", m.receivedMonoUs}, {"detail", m.simulated ? "simulated message" : "TDLib update"}});
    auto mark = [&](const std::string& stage, const std::string& detail) {
        timeline.push_back({{"stage", stage}, {"atMs", clock_.nowMs()}, {"monoUs", clock_.monotonicUs()}, {"detail", detail}});
    };
    ParseContext ctx{{m.chatId, m.messageId}, static_cast<int>(m.editVersion), m.publishedAtMs, m.receivedAtMs, m.hasMedia};
    auto lease = parserFor(*b);  // one generation for the whole parse
    Signal s;
    if (lease) {
        s = lease->parser->parse(m.text, ctx);
    } else {
        s.source = ctx.source;
        s.revision = ctx.revision;
        s.rawText = m.text;
        s.publishedAtMs = m.publishedAtMs;
        s.receivedAtMs = m.receivedAtMs;
        s.status = ParseStatus::Unsupported;
        s.kind = MessageKind::TradeSetup;
        s.parserId = b->parserId;
        s.diagnostics.push_back("Parser '" + b->parserId + "' is not active");
    }
    parseUs_.add(s.parseMicros);
    mark("parsed", std::string(toString(s.status)) + (lease ? " by " + lease->parserId + "@" + lease->version : ""));
    const std::string key = signalKey(m.chatId, m.messageId);

    auto finishWithState = [&](SignalState state, const std::string& reason) {
        Decision d;
        d.kind = state == SignalState::Review ? DecisionKind::Review : DecisionKind::Rejected;
        d.reason = reason;
        store_->upsertSignal(s, state, toJson(d), timeline);
        feed_[key] = {{"key", key},
                      {"at", m.receivedAtMs},
                      {"chatId", std::to_string(m.chatId)},
                      {"messageId", std::to_string(m.messageId)},
                      {"revision", s.revision},
                      {"source", b->title},
                      {"symbol", s.symbol},
                      {"side", s.side ? toString(*s.side) : ""},
                      {"state", toString(state)},
                      {"status", toString(s.status)},
                      {"reason", reason},
                      {"parser", s.parserId + "@" + s.parserVersion},
                      {"simulated", m.simulated}};
        feedOrder_.push_back(key);
    };

    bool catchUp = false;
    if (m.historical) {
        const bool eligible = policy_.catchUpAfterReconnect && reconciled_ && (clock_.nowMs() - m.publishedAtMs) <= static_cast<std::int64_t>(policy_.catchUpMaxAgeSeconds) * 1000;
        if (!eligible) {
            finishWithState(SignalState::Historical, "Historical/recovered message: shown in History, never executed (catch-up is off or not eligible)");
            return;
        }
        catchUp = true;
    }
    if (m.editVersion > 0) {
        bool submitted = store_->hasSubmittedRevision(s.source);
        for (auto& [pid, run] : plans_)
            if (run.signalKey == key) submitted = true;
        if (submitted) {
            finishWithState(SignalState::Review, "Message edited after submission; edits never create a second entry (review only)");
            return;
        }
        waiting_.erase(key);  // pre-submission edit replaces the candidate, then revalidates
        gates_.erase(key);
        mark("edit", "pre-submission edit replaced the waiting candidate");
    }
    if (policy_.semanticDuplicateEnabled && s.status == ParseStatus::Valid) {
        if (auto match = semantic_.check(s, clock_.nowMs(), policy_.semanticDuplicateWindowSeconds)) {
            const SourceBinding* orig = policy_.findSource(match->original.chatId);
            finishWithState(SignalState::Review, "Possible cross-source duplicate of " + (orig ? orig->title : std::to_string(match->original.chatId)) + " message " +
                                                     std::to_string(match->original.messageId) + "; suppressed for review");
            return;
        }
    }
    Decision d = evaluate(s, *b, catchUp);
    mark("decided", std::string(toString(d.kind)) + ": " + d.reason);
    feedOrder_.push_back(key);
    applyDecision(std::move(s), *b, std::move(d), std::move(timeline));
}

Decision Engine::evaluate(const Signal& s, const SourceBinding& b, bool catchUp) {
    PlanningInput in;
    in.signal = &s;
    in.binding = &b;
    in.policy = &policy_;
    in.account = (account_ && bridgePaired_) ? &*account_ : nullptr;
    std::string reason;
    auto brokerSymbol = ExecutionPlanner::resolveBrokerSymbol(s, policy_, b, reason);
    if (brokerSymbol) {
        if (auto it = symbols_.find(*brokerSymbol); it != symbols_.end()) in.symbol = &it->second;
        if (auto it = quotes_.find(*brokerSymbol); it != quotes_.end()) in.quote = &it->second;
    }
    in.nowMs = clock_.nowMs();
    in.localMinuteOfDay = localMinuteOfDay(in.nowMs);
    in.clockReliable = true;
    if (account_ && !quotes_.empty()) {
        // Severely unreliable system time pauses age-sensitive execution (broker time is server-local, so
        // only gross drift in minutes-of-hour is checked).
        const auto& q = quotes_.begin()->second;
        if (q.brokerTimeMs > 0) {
            const std::int64_t drift = ((in.nowMs - q.brokerTimeMs) % 3600000 + 3600000) % 3600000;
            const std::int64_t d = std::min(drift, 3600000 - drift);
            if (d > 120000 && in.nowMs - q.receivedAtMs < 5000) in.clockReliable = false;
        }
    }
    in.isCatchUp = catchUp;
    return planner_.evaluate(in);
}

void Engine::setSignalState(const std::string& key, SignalState state, const json& decision, const std::string& reason) {
    auto it = feed_.find(key);
    if (it != feed_.end()) {
        it->second["state"] = toString(state);
        if (!reason.empty()) it->second["reason"] = reason;
        auto chat = std::stoll(it->second.value("chatId", "0"));
        auto msg = std::stoll(it->second.value("messageId", "0"));
        store_->updateSignalState({chat, msg}, it->second.value("revision", 0), state, decision, it->second.value("timeline", json::array()));
    }
}

void Engine::applyDecision(Signal s, const SourceBinding& b, Decision d, json timeline) {
    const std::string key = signalKey(s.source.chatId, s.source.messageId);
    auto& item = feed_[key];
    item = {{"key", key},
            {"at", s.receivedAtMs},
            {"chatId", std::to_string(s.source.chatId)},
            {"messageId", std::to_string(s.source.messageId)},
            {"revision", s.revision},
            {"source", b.title},
            {"symbol", s.symbol},
            {"side", s.side ? actionString(*s.side, s.orderKind) : ""},
            {"entry", s.entry ? json{s.entry->low, s.entry->high} : json(nullptr)},
            {"sl", s.stopLoss ? json(*s.stopLoss) : json(nullptr)},
            {"tps", [&] {
                 json a = json::array();
                 for (auto& t : s.targets) a.push_back(t.price);
                 return a;
             }()},
            {"status", toString(s.status)},
            {"parser", s.parserId + "@" + s.parserVersion},
            {"ai", (b.aiMode != AiMode::Off || (policy_.ai.alwaysAssess && s.status == ParseStatus::Valid)) ? "Pending" : "Off"},
            {"aiMode", toString(b.aiMode)},
            {"reason", d.reason},
            {"simulated", timeline.size() && timeline[0].value("detail", "") == "simulated message"},
            {"timeline", timeline}};
    if (d.plan) {
        item["lots"] = d.plan->totalVolume;
        item["brokerSymbol"] = d.plan->brokerSymbol;
    }
    if (auto q = quotes_.find(d.plan ? d.plan->brokerSymbol : s.symbol); q != quotes_.end()) item["spread"] = q->second.ask - q->second.bid;
    const std::int64_t now = clock_.nowMs();

    // Ask the backend about every valid signal when "always assess" is on (advisory, non-blocking), unless
    // the source is already doing a backend gate (which contacts the backend anyway).
    if (s.status == ParseStatus::Valid && (b.aiMode == AiMode::Advisory || (policy_.ai.alwaysAssess && b.aiMode != AiMode::BackendGate)))
        requestAi(s, AiMode::Advisory, now + 5000);

    auto persist = [&](SignalState st) {
        item["state"] = toString(st);
        store_->upsertSignal(s, st, toJson(d), timeline);
    };

    switch (d.kind) {
        case DecisionKind::Planned: {
            if (b.aiMode == AiMode::LocalGate) {
                AiAssessment a;
                if (localModel_) a = localModel_->assess(s);
                else a.unavailableReason = "No local model is configured";
                const GateOutcome g = evaluateGate(a, policy_.ai.gateAcceptThreshold, policy_.ai.gateTimeoutAction);
                item["ai"] = toString(a.verdict);
                timeline.push_back({{"stage", "aiGate"}, {"atMs", clock_.nowMs()}, {"detail", std::string("local gate: ") + toString(g)}});
                if (g != GateOutcome::Pass) {
                    d.kind = g == GateOutcome::Skip ? DecisionKind::Rejected : DecisionKind::Review;
                    d.reason = std::string("Local AI gate: ") + toString(g) + (a.unavailableReason.empty() ? "" : " (" + a.unavailableReason + ")");
                    item["reason"] = d.reason;
                    persist(g == GateOutcome::Skip ? SignalState::Rejected : SignalState::Review);
                    return;
                }
            } else if (b.aiMode == AiMode::BackendGate) {
                const std::int64_t deadline = now + policy_.ai.backendGateDeadlineMs;
                gates_[key] = Gate{s, s.source.chatId, deadline, timeline, AiMode::BackendGate};
                requestAi(s, AiMode::BackendGate, deadline);
                d.reason = "Waiting for backend AI gate (up to " + std::to_string(policy_.ai.backendGateDeadlineMs) + " ms)";
                item["reason"] = d.reason;
                persist(SignalState::Validated);
                return;
            }
            const std::string why = canDispatchReason();
            if (!why.empty()) {
                if (paused_ || b.entryMode != EntryMode::WaitForRange || !d.plan) {
                    d.kind = DecisionKind::Rejected;
                    d.reason = why;
                    item["reason"] = why;
                    persist(SignalState::Rejected);
                    return;
                }
                waiting_[key] = Candidate{s, s.source.chatId, d.plan->expiryMs, why, timeline};
                item["reason"] = why;
                persist(SignalState::Waiting);
                return;
            }
            persist(SignalState::Planned);
            dispatchPlan(*d.plan, s, timeline);
            return;
        }
        case DecisionKind::PreviewOnly:
            persist(SignalState::Previewed);
            return;
        case DecisionKind::Waiting:
            if (paused_) {
                d.kind = DecisionKind::Rejected;
                d.reason = "Copying is paused";
                item["reason"] = d.reason;
                persist(SignalState::Rejected);
                return;
            }
            waiting_[key] = Candidate{s, s.source.chatId, d.waitUntilMs, d.reason, timeline};
            persist(SignalState::Waiting);
            return;
        case DecisionKind::Review:
            persist(SignalState::Review);
            return;
        case DecisionKind::Rejected:
            persist(SignalState::Rejected);
            return;
    }
}

void Engine::reevaluateWaiting() {
    const std::int64_t now = clock_.nowMs();
    for (auto it = waiting_.begin(); it != waiting_.end();) {
        Candidate& c = it->second;
        const std::string key = it->first;
        const SourceBinding* b = policy_.findSource(c.chatId);
        json dec;
        if (!b || !b->enabled) {
            setSignalState(key, SignalState::Rejected, {{"kind", "Rejected"}, {"reason", "Source removed or disabled"}}, "Source removed or disabled");
            it = waiting_.erase(it);
            continue;
        }
        if (paused_) {
            setSignalState(key, SignalState::Rejected, {{"kind", "Rejected"}, {"reason", "Copying paused: unsubmitted entry rejected"}}, "Copying paused: unsubmitted entry rejected");
            it = waiting_.erase(it);
            continue;
        }
        if (now > c.untilMs) {
            setSignalState(key, SignalState::Expired, {{"kind", "Rejected"}, {"reason", "Waiting range expired"}}, "Waiting range expired");
            it = waiting_.erase(it);
            continue;
        }
        Decision d = evaluate(c.signal, *b, false);
        if (d.kind == DecisionKind::Waiting) {
            c.lastReason = d.reason;
            feed_[key]["reason"] = d.reason;
            ++it;
            continue;
        }
        if (d.kind == DecisionKind::Planned) {
            const std::string why = canDispatchReason();
            if (!why.empty()) {
                c.lastReason = why;
                feed_[key]["reason"] = why;
                ++it;
                continue;
            }
            Signal s = c.signal;
            json tl = c.timeline;
            tl.push_back({{"stage", "rangeEntered"}, {"atMs", now}, {"monoUs", clock_.monotonicUs()}, {"detail", d.reason}});
            it = waiting_.erase(it);
            setSignalState(key, SignalState::Planned, toJson(d), "");
            dispatchPlan(*d.plan, s, tl);
            continue;
        }
        const SignalState st = d.kind == DecisionKind::Review ? SignalState::Review : d.kind == DecisionKind::PreviewOnly ? SignalState::Previewed : SignalState::Rejected;
        setSignalState(key, st, toJson(d), d.reason);
        it = waiting_.erase(it);
    }
    // AI gate deadlines: late responses can never revive an expired entry.
    for (auto it = gates_.begin(); it != gates_.end();) {
        if (now > it->second.deadlineMs) {
            AiAssessment a;
            a.unavailableReason = "AI gate deadline passed";
            const GateOutcome g = evaluateGate(a, policy_.ai.gateAcceptThreshold, policy_.ai.gateTimeoutAction);
            const std::string reason = std::string("AI gate timed out (") + (g == GateOutcome::Skip ? "skipped" : "sent to review") + ")";
            setSignalState(it->first, g == GateOutcome::Skip ? SignalState::Rejected : SignalState::Review, {{"kind", "Review"}, {"reason", reason}}, reason);
            feed_[it->first]["ai"] = "Unavailable";
            it = gates_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string Engine::canDispatchReason() const {
    if (paused_) return reconciled_ ? "Copying is paused" : "Paused until startup reconciliation completes";
    if (policy_.executionMode != ExecutionMode::Automatic) return "Preview mode";
    if (!bridge_ || !bridgePaired_) return "MT5 terminal is not connected and paired";
    if (!bridgeHealthy_) return "EA heartbeat missing: new dispatch inhibited";
    if (!reconciled_) return "Reconciliation not complete";
    if (bridgeUpgrading_) return "Bridge implementation upgrade in progress";
    if (overload_) return "Overload detected: copying inhibited until reviewed";
    if (!store_->healthy()) return "Journal failure: new submissions are blocked (" + store_->lastWriteError() + ")";
    if (diskFreeMb(dataDir_) < 200) return "Low disk space: new submissions are blocked";
    if (!account_ || !boundAccount_ || !(account_->key == *boundAccount_)) return "Connected account differs from the bound account";
    return {};
}

void Engine::dispatchPlan(const ExecutionPlan& plan, const Signal& s, json timeline) {
    const std::string key = signalKey(s.source.chatId, s.source.messageId);
    const std::int64_t now = clock_.nowMs();
    PlanRun run;
    run.plan = plan;
    run.signalKey = key;
    run.receivedMonoUs = timeline.size() ? timeline[0].value("monoUs", static_cast<std::int64_t>(0)) : 0;
    for (auto& child : plan.children) {
        Command c;
        c.commandId = newUuid();
        c.planId = plan.planId;
        c.childIndex = child.index;
        c.action = actionString(plan.side, plan.kind);
        c.deadlineMs = std::min(plan.expiryMs, now + 5000);
        c.state = CommandState::Prepared;
        json payload = {{"commandId", c.commandId},
                        {"planId", plan.planId},
                        {"childIndex", child.index},
                        {"childCount", plan.children.size()},
                        {"account", toJson(plan.account)},
                        {"symbol", plan.brokerSymbol},
                        {"side", toString(plan.side)},
                        {"orderKind", toString(plan.kind)},
                        {"volume", child.volume},
                        {"stopLoss", plan.stopLoss},
                        {"takeProfit", child.takeProfit ? *child.takeProfit : 0.0},
                        {"entryLow", plan.entry ? plan.entry->low : 0.0},
                        {"entryHigh", plan.entry ? plan.entry->high : 0.0},
                        {"pendingPrice", plan.pendingPrice},
                        {"pendingExpiryMs", plan.pendingExpiryMs},
                        {"deadlineMs", c.deadlineMs},
                        {"maxDeviationPrice", plan.maxDeviationPrice},
                        {"maxSpreadPrice", plan.maxSpreadPrice},
                        {"sizingMode", plan.sizingMode},
                        {"riskPercent", plan.riskPercent},
                        {"plannedRiskMoney", plan.plannedRiskMoney},
                        {"magic", kCopierMagic},
                        {"comment", "SR " + c.commandId.substr(0, 8)},
                        {"configVersion", plan.configVersion},
                        {"publishedAtMs", s.publishedAtMs}};
        c.payloadJson = payload.dump();
        run.commands.push_back(c);
    }
    // The first child is journaled directly as Dispatching: plan, commands, reservations and the
    // Prepared->Dispatching step share ONE durable commit before anything is sent.
    if (!run.commands.empty() && canDispatchReason().empty()) run.commands[0].state = CommandState::Dispatching;
    const auto t0 = clock_.monotonicUs();
    auto st = store_->recordPlan(plan, run.commands);  // durable before crossing the submission boundary
    journalMs_.add(static_cast<double>(clock_.monotonicUs() - t0) / 1000.0);
    journalWaitMs_.add(store_->lastCriticalWaitMs());
    journalExecMs_.add(store_->lastCriticalExecMs());
    journalBodyMs_.add(store_->lastPlanBodyMs());
    journalFlushMs_.add(store_->lastPlanFlushMs());
    if (!st) {
        addError("Journal failure; nothing was submitted: " + st.error().message);
        setSignalState(key, SignalState::Rejected, {{"kind", "Rejected"}, {"reason", "Journal failure"}}, "Journal failure; nothing was submitted");
        return;
    }
    timeline.push_back({{"stage", "journaled"}, {"atMs", clock_.nowMs()}, {"monoUs", clock_.monotonicUs()}, {"detail", std::to_string(run.commands.size()) + " command(s) durably prepared"}});
    if (run.receivedMonoUs) decisionMs_.add(static_cast<double>(clock_.monotonicUs() - run.receivedMonoUs) / 1000.0);
    run.timeline = timeline;
    feed_[key]["timeline"] = timeline;
    feed_[key]["requested"] = plan.totalVolume;
    feed_[key]["commandState"] = "Prepared";
    plans_[plan.planId] = std::move(run);
    setSignalState(key, SignalState::Dispatched, {{"kind", "Planned"}, {"reason", "Dispatched to MT5"}, {"plan", toJson(plan)}}, "Dispatched to MT5");
    sendChild(plan.planId);
}

void Engine::sendChild(const std::string& planId) {
    auto it = plans_.find(planId);
    if (it == plans_.end()) return;
    PlanRun& run = it->second;
    if (run.next >= run.commands.size()) return;
    Command& cmd = run.commands[run.next];
    const std::string why = canDispatchReason();
    if (!why.empty() || clock_.nowMs() > cmd.deadlineMs) {
        const std::string reason = why.empty() ? "Deadline passed before dispatch" : why;
        store_->transition(cmd.commandId, CommandState::Rejected, json{{"reason", reason}, {"submitted", false}}.dump());
        feed_[run.signalKey]["commandState"] = "Rejected";
        feed_[run.signalKey]["reason"] = reason;
        ++run.next;
        stopRemainingChildren(planId, "Stopped after child " + std::to_string(cmd.childIndex + 1) + " was not dispatched");
        return;
    }
    if (cmd.state != CommandState::Dispatching) {  // later split children: separate durable transition
        if (auto st = store_->transition(cmd.commandId, CommandState::Dispatching, "{}"); !st) {
            addError("Cannot journal dispatch; command left Prepared: " + st.error().message);
            return;
        }
        cmd.state = CommandState::Dispatching;
    }
    ipc::Envelope e;
    e.type = ipc::msg::EXECUTE;
    e.id = cmd.commandId;
    e.accountKey = run.plan.account.str();
    e.seq = ++bridgeSeq_;
    e.deadline = cmd.deadlineMs;
    e.configVersion = run.plan.configVersion;
    e.payload = json::parse(cmd.payloadJson);
    const bool sent = bridge_ && bridge_->send(e.serialize(), 1000);
    inflight_[cmd.commandId] = Inflight{planId, run.signalKey, CommandState::Dispatching, cmd.deadlineMs + 10000, clock_.monotonicUs(), run.receivedMonoUs};
    ++run.next;
    feed_[run.signalKey]["commandState"] = "Dispatching";
    run.timeline.push_back({{"stage", "enqueued"}, {"atMs", clock_.nowMs()}, {"monoUs", clock_.monotonicUs()}, {"detail", "EXECUTE child " + std::to_string(cmd.childIndex + 1)}});
    feed_[run.signalKey]["timeline"] = run.timeline;
    if (!sent) {
        // The write may or may not have reached the EA: uncertain, never blindly repeated.
        applyCommandState(cmd.commandId, CommandState::Unknown, {{"reason", "pipe write failed; reconciliation required"}});
        stopRemainingChildren(planId, "Stopped after an uncertain dispatch");
        requestReconciliation();
    }
}

void Engine::stopRemainingChildren(const std::string& planId, const std::string& reason) {
    auto it = plans_.find(planId);
    if (it == plans_.end()) return;
    PlanRun& run = it->second;
    while (run.next < run.commands.size()) {
        store_->transition(run.commands[run.next].commandId, CommandState::Canceled, json{{"reason", reason}, {"submitted", false}}.dump());
        ++run.next;
    }
    feed_[run.signalKey]["reason"] = reason + " (partial plan execution; filled children stay protected, no automatic rollback)";
}

// AI

void Engine::requestAi(const Signal& s, AiMode mode, std::int64_t deadlineMs) {
    const std::string key = signalKey(s.source.chatId, s.source.messageId);
    if (licenseKey_.empty()) {
        // The backend requires a license key; without one, never call it. Show that in the feed.
        feed_[key]["ai"] = "No license";
        return;
    }
    AiJob job{key, s, policy_.ai.endpoint, licenseKey_, deadlineMs, mode};
    if (!aiQueue_.tryPush(std::move(job))) feed_[key]["ai"] = "Unavailable";
}

void Engine::aiLoop() {
    while (!stopping_) {
        auto job = aiQueue_.popFor(std::chrono::milliseconds(200));
        if (!job) continue;
        const std::int64_t now = clock_.nowMs();
        AiAssessment a;
        if (now >= job->deadlineMs) {
            a.unavailableReason = "expired before sending (discarded, never queued for later)";
        } else {
            const std::string reqId = newUuid();
            const std::string body = buildAiRequest(job->signal, reqId).dump();  // signal-only payload
            const unsigned timeout = static_cast<unsigned>(std::max<std::int64_t>(50, job->deadlineMs - now));
            auto r = httpRequest("POST", job->endpoint, body, "application/json", timeout, 64 * 1024, {{"X-License-Key", job->licenseKey}});
            if (r.status == 200) {
                a = parseAiResponse(r.body, reqId);
            } else if (r.status == 401) {
                a.unavailableReason = "license key rejected by the backend";
            } else {
                a.unavailableReason = r.error.empty() ? "HTTP " + std::to_string(r.status) : r.error;
            }
            a.latencyMs = r.elapsedMs;
        }
        if (a.assessedAtMs == 0) a.assessedAtMs = clock_.nowMs();
        post([this, key = job->key, a] { onAiResult(key, a); });
    }
}

void Engine::onAiResult(const std::string& key, AiAssessment a) {
    json aj = {{"verdict", toString(a.verdict)},
               {"reasons", a.reasons},
               {"modelVersion", a.modelVersion},
               {"score", a.score ? json(*a.score) : json(nullptr)},
               {"scoreCalibrated", a.scoreCalibrated},
               {"provider", a.provider},
               {"assessedAtMs", a.assessedAtMs},
               {"latencyMs", a.latencyMs},
               {"unavailableReason", a.unavailableReason},
               {"disclaimer", "AI assessments are estimates, not guaranteed predictions of profitable trades"}};
    auto fit = feed_.find(key);
    if (fit != feed_.end()) {
        fit->second["ai"] = toString(a.verdict);
        fit->second["aiDetail"] = aj;
        const auto chat = std::stoll(fit->second.value("chatId", "0"));
        const auto msg = std::stoll(fit->second.value("messageId", "0"));
        store_->updateSignalAi({chat, msg}, fit->second.value("revision", 0), aj);
        if (store_->getSetting("ai.storeLocally").value_or("true") == "true") store_->saveAiAssessment({chat, msg}, fit->second.value("revision", 0), aj);
    }
    auto git = gates_.find(key);
    if (git == gates_.end()) return;  // advisory, or a late gate response that can no longer revive the entry
    Gate g = git->second;
    gates_.erase(git);
    if (clock_.nowMs() > g.deadlineMs) return;
    const GateOutcome outcome = evaluateGate(a, policy_.ai.gateAcceptThreshold, policy_.ai.gateTimeoutAction);
    g.timeline.push_back({{"stage", "aiGate"}, {"atMs", clock_.nowMs()}, {"detail", std::string("backend gate: ") + toString(outcome)}});
    if (outcome != GateOutcome::Pass) {
        const std::string reason = std::string("Backend AI gate: ") + toString(outcome) + (a.unavailableReason.empty() ? "" : " (" + a.unavailableReason + ")");
        setSignalState(key, outcome == GateOutcome::Skip ? SignalState::Rejected : SignalState::Review, {{"kind", "Review"}, {"reason", reason}}, reason);
        return;
    }
    // Recheck quote, age, risk and range after the gate before submission.
    const SourceBinding* b = policy_.findSource(g.chatId);
    if (!b) return;
    Decision d = evaluate(g.signal, *b, false);
    if (d.kind != DecisionKind::Planned) {
        setSignalState(key, d.kind == DecisionKind::Review ? SignalState::Review : SignalState::Rejected, toJson(d), "After AI gate: " + d.reason);
        return;
    }
    const std::string why = canDispatchReason();
    if (!why.empty()) {
        setSignalState(key, SignalState::Rejected, {{"kind", "Rejected"}, {"reason", why}}, why);
        return;
    }
    dispatchPlan(*d.plan, g.signal, g.timeline);
}

// bridge

void Engine::onBridgeConnection(std::shared_ptr<ipc::PipeConnection> c) {
    int stage = 0;
    json hello;
    std::string nonceE;
    c->readLoop([&](std::string&& frame) {
        auto env = ipc::parseEnvelope(frame);
        if (!env) {
            c->close();
            return;
        }
        if (stage == 0) {
            if (env->type != ipc::msg::HELLO) return c->close();
            hello = env->payload;
            const std::string nonceB = hello.value("nonce", "");
            if (nonceB.size() != 32 || hello.value("terminalId", "").empty() || hello.value("server", "").empty()) return c->close();
            nonceE = crypto::toHex(crypto::randomBytes(16));
            ipc::Envelope ch;
            ch.type = ipc::msg::CHALLENGE;
            ch.payload = {{"nonce", nonceE}, {"proof", crypto::toHex(crypto::hmacSha256(*pairingSecret_, "engine|" + nonceB + "|" + nonceE))}};
            c->send(ch.serialize());
            stage = 1;
            return;
        }
        if (stage == 1) {
            if (env->type != ipc::msg::AUTH) return c->close();
            const std::string accountKey = hello.value("terminalId", "") + "|" + hello.value("login", "") + "|" + hello.value("server", "");
            auto expect = crypto::hmacSha256(*pairingSecret_, "bridge|" + nonceE + "|" + hello.value("nonce", "") + "|" + accountKey);
            auto proof = crypto::fromHex(env->payload.value("proof", ""));
            if (!proof || !crypto::constantTimeEqual(*proof, expect)) {
                post([this] { addError("Rejected a bridge connection with an invalid pairing proof"); });
                return c->close();
            }
            {
                std::lock_guard lock(bridgeConnMu_);
                if (activeBridgeConn_ && activeBridgeConn_->open()) {
                    // Reject a second active owner (one copier EA per terminal/account in V1).
                    ipc::Envelope err;
                    err.type = ipc::msg::ERROR_;
                    err.payload = {{"message", "Another copier EA is already connected; only one active destination is supported"}};
                    c->send(err.serialize());
                    post([this] { addError("Rejected a second copier EA connection; only one active owner is allowed"); });
                    return c->close();
                }
                activeBridgeConn_ = c;
            }
            stage = 2;
            try {
                callSync([&] {
                    onBridgeAuthenticated(c, hello);
                    return 0;
                });
            } catch (...) {
                c->close();
            }
            return;
        }
        post([this, e = std::move(*env)] { onBridgeEnvelope(e); });
    });
    {
        std::lock_guard lock(bridgeConnMu_);
        if (activeBridgeConn_ == c) activeBridgeConn_.reset();
    }
    if (stage == 2) post([this, c] { onBridgeDisconnected(c); });
}

void Engine::onBridgeAuthenticated(std::shared_ptr<ipc::PipeConnection> c, const json& hello) {
    AccountKey key{hello.value("terminalId", ""), jsonInt64(hello.value("login", json("0"))), hello.value("server", "")};
    bridge_ = c;
    bridgeHello_ = hello;
    bridgeHealthy_ = true;
    lastHeartbeatMs_ = clock_.nowMs();
    reconciled_ = false;
    if (!boundAccount_) {
        pairingPending_ = key;
        bridgePaired_ = false;
        bridgeStatusText_ = "Pairing approval required for account " + std::to_string(key.login) + " @ " + key.server;
        notify("warning", bridgeStatusText_);
    } else if (*boundAccount_ == key) {
        bridgePaired_ = true;
        pairingPending_.reset();
        bridgeStatusText_ = "Paired";
    } else {
        bridgePaired_ = false;
        pairingPending_ = key;
        paused_ = true;  // an account switch always pauses and requires rebinding
        bridgeStatusText_ = "Connected account " + std::to_string(key.login) + " @ " + key.server + " differs from the bound account; copying paused";
        addError(bridgeStatusText_);
    }
    ipc::Envelope w;
    w.type = ipc::msg::WELCOME;
    w.payload = {{"paired", bridgePaired_}, {"engineVersion", kProductVersion}};
    c->send(w.serialize());
    sendToBridge(ipc::msg::SNAPSHOT_REQUEST, {{"symbols", [&] {
                                                   json a = json::array();
                                                   std::set<std::string> syms;
                                                   for (auto& [k, v] : policy_.symbolMap) syms.insert(v);
                                                   for (auto& [k, v] : policy_.symbolLimits) syms.insert(k);
                                                   for (auto& s : syms) a.push_back(s);
                                                   return a;
                                               }()}});
    if (bridgePaired_) requestReconciliation();
}

void Engine::onBridgeDisconnected(const std::shared_ptr<ipc::PipeConnection>& c) {
    if (bridge_ != c) return;
    bridge_.reset();
    bridgePaired_ = false;
    bridgeHealthy_ = false;
    reconciled_ = false;
    paused_ = true;
    if (account_) account_->connected = false;
    bridgeStatusText_ = "MT5 terminal disconnected";
    notify("warning", "MT5 bridge disconnected; new dispatch inhibited until the EA reconnects and reconciles");
}

void Engine::sendToBridge(const std::string& type, const json& payload, const std::string& id) {
    if (!bridge_) return;
    ipc::Envelope e;
    e.type = type;
    e.id = id.empty() ? newUuid() : id;
    e.accountKey = boundAccount_ ? boundAccount_->str() : "";
    e.seq = ++bridgeSeq_;
    e.configVersion = policy_.configVersion;
    e.payload = payload;
    bridge_->send(e.serialize(), 1000);
}

void Engine::requestReconciliation() {
    if (!bridge_ || !bridgePaired_) return;
    reconciled_ = false;
    reconcileRequested_ = true;
    reconcileSentMs_ = clock_.nowMs();
    json cmds = json::array();
    for (auto& u : store_->unresolvedDetailed())
        cmds.push_back({{"commandId", u.command.commandId}, {"state", toString(u.command.state)}, {"orderTicket", u.orderTicket}, {"requestId", u.requestId}, {"symbol", u.symbol}, {"volume", u.volume}});
    sendToBridge(ipc::msg::RECONCILE_REQUEST, {{"commands", cmds}, {"sinceMs", clock_.nowMs() - 7 * 86400000LL}});
}

void Engine::onBridgeEnvelope(const ipc::Envelope& e) {
    const json& p = e.payload;
    if (e.type == ipc::msg::HEARTBEAT) {
        lastHeartbeatMs_ = clock_.nowMs();
        if (!bridgeHealthy_) {
            bridgeHealthy_ = true;
            notify("info", "EA heartbeat restored; reconciling before new dispatch");
            requestReconciliation();
        }
        return;
    }
    if (e.type == ipc::msg::ACCOUNT_SNAPSHOT) {
        auto a = accountFromJson(p);
        if (!a) return addError("Malformed account snapshot: " + a.error().message);
        AccountSnapshot acc = a.value();
        AccountKey hk{bridgeHello_.value("terminalId", ""), jsonInt64(bridgeHello_.value("login", json("0"))), bridgeHello_.value("server", "")};
        if (!(acc.key == hk)) {
            // The terminal switched accounts under us: pause and require rebinding.
            paused_ = true;
            bridgePaired_ = false;
            pairingPending_ = acc.key;
            addError("Terminal account changed to " + std::to_string(acc.key.login) + " @ " + acc.key.server + "; copying paused until rebinding");
        }
        acc.updatedAtMs = clock_.nowMs();
        account_ = acc;
        return;
    }
    if (e.type == ipc::msg::SYMBOL_SNAPSHOT) {
        for (auto& s : p.value("symbols", json::array())) {
            auto spec = symbolFromJson(s);
            if (spec) symbols_[spec.value().name] = spec.value();
        }
        return;
    }
    if (e.type == "SYMBOL_CATALOG") {  // full broker symbol list (names only), sent in chunks; reset on the first
        if (p.value("reset", false)) symbolCatalog_.clear();
        for (auto& n : p.value("names", json::array()))
            if (n.is_string() && !n.get<std::string>().empty()) symbolCatalog_.push_back(n.get<std::string>());
        return;
    }
    if (e.type == ipc::msg::QUOTE) {
        const json list = p.contains("quotes") ? p["quotes"] : json::array({p});
        for (auto& qj : list) {
            auto q = quoteFromJson(qj);
            if (!q) continue;
            Quote quote = q.value();
            quote.receivedAtMs = clock_.nowMs();  // quote updates may coalesce; trade events may not
            quotes_[quote.symbol] = quote;
        }
        // Waiting ranges react to the new price immediately instead of on the next 100 ms tick.
        if (!waiting_.empty()) reevaluateWaiting();
        return;
    }
    if (e.type == ipc::msg::COMMAND_STATUS) return handleCommandStatus(p);
    if (e.type == ipc::msg::TRADE_EVENT) return handleTradeEvent(p);
    if (e.type == ipc::msg::RECONCILE_REPORT) return handleReconcileReport(p);
    if (e.type == "BRIDGE_UPGRADE_RESULT") {
        bridgeUpgrading_ = false;
        const bool ok = p.value("ok", false);
        store_->addUpdateHistory("bridge-upgrade", "mt5_bridge_impl", p.value("previous", ""), p.value("version", ""), ok ? "activated" : "failed", p, false);
        if (ok) notify("info", "Bridge implementation " + p.value("version", "") + " active; queues and journals were preserved");
        else addError("Bridge implementation upgrade failed; previous implementation retained: " + p.value("error", ""));
        requestReconciliation();
        return;
    }
    if (e.type == ipc::msg::ERROR_) return addError("EA: " + p.value("message", "unknown error"));
}

void Engine::applyCommandState(const std::string& commandId, CommandState target, const json& detail) {
    auto cur = store_->commandState(commandId);
    if (!cur) return;
    if (*cur == target && target != CommandState::PartiallyFilled) return;
    // Walk intermediate states so the journal records a legal sequence (e.g. Dispatching -> ReceivedByEA -> Submitted -> Filled).
    const CommandState chain[] = {CommandState::ReceivedByEA, CommandState::Submitted, CommandState::Accepted};
    for (int guard = 0; guard < 4 && !canTransition(*cur, target); ++guard) {
        bool advanced = false;
        for (auto next : chain) {
            if (canTransition(*cur, next) && canTransition(next, target)) {
                store_->transition(commandId, next, json{{"implied", true}}.dump());
                cur = next;
                advanced = true;
                break;
            }
        }
        if (!advanced) {
            for (auto next : chain) {
                if (canTransition(*cur, next)) {
                    store_->transition(commandId, next, json{{"implied", true}}.dump());
                    cur = next;
                    advanced = true;
                    break;
                }
            }
        }
        if (!advanced) break;
    }
    if (!canTransition(*cur, target)) {
        addError("Ignored illegal command transition " + std::string(toString(*cur)) + " -> " + toString(target) + " for " + commandId.substr(0, 8));
        return;
    }
    store_->transition(commandId, target, detail.dump());
    auto inf = inflight_.find(commandId);
    if (inf != inflight_.end()) {
        inf->second.state = target;
        feed_[inf->second.signalKey]["commandState"] = toString(target);
        if (isTerminal(target)) inflight_.erase(inf);
    }
}

void Engine::handleCommandStatus(const json& p) {
    const std::string id = p.value("commandId", "");
    auto target = commandStateFromString(p.value("state", ""));
    if (id.empty() || !target) return;
    auto inf = inflight_.find(id);
    const std::string planId = inf != inflight_.end() ? inf->second.planId : "";
    const std::string key = inf != inflight_.end() ? inf->second.signalKey : "";
    const std::int64_t receivedMono = inf != inflight_.end() ? inf->second.receivedMonoUs : 0;
    const std::int64_t sentMono = inf != inflight_.end() ? inf->second.sentMonoUs : 0;
    applyCommandState(id, *target, p);
    store_->setCommandBrokerIds(id, p.value("requestId", ""), p.value("orderTicket", ""), p.value("filledVolume", 0.0));
    if (!key.empty()) {
        auto& item = feed_[key];
        if (p.contains("filledVolume")) {
            json fills = item.value("fills", json::object());
            fills[id] = p.value("filledVolume", 0.0);
            double sum = 0;
            for (auto& [k, v] : fills.items()) sum += v.get<double>();
            item["fills"] = fills;
            item["filled"] = sum;  // requested versus filled quantity across child orders
        }
        if (!p.value("orderTicket", "").empty()) {
            json tickets = item.value("tickets", json::array());
            if (std::find(tickets.begin(), tickets.end(), p.value("orderTicket", "")) == tickets.end()) tickets.push_back(p.value("orderTicket", ""));
            item["tickets"] = tickets;
        }
        if (!p.value("reason", "").empty()) item["reason"] = p.value("reason", "");
        const std::int64_t sendUs = p.value("sendUs", static_cast<std::int64_t>(0));
        if (sendUs > 0 && receivedMono > 0 && sendUs > receivedMono) {
            const double e2e = static_cast<double>(sendUs - receivedMono) / 1000.0;
            e2eMs_.add(e2e);
            item["latencyMs"] = e2e;
        }
        if ((*target == CommandState::Accepted || *target == CommandState::Filled || *target == CommandState::Working) && sentMono > 0)
            brokerMs_.add(static_cast<double>(clock_.monotonicUs() - sentMono) / 1000.0);
        auto pit = plans_.find(planId);
        if (pit != plans_.end()) {
            pit->second.timeline.push_back({{"stage", std::string("ea:") + toString(*target)}, {"atMs", clock_.nowMs()}, {"monoUs", clock_.monotonicUs()}, {"detail", p.value("reason", "")}});
            item["timeline"] = pit->second.timeline;
            store_->updateSignalState({std::stoll(item.value("chatId", "0")), std::stoll(item.value("messageId", "0"))}, item.value("revision", 0), SignalState::Dispatched,
                                      {{"kind", "Planned"}, {"reason", item.value("reason", "")}, {"plan", toJson(pit->second.plan)}}, pit->second.timeline);
        }
    }
    // Sequential split children: continue only after a successful submission; stop after failure/unknown.
    if (!planId.empty()) {
        if (*target == CommandState::Submitted || *target == CommandState::Accepted || *target == CommandState::Filled || *target == CommandState::Working ||
            *target == CommandState::PartiallyFilled) {
            sendChild(planId);
        } else if (*target == CommandState::Rejected || *target == CommandState::Unknown) {
            stopRemainingChildren(planId, "Plan stopped after child failure (" + std::string(toString(*target)) + ")");
        }
    }
}

void Engine::handleTradeEvent(const json& p) {
    const std::string kind = p.value("kind", "");
    const std::string accountKey = boundAccount_ ? boundAccount_->str() : "";
    if (kind == "deal") {
        auto d = dealFromJson(p.value("deal", json::object()));
        if (!d) return;
        Deal deal = d.value();
        if (deal.commandId.empty() && deal.magic == kCopierMagic) {
            if (auto cid = store_->commandIdForTicket(std::to_string(deal.order))) deal.commandId = *cid;
            else if (auto cid2 = store_->commandIdForTicket(std::to_string(deal.positionId))) deal.commandId = *cid2;
        }
        if (!deal.commandId.empty())
            if (auto chat = store_->chatIdForCommand(deal.commandId)) deal.sourceChatId = *chat;
        store_->upsertDeal(deal, accountKey);
        ledger_.addDeal(deal);  // deduplicated by ticket: replay after reconnect is counted once
        if (!deal.commandId.empty() && deal.entry == 0 && deal.positionId)
            store_->upsertPosition(std::to_string(deal.positionId), deal.commandId, deal.symbol, deal.type == 0 ? "BUY" : "SELL", deal.volume, deal.price, 0, 0, 0, false);
    } else if (kind == "position") {
        store_->upsertPosition(p.value("positionId", ""), p.value("commandId", ""), p.value("symbol", ""), p.value("side", ""), p.value("volume", 0.0), p.value("openPrice", 0.0),
                               p.value("sl", 0.0), p.value("tp", 0.0), p.value("profit", 0.0), p.value("closed", false));
    }
}

void Engine::handleReconcileReport(const json& p) {
    int resolved = 0, review = 0;
    std::map<std::string, json> reports;
    for (auto& c : p.value("commands", json::array())) reports[c.value("commandId", "")] = c;
    for (auto& u : store_->unresolvedDetailed()) {
        const std::string id = u.command.commandId;
        auto it = reports.find(id);
        const json r = it != reports.end() ? it->second : json::object();
        const std::string eaState = r.value("state", "");
        if (u.command.state == CommandState::Prepared) {
            // Never dispatched: safe to reject.
            store_->transition(id, CommandState::Rejected, json{{"reason", "not dispatched before restart"}, {"submitted", false}}.dump());
            ++resolved;
            continue;
        }
        if (eaState.empty() || eaState == "Unknown") {
            if (!r.value("journaled", false)) {
                // The EA journal has no acceptance record: the EA never submitted it.
                applyCommandState(id, CommandState::Rejected, {{"reason", "EA has no acceptance record; never submitted"}, {"submitted", false}});
                ++resolved;
            } else {
                // Accepted by the EA but no broker evidence: ambiguous. Require review; never resubmit.
                applyCommandState(id, CommandState::Unknown, {{"reason", "EA journaled acceptance but no broker order/deal was found; review required"}});
                ++review;
            }
            continue;
        }
        if (auto st = commandStateFromString(eaState)) {
            applyCommandState(id, *st, r);
            store_->setCommandBrokerIds(id, r.value("requestId", ""), r.value("orderTicket", ""), r.value("filledVolume", 0.0));
            ++resolved;
        }
    }
    for (auto& d : p.value("deals", json::array())) handleTradeEvent({{"kind", "deal"}, {"deal", d}});
    if (review > 0) addError(std::to_string(review) + " command(s) have ambiguous outcomes and require review; they will not be resubmitted");
    for (auto it = inflight_.begin(); it != inflight_.end();) {
        auto st = store_->commandState(it->first);
        if (st && isTerminal(*st)) it = inflight_.erase(it);
        else ++it;
    }
    notify("info", "Reconciliation complete: " + std::to_string(resolved) + " resolved, " + std::to_string(review) + " need review");
    finishReconciliation();
}

void Engine::finishReconciliation() {
    reconcileRequested_ = false;
    reconciled_ = true;
    if (bridgePaired_ && account_ && boundAccount_ && account_->key == *boundAccount_) paused_ = userPaused_;  // restore the configured state
}

void Engine::checkHealth() {
    const std::int64_t now = clock_.nowMs();
    // Keep Windows from sleeping while copying automatically (sleep suspends execution). The request is
    // per-thread, so it always runs on the decision thread; the display is never forced on.
    const bool wantAwake = policy_.executionMode == ExecutionMode::Automatic && !paused_ && keepAwakeSetting_;
    if (wantAwake != keepingAwake_) {
        SetThreadExecutionState(ES_CONTINUOUS | (wantAwake ? ES_SYSTEM_REQUIRED : 0));
        keepingAwake_ = wantAwake;
    }
    if (bridge_ && bridgeHealthy_ && now - lastHeartbeatMs_ > 3000) {
        bridgeHealthy_ = false;  // three missed heartbeats inhibit new dispatch until healthy and reconciled
        reconciled_ = false;
        addError("EA heartbeat missed for 3 s; new dispatch inhibited");
    }
    if (reconcileRequested_ && now - reconcileSentMs_ > 15000) requestReconciliation();
    for (auto& [id, inf] : inflight_) {
        if ((inf.state == CommandState::Dispatching || inf.state == CommandState::ReceivedByEA || inf.state == CommandState::Submitted) && now > inf.deadlineMs) {
            const std::string cid = id;
            post([this, cid] {
                applyCommandState(cid, CommandState::Unknown, {{"reason", "no EA/broker confirmation before deadline; reconciling (never blindly repeated)"}});
                requestReconciliation();
            });
            inf.deadlineMs = INT64_MAX;
        }
    }
}

// notifications

void Engine::addError(const std::string& message) {
    errors_.push_front({{"at", clock_.nowMs()}, {"message", message}});
    while (errors_.size() > 50) errors_.pop_back();
    if (opt_.console) std::printf("[error] %s\n", message.c_str());
    broadcast("notification", {{"level", "error"}, {"message", message}});
}

void Engine::notify(const std::string& level, const std::string& message) {
    if (opt_.console) std::printf("[%s] %s\n", level.c_str(), message.c_str());
    broadcast("notification", {{"level", level}, {"message", message}});
}

}  // namespace sr::engine
