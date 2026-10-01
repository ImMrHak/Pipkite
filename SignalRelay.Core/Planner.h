#pragma once
// valid signal + policy + broker facts -> plan, waiting candidate, or rejection/review.
// the EA checks everything again before sending.

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "Models.h"
#include "Policy.h"

namespace sr {

// volume/price arithmetic in integer broker units
std::int64_t toSteps(double volume, double step);          // floor, tolerant of FP noise
double roundVolumeDown(double volume, double step);
double normalizeToTick(double price, double tickSize);
std::int64_t toTicks(double price, double tickSize);

struct SplitResult {
    bool ok = false;
    std::string reason;
    std::vector<double> volumes;
};
// Splits a total volume by percentage allocations, rounding each child down to the lot step and
// assigning the remainder step by step without exceeding the total. Rejects children below min lot.
SplitResult splitVolume(double total, const std::vector<double>& allocationsPercent, const SymbolSpec& spec);

// Estimated account-currency loss at SL for 1.0 lot (tick-value approximation; the EA uses OrderCalcProfit).
double lossPerLot(double entryPrice, double stopLoss, const SymbolSpec& spec);

enum class DecisionKind { Planned, PreviewOnly, Waiting, Rejected, Review };
const char* toString(DecisionKind k);

struct Decision {
    DecisionKind kind = DecisionKind::Rejected;
    std::string reason;
    std::optional<ExecutionPlan> plan;
    std::int64_t waitUntilMs = 0;
    std::vector<std::string> checks;  // passed checks, for the processing timeline
};

struct PlanningInput {
    const Signal* signal = nullptr;
    const SourceBinding* binding = nullptr;
    const ExecutionPolicy* policy = nullptr;
    const AccountSnapshot* account = nullptr;  // null when no terminal is bound
    const SymbolSpec* symbol = nullptr;        // null when unknown
    const Quote* quote = nullptr;              // null when no quote yet
    std::int64_t nowMs = 0;
    int localMinuteOfDay = -1;                 // for trading hours; -1 = skip
    bool clockReliable = true;
    bool isCatchUp = false;
};

class IExecutionPlanner {
public:
    virtual ~IExecutionPlanner() = default;
    virtual Decision evaluate(const PlanningInput& in) const = 0;
};

class ExecutionPlanner final : public IExecutionPlanner {
public:
    // idGenerator supplies plan ids (UUIDs in production, deterministic in tests).
    explicit ExecutionPlanner(std::function<std::string()> idGenerator) : newId_(std::move(idGenerator)) {}
    Decision evaluate(const PlanningInput& in) const override;

    // Resolves the broker symbol per policy. Never picks a vaguely matching symbol.
    static std::optional<std::string> resolveBrokerSymbol(const Signal& s, const ExecutionPolicy& p, const SourceBinding& b, std::string& reason);

private:
    std::function<std::string()> newId_;
};

}  // namespace sr
