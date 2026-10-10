#pragma once
// Native engine: owns Telegram, configuration snapshots, parsing, decisions, durable command IDs and
// delivery. All mutable engine state is confined to one decision thread; other threads post tasks.

#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "PluginManager.h"
#include "Pipkite.Core/AiContract.h"
#include "Pipkite.Core/BoundedQueue.h"
#include "Pipkite.Core/Clock.h"
#include "Pipkite.Core/Dedup.h"
#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/Planner.h"
#include "Pipkite.Core/Policy.h"
#include "Pipkite.Core/ProfitLedger.h"
#include "Pipkite.Protocol/Envelope.h"
#include "Pipkite.Protocol/NamedPipe.h"
#include "Pipkite.Storage/Backup.h"
#include "Pipkite.Storage/Store.h"
#include "TelegramClient.h"
#include "Updater.h"

namespace sr::engine {

struct EngineOptions {
    bool console = false;
};

class LatencyStats {
public:
    void add(double v);
    nlohmann::json summary() const;

private:
    std::deque<double> samples_;
};

class Engine {
public:
    explicit Engine(EngineOptions opt);
    ~Engine();
    Status start();
    void waitForStop();
    void requestStop();

private:
    // threading
    void decisionLoop();
    bool post(std::function<void()> task);
    template <typename F>
    auto callSync(F&& f, unsigned timeoutMs = 10000) -> decltype(f());

    // message pipeline
    void onIncoming(IncomingMessage m);
    void processMessage(const IncomingMessage& m);
    Decision evaluate(const Signal& s, const SourceBinding& b, bool catchUp);
    void applyDecision(Signal s, const SourceBinding& b, Decision d, nlohmann::json timeline);
    void reevaluateWaiting();
    void dispatchPlan(const ExecutionPlan& plan, const Signal& s, nlohmann::json timeline);
    void sendChild(const std::string& planId);
    void stopRemainingChildren(const std::string& planId, const std::string& reason);
    std::string canDispatchReason() const;
    void setSignalState(const std::string& key, SignalState state, const nlohmann::json& decision, const std::string& reason);

    // AI
    void aiLoop();
    void requestAi(const Signal& s, AiMode mode, std::int64_t deadlineMs);
    void onAiResult(const std::string& key, AiAssessment a);

    // bridge
    void onBridgeConnection(std::shared_ptr<ipc::PipeConnection> c);
    void onBridgeAuthenticated(std::shared_ptr<ipc::PipeConnection> c, const nlohmann::json& hello);
    void onBridgeEnvelope(const ipc::Envelope& e);
    void onBridgeDisconnected(const std::shared_ptr<ipc::PipeConnection>& c);
    void sendToBridge(const std::string& type, const nlohmann::json& payload, const std::string& id = {});
    void requestReconciliation();
    void handleCommandStatus(const nlohmann::json& p);
    void handleTradeEvent(const nlohmann::json& p);
    void handleReconcileReport(const nlohmann::json& p);
    void applyCommandState(const std::string& commandId, CommandState target, const nlohmann::json& detail);
    void checkHealth();
    void finishReconciliation();

    // UI
    void onUiConnection(std::shared_ptr<ipc::PipeConnection> c);
    nlohmann::json handleUiRequest(const std::string& method, const nlohmann::json& params);
    nlohmann::json handleOnEngineThread(const std::string& method, const nlohmann::json& params);
    nlohmann::json snapshot();
    void publishSnapshot();
    void broadcast(const std::string& topic, const nlohmann::json& data);
    void notify(const std::string& level, const std::string& message);
    void addError(const std::string& message);

    // configuration
    void loadConfiguration();
    Status savePolicy(ExecutionPolicy p, bool bumpVersion);
    void initParsers();
    std::shared_ptr<const ParserGeneration> parserFor(const SourceBinding& b) const;
    Status activateParserVersion(const std::string& id, const std::string& version, const nlohmann::json& notes, bool requireFixtures);
    void updateTelegramAllowlist();
    nlohmann::json parserTemplateJson(const std::string& id, const std::string& version);
    void installBridgeImplementation();
    // MetaTrader terminal: install the EA and start the chosen terminal once (connection thread safe).
    nlohmann::json launchTerminal(const std::string& dataPath, const std::string& exePath, const std::string& chartSymbol);
    void setTerminalStatus(const std::string& text);
    std::string terminalStatus();

    EngineOptions opt_;
    SystemClock clock_;
    std::wstring dataDir_, installDir_;
    std::unique_ptr<db::Store> store_;
    std::unique_ptr<PluginManager> plugins_;
    std::unique_ptr<TelegramClient> telegram_;
    std::unique_ptr<Updater> updater_;
    ParserRegistry parsers_;
    ExecutionPlanner planner_;
    RevisionDeduplicator dedup_;
    SemanticDuplicateDetector semantic_;
    ProfitLedger ledger_;
    std::optional<LocalLinearModel> localModel_;

    BoundedQueue<std::function<void()>> tasks_{8192};
    std::atomic<int> pendingMessages_{0};
    std::thread decisionThread_;
    std::atomic<bool> stopping_{false};
    std::promise<void> stopPromise_;
    std::atomic<bool> stopRequested_{false};
    std::thread::id decisionThreadId_;

    // AI worker
    struct AiJob {
        std::string key;
        Signal signal;
        std::string endpoint;
        std::string licenseKey;
        std::int64_t deadlineMs;
        AiMode mode;
    };
    BoundedQueue<AiJob> aiQueue_{64};
    std::thread aiThread_;
    std::thread autoLaunchThread_;
    std::mutex terminalMu_;
    std::string terminalStatus_;
    std::string licenseKey_;  // decision-thread cache of the setting; sent as X-License-Key on backend calls

    // decision-thread state
    ExecutionPolicy policy_;
    bool paused_ = true;          // restart begins paused until reconciliation completes
    bool userPaused_ = false;
    bool keepAwakeSetting_ = true;
    bool keepingAwake_ = false;
    bool reconciled_ = false;
    std::atomic<bool> overload_{false};
    bool bridgeUpgrading_ = false;
    std::int64_t reconcileSentMs_ = 0;
    std::int64_t sessionStartMs_ = 0;

    struct Candidate {
        Signal signal;
        std::int64_t chatId;
        std::int64_t untilMs;
        std::string lastReason;
        nlohmann::json timeline;
    };
    std::map<std::string, Candidate> waiting_;
    struct Gate {
        Signal signal;
        std::int64_t chatId;
        std::int64_t deadlineMs;
        nlohmann::json timeline;
        AiMode mode;
    };
    std::map<std::string, Gate> gates_;

    struct PlanRun {
        ExecutionPlan plan;
        std::vector<Command> commands;
        std::size_t next = 0;
        std::string signalKey;
        nlohmann::json timeline;
        std::int64_t receivedMonoUs = 0;
    };
    std::map<std::string, PlanRun> plans_;  // planId -> sequential child dispatch
    struct Inflight {
        std::string planId;
        std::string signalKey;
        CommandState state;
        std::int64_t deadlineMs;
        std::int64_t sentMonoUs;
        std::int64_t receivedMonoUs;
    };
    std::map<std::string, Inflight> inflight_;

    struct FeedItem {
        nlohmann::json data;
    };
    std::deque<std::string> feedOrder_;
    std::map<std::string, nlohmann::json> feed_;
    std::deque<nlohmann::json> errors_;
    LatencyStats parseUs_, decisionMs_, journalMs_, journalWaitMs_, journalExecMs_, journalBodyMs_, journalFlushMs_, e2eMs_, brokerMs_;
    std::string lastSnapshot_;
    std::int64_t lastEquitySnapshotMs_ = 0;
    std::int64_t lastRetentionMs_ = 0;

    // bridge state (decision thread)
    std::shared_ptr<ipc::PipeConnection> bridge_;
    nlohmann::json bridgeHello_ = nlohmann::json::object();
    bool bridgePaired_ = false;
    std::optional<AccountKey> pairingPending_;
    std::optional<AccountKey> boundAccount_;
    std::optional<AccountSnapshot> account_;
    std::map<std::string, SymbolSpec> symbols_;
    std::vector<std::string> symbolCatalog_;  // every broker symbol name (from the EA), for pick-lists
    std::map<std::string, Quote> quotes_;
    std::int64_t lastHeartbeatMs_ = 0;
    bool bridgeHealthy_ = false;
    bool reconcileRequested_ = false;
    std::string bridgeStatusText_ = "No MT5 terminal connected";
    std::int64_t bridgeSeq_ = 0;

    // servers
    std::unique_ptr<ipc::PipeServer> bridgeServer_;
    std::unique_ptr<ipc::PipeServer> uiServer_;
    std::mutex uiMu_;
    std::vector<std::shared_ptr<ipc::PipeConnection>> uiClients_;
    std::optional<crypto::Bytes> pairingSecret_, uiSecret_;
    std::mutex bridgeConnMu_;
    std::shared_ptr<ipc::PipeConnection> activeBridgeConn_;  // guarded; used to reject a second owner

    // import staging
    std::optional<db::ImportPreview> stagedImport_;
};

template <typename F>
auto Engine::callSync(F&& f, unsigned timeoutMs) -> decltype(f()) {
    using R = decltype(f());
    if (std::this_thread::get_id() == decisionThreadId_) return f();
    auto p = std::make_shared<std::promise<R>>();
    auto fut = p->get_future();
    bool queued = post([p, fn = std::forward<F>(f)]() mutable {
        try {
            p->set_value(fn());
        } catch (...) {
            p->set_exception(std::current_exception());
        }
    });
    if (!queued) throw std::runtime_error("engine is busy");
    if (fut.wait_for(std::chrono::milliseconds(timeoutMs)) != std::future_status::ready) throw std::runtime_error("engine did not respond in time");
    return fut.get();
}

}  // namespace sr::engine
