#pragma once
// Versioned execution policy and per-source bindings. Plain data + validation.

#include <map>
#include <string>
#include <vector>

#include "Models.h"
#include "Result.h"
#include <nlohmann/json.hpp>

namespace sr {

struct SymbolLimits {
    double maxSpreadPrice = 0;          // broker price units; must be configured before execution
    double maxEntryDeviationPrice = 0;  // tolerance around single-price entries / NOW references
    double maxLots = 0;                 // 0 = no extra cap beyond account/broker limits
};

struct SizingPolicy {
    enum class Mode { FixedTotalLots, RiskPercent } mode = Mode::FixedTotalLots;
    double lots = 0.01;
    double riskPercent = 0.5;  // % of current equity lost at SL (estimate)
};

struct TargetPolicy {
    enum class Mode { Single, Split } mode = Mode::Single;
    int selectedIndex = 1;               // 1-based position in the provider's TP list
    std::vector<double> allocations;     // Split: percentages summing to 100
    bool singleTargetFallback = false;   // Split impossible with min lot -> single target instead of rejection
};

struct AccountLimits {
    int maxOpenPositions = 5;
    int maxPendingOrders = 5;
    double maxTotalLots = 1.0;
    double maxDailyLoss = 0;             // account currency, 0 = disabled
    double maxRiskPercentPerSignal = 2.0;
    int lossResetUtcOffsetMinutes = 0;   // explicit reset timezone for daily loss accounting
    int tradingStartMinute = 0;          // minutes since local midnight
    int tradingEndMinute = 24 * 60;
};

struct AiSettings {
    std::string endpoint = "http://localhost:8080/api/v1/ai/suggest";
    int backendGateDeadlineMs = 250;
    double gateAcceptThreshold = 0.6;    // minimum score for "Good" to pass a gate
    std::string gateTimeoutAction = "review";  // review | skip
    bool transmitSignalOnly = true;      // always true; exposed for transparency
    bool alwaysAssess = true;            // ask the backend for an advisory assessment on every valid signal
    std::string localModelPath;          // empty = local gate unavailable
};

struct SourceBinding {
    std::int64_t chatId = 0;
    std::string title;
    std::string chatType = "channel";  // channel | group | supergroup | private
    bool enabled = true;
    std::string parserId = "gold_provider_v1";
    std::string parserVersion;  // empty = follow the active version
    EntryMode entryMode = EntryMode::WaitForRange;
    double immediateTolerancePrice = 0.5;
    bool overrideSizing = false;
    SizingPolicy sizing;
    bool overrideTargets = false;
    TargetPolicy targets;
    AiMode aiMode = AiMode::Off;
    std::string tradeSymbol;  // broker symbol this channel trades (chosen from MT5); empty = resolve from the signal
    std::map<std::string, std::string> aliasOverrides;  // provider alias -> canonical

    nlohmann::json toJson() const;
    static Result<SourceBinding> fromJson(const nlohmann::json& j);
};

struct ExecutionPolicy {
    int schemaVersion = kConfigSchemaVersion;
    std::int64_t configVersion = 1;
    ExecutionMode executionMode = ExecutionMode::Preview;
    bool requireStopLoss = true;
    int maxImmediateAgeSeconds = 30;
    int rangeExpirySeconds = 120;
    int pendingExpirySeconds = 3600;
    int futureToleranceSeconds = 5;
    int quoteMaxAgeMs = 3000;
    bool catchUpAfterReconnect = false;
    int catchUpMaxAgeSeconds = 60;
    bool allowMessageLot = false;
    // "Force enter": for wait-for-range sources, still enter at market if the price has slipped only a few
    // pips past the entry band (a missed entry by a small movement). Off by default.
    bool forceEnterMissed = false;
    double forceEnterPips = 2.0;  // maximum pips past the band to still take the entry
    bool semanticDuplicateEnabled = false;
    int semanticDuplicateWindowSeconds = 60;
    SizingPolicy sizing;
    TargetPolicy targets;
    AccountLimits account;
    AiSettings ai;
    std::map<std::string, std::string> symbolMap{{"GOLD", "XAUUSD"}};
    std::map<std::string, SymbolLimits> symbolLimits;
    std::vector<SourceBinding> sources;

    nlohmann::json toJson(bool includeSources = true) const;
    static Result<ExecutionPolicy> fromJson(const nlohmann::json& j);
    Status validate() const;

    const SourceBinding* findSource(std::int64_t chatId) const;
    SizingPolicy sizingFor(const SourceBinding& b) const { return b.overrideSizing ? b.sizing : sizing; }
    TargetPolicy targetsFor(const SourceBinding& b) const { return b.overrideTargets ? b.targets : targets; }
};

// Canonical symbols that must never be auto-resolved; an explicit broker mapping is required.
bool requiresExplicitBrokerMapping(const std::string& canonical);

}  // namespace sr
