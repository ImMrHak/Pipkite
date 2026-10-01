#pragma once
// Local profit accounting from broker deals. Net realized P/L = profit + commission + swap + fee,
// each deal counted once (deduplicated by ticket), deposits/withdrawals kept as cash flows.

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "Models.h"

namespace sr {

struct ProfitFilter {
    std::int64_t fromMs = 0;
    std::int64_t toMs = INT64_MAX;
    std::string symbol;            // empty = all
    std::int64_t sourceChatId = 0; // 0 = all
    bool copierOnly = true;
};

struct ProfitSummary {
    double profit = 0;
    double commission = 0;
    double swap = 0;
    double fee = 0;
    double net = 0;
    double deposits = 0;
    double withdrawals = 0;
    int dealCount = 0;
    int unresolvedCount = 0;  // copier-magic deals without command attribution
    int wins = 0;
    int losses = 0;
};

struct CurvePoint {
    std::int64_t timeMs;
    double value;
};

class ProfitLedger {
public:
    // Returns false if the ticket was already recorded (replay after reconnect).
    bool addDeal(const Deal& d);
    ProfitSummary summarize(const ProfitFilter& f) const;
    std::vector<CurvePoint> cumulativeRealized(const ProfitFilter& f, std::size_t maxPoints = 500) const;
    std::size_t size() const { return deals_.size(); }
    static bool isTrade(const Deal& d) { return d.type == 0 || d.type == 1; }

private:
    std::map<std::uint64_t, Deal> deals_;  // by ticket
    bool matches(const Deal& d, const ProfitFilter& f) const;
};

// Largest-triangle-three-buckets style downsampling (keeps first/last and extremes per bucket).
std::vector<CurvePoint> downsample(const std::vector<CurvePoint>& in, std::size_t maxPoints);

}  // namespace sr
