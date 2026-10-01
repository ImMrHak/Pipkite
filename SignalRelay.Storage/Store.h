#pragma once
// Durable local store. One journal writer thread owns the write connection; reads use a
// separate WAL reader connection. Critical writes (commands) block until committed to disk.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "SignalRelay.Core/Interfaces.h"
#include "SignalRelay.Core/Models.h"
#include "SignalRelay.Core/ParserBuilder.h"
#include "Sqlite.h"

namespace sr::db {

struct HistoryFilter {
    std::string text;       // matches raw text, symbol or state
    std::string state;      // exact signal state, empty = any
    std::int64_t chatId = 0;
    std::int64_t fromMs = 0;
    std::int64_t toMs = 0;  // 0 = open ended
};

struct UnresolvedCommand {
    Command command;
    std::string symbol;
    double volume = 0;
    std::string accountKey;
    std::string requestId;
    std::string orderTicket;
};

class Store final : public IExecutionJournal {
public:
    static Result<std::unique_ptr<Store>> open(const std::string& path, const IClock& clock);
    ~Store() override;

    // Healthy = last critical write succeeded. When false the engine blocks new submissions.
    bool healthy() const { return healthy_.load(); }
    std::string lastWriteError() const;
    const std::string& path() const { return path_; }

    // messages & signals
    Result<bool> insertRawMessage(const IncomingMessage& m);  // false = duplicate revision (synchronous)
    // Hot-path variant: read-only duplicate check, then a background insert (no flush on the signal path).
    Result<bool> admitRawMessage(const IncomingMessage& m);
    Status upsertSignal(const Signal& s, SignalState state, const nlohmann::json& decision, const nlohmann::json& timeline);
    Status updateSignalState(const SourceKey& k, int revision, SignalState state, const nlohmann::json& decision, const nlohmann::json& timeline);
    Status updateSignalAi(const SourceKey& k, int revision, const nlohmann::json& ai);
    std::optional<SignalState> signalState(const SourceKey& k, int revision);
    bool hasSubmittedRevision(const SourceKey& k);  // any revision of this message crossed submission

    // execution journal
    Status recordPlan(const ExecutionPlan& plan, const std::vector<Command>& commands);  // atomic + durable
    Status recordCommand(const Command& c, const ExecutionPlan& plan) override;
    Status transition(const std::string& commandId, CommandState to, const std::string& detailJson) override;
    Status setCommandBrokerIds(const std::string& commandId, const std::string& requestId, const std::string& orderTicket, double filledVolume);
    std::vector<Command> unresolvedCommands() override;
    std::vector<UnresolvedCommand> unresolvedDetailed();
    std::optional<CommandState> commandState(const std::string& commandId);
    std::optional<std::string> commandIdForTicket(const std::string& orderOrPositionTicket);
    std::optional<std::int64_t> chatIdForCommand(const std::string& commandId);

    // broker facts
    Result<bool> upsertDeal(const Deal& d, const std::string& accountKey);
    std::vector<Deal> loadDeals(std::int64_t fromMs = 0);
    Status upsertPosition(const std::string& positionId, const std::string& commandId, const std::string& symbol, const std::string& side, double volume, double openPrice,
                          double sl, double tp, double profit, bool closed);
    Status saveProfitSnapshot(std::int64_t timeMs, const std::string& accountKey, double balance, double equity, double floating);
    nlohmann::json equityCurve(std::int64_t fromMs, std::size_t maxPoints);

    // configuration
    Status putSetting(const std::string& key, const std::string& value);
    std::optional<std::string> getSetting(const std::string& key);
    Status savePolicy(const nlohmann::json& policy, std::int64_t configVersion);
    // Mirrors the current source bindings (by chat ID) for history/reporting joins.
    Status replaceSources(const nlohmann::json& sources);
    std::optional<nlohmann::json> latestPolicy();
    Status saveParserVersion(const ParserVersionInfo& v);
    std::vector<ParserVersionInfo> loadParserVersions();
    Status saveFixtures(const std::string& parserId, const std::vector<Fixture>& fixtures);
    std::vector<Fixture> loadFixtures(const std::string& parserId);
    Status setWatermark(const std::string& scope, std::int64_t value);
    std::optional<std::int64_t> watermark(const std::string& scope);
    Status saveAccountBinding(const AccountKey& key);
    std::optional<AccountKey> activeAccountBinding();
    Status clearAccountBindings();

    // updates & AI
    Status addUpdateHistory(const std::string& releaseId, const std::string& component, const std::string& from, const std::string& to, const std::string& state,
                            const nlohmann::json& notes, bool restartRequired);
    nlohmann::json updateHistory(int limit);
    Status markUpdateNotesShown(std::int64_t id);
    Status saveAiAssessment(const SourceKey& k, int revision, const nlohmann::json& a);

    // queries for the UI
    std::int64_t historyCount(const HistoryFilter& f);
    nlohmann::json historyPage(const HistoryFilter& f, std::int64_t offset, int limit);
    nlohmann::json signalDetail(std::int64_t chatId, std::int64_t messageId, int revision);
    nlohmann::json recentFeed(int limit);
    nlohmann::json stats();

    // maintenance
    Result<nlohmann::json> purgeRetention(int rawTextDays, int metadataDays);
    Status eraseHistory();  // refuses while unresolved commands exist
    Result<std::string> integrityCheck();

    // Generic access for backup/export helpers.
    Status writeSync(std::function<Status(Database&)> fn);
    template <typename F>
    auto read(F&& fn) {
        std::lock_guard lock(readMu_);
        return fn(reader_);
    }

private:
    Store(const IClock& clock) : clock_(clock) {}
    void writerLoop();
    void writeAsync(std::function<Status(Database&)> fn);

    const IClock& clock_;
    std::string path_;
    Database writer_;
    Database reader_;
    std::mutex readMu_;

    struct Task {
        std::function<Status(Database&)> fn;
        std::shared_ptr<std::promise<Status>> done;
        std::chrono::steady_clock::time_point queuedAt{};
    };
    std::atomic<double> lastCriticalWaitMs_{0}, lastCriticalExecMs_{0}, lastPlanBodyMs_{0}, lastPlanFlushMs_{0};

public:
    // Diagnostics for the latency panel: queue wait and execution time of the most recent critical write.
    double lastCriticalWaitMs() const { return lastCriticalWaitMs_.load(); }
    double lastCriticalExecMs() const { return lastCriticalExecMs_.load(); }
    // Split of the most recent order-plan commit: SQL work vs. COMMIT (disk flush).
    double lastPlanBodyMs() const { return lastPlanBodyMs_.load(); }
    double lastPlanFlushMs() const { return lastPlanFlushMs_.load(); }

private:
    std::mutex qmu_;
    std::condition_variable qcv_;
    std::deque<Task> queue_;       // submission-critical writes, drained first
    std::deque<Task> background_;  // non-critical writes (FIFO among themselves)
    bool stopping_ = false;
    bool walDirty_ = false;  // guarded by qmu_: background frames not yet flushed by a FULL commit
    std::atomic<int> walFrames_{0};
    bool maintenanceDue_ = false;  // guarded by qmu_: a write happened since the last idle maintenance
    static constexpr int kCheckpointFrames = 4000;  // ~16 MB of WAL before an idle checkpoint
    std::thread thread_;
    std::atomic<bool> healthy_{true};
    mutable std::mutex errMu_;
    std::string lastError_;
};

}  // namespace sr::db
