#include "Planner.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace sr {

namespace {

std::string num(double v) {
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

Decision reject(std::string reason, std::vector<std::string> checks = {}) {
    Decision d;
    d.kind = DecisionKind::Rejected;
    d.reason = std::move(reason);
    d.checks = std::move(checks);
    return d;
}

}  // namespace

std::int64_t toSteps(double volume, double step) {
    if (!(step > 0) || !(volume >= 0)) return 0;
    return static_cast<std::int64_t>(std::floor(volume / step + 1e-9));
}

double roundVolumeDown(double volume, double step) {
    const std::int64_t n = toSteps(volume, step);
    // Rebuild from integer steps and trim FP noise to 8 decimals.
    return std::round(static_cast<double>(n) * step * 1e8) / 1e8;
}

std::int64_t toTicks(double price, double tickSize) {
    if (!(tickSize > 0)) return 0;
    return static_cast<std::int64_t>(std::llround(price / tickSize));
}

double normalizeToTick(double price, double tickSize) {
    if (!(tickSize > 0)) return price;
    return std::round(static_cast<double>(toTicks(price, tickSize)) * tickSize * 1e8) / 1e8;
}

double lossPerLot(double entryPrice, double stopLoss, const SymbolSpec& spec) {
    if (!(spec.tickSize > 0) || !(spec.tickValue > 0)) return 0;
    const double ticks = std::fabs(entryPrice - stopLoss) / spec.tickSize;
    return ticks * spec.tickValue;
}

SplitResult splitVolume(double total, const std::vector<double>& alloc, const SymbolSpec& spec) {
    SplitResult r;
    const std::int64_t totalSteps = toSteps(total, spec.volumeStep);
    const std::int64_t minSteps = static_cast<std::int64_t>(std::ceil(spec.volumeMin / spec.volumeStep - 1e-9));
    if (alloc.empty()) {
        r.reason = "No split allocations configured";
        return r;
    }
    std::vector<std::int64_t> steps;
    std::int64_t used = 0;
    for (double a : alloc) {
        std::int64_t s = static_cast<std::int64_t>(std::floor(static_cast<double>(totalSteps) * a / 100.0 + 1e-9));
        steps.push_back(s);
        used += s;
    }
    // Distribute the remainder one step at a time in allocation order; never exceeds the total.
    std::int64_t remainder = totalSteps - used;
    for (std::size_t k = 0; remainder > 0; k = (k + 1) % steps.size()) {
        ++steps[k];
        --remainder;
    }
    for (std::size_t k = 0; k < steps.size(); ++k) {
        if (steps[k] < minSteps) {
            r.reason = "Split child " + std::to_string(k + 1) + " would be below the broker minimum lot " + num(spec.volumeMin) + "; total " + num(total) + " cannot be split across " +
                       std::to_string(steps.size()) + " targets";
            return r;
        }
        r.volumes.push_back(std::round(static_cast<double>(steps[k]) * spec.volumeStep * 1e8) / 1e8);
    }
    r.ok = true;
    return r;
}

const char* toString(DecisionKind k) {
    switch (k) {
        case DecisionKind::Planned: return "Planned";
        case DecisionKind::PreviewOnly: return "PreviewOnly";
        case DecisionKind::Waiting: return "Waiting";
        case DecisionKind::Rejected: return "Rejected";
        case DecisionKind::Review: return "Review";
    }
    return "Rejected";
}

std::optional<std::string> ExecutionPlanner::resolveBrokerSymbol(const Signal& s, const ExecutionPolicy& p, const SourceBinding& b, std::string& reason) {
    if (!b.tradeSymbol.empty()) return b.tradeSymbol;  // this channel is pinned to one broker symbol (chosen from MT5)
    std::string canonical = s.symbol;
    if (auto it = b.aliasOverrides.find(s.symbolAlias); it != b.aliasOverrides.end()) canonical = it->second;
    if (canonical.empty()) {
        reason = "Signal has no symbol";
        return std::nullopt;
    }
    if (auto it = p.symbolMap.find(canonical); it != p.symbolMap.end()) return it->second;
    if (auto it = p.symbolMap.find(s.symbolAlias); it != p.symbolMap.end()) return it->second;
    if (requiresExplicitBrokerMapping(canonical)) {
        reason = canonical + " must be mapped to an exact broker symbol in Execution policy > Symbol map";
        return std::nullopt;
    }
    return canonical;  // exact name only; the terminal must report this symbol verbatim
}

Decision ExecutionPlanner::evaluate(const PlanningInput& in) const {
    std::vector<std::string> checks;
    const Signal& s = *in.signal;
    const ExecutionPolicy& p = *in.policy;

    if (!in.binding) return reject("Source chat is not in the allowlist");
    const SourceBinding& b = *in.binding;
    if (!b.enabled) return reject("Source is disabled");
    checks.push_back("source allowed");

    if (s.kind != MessageKind::TradeSetup) return reject(std::string("Not a trade setup (") + toString(s.kind) + ")");
    if (s.status == ParseStatus::Ambiguous || s.status == ParseStatus::Incomplete) {
        Decision d;
        d.kind = DecisionKind::Review;
        d.reason = std::string("Parser status ") + toString(s.status) + ": " + (s.diagnostics.empty() ? "" : s.diagnostics.back());
        return d;
    }
    if (s.status != ParseStatus::Valid) return reject(std::string("Parser status ") + toString(s.status));
    checks.push_back("parser status Valid");

    if (!in.clockReliable) return reject("System clock is unreliable; age-sensitive execution is paused");
    const std::int64_t age = in.nowMs - s.publishedAtMs;
    if (-age > static_cast<std::int64_t>(p.futureToleranceSeconds) * 1000) return reject("Signal timestamp is in the future beyond tolerance");
    if (in.isCatchUp && age > static_cast<std::int64_t>(p.catchUpMaxAgeSeconds) * 1000) return reject("Recovered message is older than the catch-up age limit");
    checks.push_back("age " + std::to_string(age) + " ms");

    if (p.requireStopLoss && !s.stopLoss) return reject("Stop loss is required");
    checks.push_back("stop loss present");

    std::string symReason;
    auto brokerSymbol = resolveBrokerSymbol(s, p, b, symReason);
    if (!brokerSymbol) return reject(symReason);
    checks.push_back("symbol " + s.symbol + " -> " + *brokerSymbol);

    if (!in.account) {
        if (p.executionMode == ExecutionMode::Preview) {
            Decision d;
            d.kind = DecisionKind::PreviewOnly;
            d.reason = "Preview: no MT5 account is bound yet, so volume and broker checks were not evaluated";
            d.checks = checks;
            return d;
        }
        return reject("No MT5 account is bound", checks);
    }
    const AccountSnapshot& acc = *in.account;
    if (!acc.connected) return reject("MT5 terminal is not connected to the broker", checks);
    if (!acc.tradeAllowed) return reject("Trading is not allowed on the bound account/terminal (check AutoTrading)", checks);
    checks.push_back("account " + std::to_string(acc.key.login) + "@" + acc.key.server);

    if (!in.symbol) return reject("Symbol " + *brokerSymbol + " is not available in the bound terminal", checks);
    const SymbolSpec& spec = *in.symbol;
    if (!spec.tradeAllowed) return reject("Trading is disabled for " + spec.name, checks);
    if (!spec.sessionOpen) return reject("Market is closed for " + spec.name, checks);

    auto limIt = p.symbolLimits.find(*brokerSymbol);
    if (limIt == p.symbolLimits.end()) limIt = p.symbolLimits.find(s.symbol);
    if (limIt == p.symbolLimits.end() || !(limIt->second.maxSpreadPrice > 0))
        return reject("Symbol limits (max spread, entry deviation) are not configured for " + *brokerSymbol, checks);
    const SymbolLimits& lim = limIt->second;

    // Execution deadline for this candidate.
    std::int64_t expiryMs = 0;
    if (b.entryMode == EntryMode::ImmediateWithinTolerance) expiryMs = s.publishedAtMs + static_cast<std::int64_t>(p.maxImmediateAgeSeconds) * 1000;
    else expiryMs = s.publishedAtMs + static_cast<std::int64_t>(p.rangeExpirySeconds) * 1000;
    if (s.expiryMinutes) expiryMs = std::min(expiryMs, s.publishedAtMs + static_cast<std::int64_t>(*s.expiryMinutes) * 60000);
    if (in.nowMs > expiryMs) return reject("Signal expired before it became executable", checks);

    auto waitOrReject = [&](const std::string& why) {
        if (b.entryMode == EntryMode::WaitForRange) {
            Decision d;
            d.kind = DecisionKind::Waiting;
            d.reason = why;
            d.waitUntilMs = expiryMs;
            d.checks = checks;
            return d;
        }
        return reject(why, checks);
    };

    if (!in.quote || in.quote->bid <= 0 || in.quote->ask <= 0) return waitOrReject("Waiting for a quote for " + *brokerSymbol);
    const Quote& q = *in.quote;
    if (in.nowMs - q.receivedAtMs > p.quoteMaxAgeMs) return waitOrReject("Quote for " + *brokerSymbol + " is stale");
    const double spread = q.ask - q.bid;
    if (spread > lim.maxSpreadPrice + 1e-12) return waitOrReject("Spread " + num(spread) + " exceeds the cap " + num(lim.maxSpreadPrice));
    checks.push_back("spread " + num(spread));

    const bool buy = *s.side == Side::Buy;
    const double exec = buy ? q.ask : q.bid;  // BUY uses ask, SELL uses bid
    OrderKind kind = OrderKind::Market;
    double pendingPrice = 0;
    std::int64_t pendingExpiryMs = 0;
    double forceSlack = 0;  // extra entry-deviation tolerance when a force-enter deliberately slipped past the band

    switch (b.entryMode) {
        case EntryMode::WaitForRange: {
            if (!s.entry) return reject("NOW without an entry reference cannot be range-validated; use a source with immediate_within_tolerance and a reference price", checks);
            double lo = s.entry->low, hi = s.entry->high;
            if (lo == hi) {
                lo -= lim.maxEntryDeviationPrice;
                hi += lim.maxEntryDeviationPrice;
            }
            if (exec < lo - 1e-12 || exec > hi + 1e-12) {
                // force enter: price only slipped a few pips past the entry, take it at market.
                // only on the moved-away side (BUY above the band, SELL below), a better price still waits.
                const double pip = spec.point * ((spec.digits == 3 || spec.digits == 5) ? 10.0 : 1.0);
                const double tol = p.forceEnterMissed ? p.forceEnterPips * pip : 0.0;
                const double past = buy ? exec - hi : lo - exec;
                const bool movedPast = buy ? exec > hi : exec < lo;
                if (tol > 0 && movedPast && past <= tol + 1e-12) {
                    forceSlack = tol;  // let the EA accept the deliberate slip on revalidation
                    checks.push_back("force-enter: price " + num(exec) + " slipped " + num(past) + " past [" + num(lo) + ", " + num(hi) + "] (within " + num(tol) + ")");
                } else {
                    return waitOrReject("Waiting for " + std::string(buy ? "ask " : "bid ") + num(exec) + " to enter [" + num(lo) + ", " + num(hi) + "]");
                }
            } else {
                checks.push_back("price " + num(exec) + " inside [" + num(lo) + ", " + num(hi) + "]");
            }
            break;
        }
        case EntryMode::ImmediateWithinTolerance: {
            if (age > static_cast<std::int64_t>(p.maxImmediateAgeSeconds) * 1000) return reject("Signal is older than the immediate-execution age limit", checks);
            if (!s.marketIntent) return reject("immediate_within_tolerance executes only explicit NOW/market signals", checks);
            if (!s.entry) return reject("NOW signal has no reference price to check tolerance against", checks);
            const double lo = s.entry->low - b.immediateTolerancePrice, hi = s.entry->high + b.immediateTolerancePrice;
            if (exec < lo - 1e-12 || exec > hi + 1e-12) return reject("Price " + num(exec) + " is outside the tolerance window [" + num(lo) + ", " + num(hi) + "]", checks);
            checks.push_back("price within tolerance");
            break;
        }
        case EntryMode::ExplicitPendingOnly: {
            if (!s.explicitKind) return reject("explicit_pending_only requires an explicit LIMIT or STOP instruction", checks);
            if (!s.entry || s.entry->low != s.entry->high) return reject("A pending order needs exactly one entry price", checks);
            kind = s.orderKind;
            pendingPrice = normalizeToTick(s.entry->low, spec.tickSize);
            const double ref = buy ? q.ask : q.bid;
            if (kind == OrderKind::Limit && buy && !(pendingPrice < ref)) return reject("BUY LIMIT price must be below the current ask", checks);
            if (kind == OrderKind::Stop && buy && !(pendingPrice > ref)) return reject("BUY STOP price must be above the current ask", checks);
            if (kind == OrderKind::Limit && !buy && !(pendingPrice > ref)) return reject("SELL LIMIT price must be above the current bid", checks);
            if (kind == OrderKind::Stop && !buy && !(pendingPrice < ref)) return reject("SELL STOP price must be below the current bid", checks);
            const double minDist = spec.stopsLevelPoints * spec.point;
            const double freezeDist = spec.freezeLevelPoints * spec.point;
            if (std::fabs(pendingPrice - ref) < std::max(minDist, freezeDist)) return reject("Pending price is inside the broker stops/freeze distance", checks);
            if ((spec.expirationModes & 4) == 0) return reject("Broker does not support specified pending-order expiry for " + spec.name + "; permanent orders are never left silently", checks);
            pendingExpiryMs = in.nowMs + static_cast<std::int64_t>(p.pendingExpirySeconds) * 1000;
            if (s.expiryMinutes) pendingExpiryMs = std::min(pendingExpiryMs, s.publishedAtMs + static_cast<std::int64_t>(*s.expiryMinutes) * 60000);
            expiryMs = std::min(expiryMs, in.nowMs + static_cast<std::int64_t>(p.maxImmediateAgeSeconds) * 1000);
            checks.push_back(std::string("pending ") + actionString(*s.side, kind) + " @" + num(pendingPrice));
            break;
        }
    }

    const double entryPrice = kind == OrderKind::Market ? exec : pendingPrice;
    const double sl = normalizeToTick(*s.stopLoss, spec.tickSize);
    if (buy ? !(sl < entryPrice) : !(sl > entryPrice)) return reject("Stop loss " + num(sl) + " is already on the wrong side of the execution price " + num(entryPrice), checks);
    const double minStop = spec.stopsLevelPoints * spec.point;
    if (std::fabs(entryPrice - sl) < minStop) return reject("Stop loss is closer than the broker stops level; SL is never widened automatically", checks);
    checks.push_back("stop distance ok");

    // sizing
    const SizingPolicy sp = p.sizingFor(b);
    const double lpl = lossPerLot(entryPrice, sl, spec);
    if (!(lpl > 0)) return reject("Cannot estimate loss per lot for " + spec.name + " (tick value unavailable)", checks);
    double total = 0;
    std::string sizingMode;
    if (sp.mode == SizingPolicy::Mode::FixedTotalLots) {
        total = (p.allowMessageLot && s.lot) ? *s.lot : sp.lots;
        sizingMode = (p.allowMessageLot && s.lot) ? "message_lot" : "fixed_total_lots";
    } else {
        if (!(acc.equity > 0)) return reject("Account equity is unavailable", checks);
        const double budget = acc.equity * sp.riskPercent / 100.0;
        total = budget / lpl;
        sizingMode = "risk_percent";
    }
    double cap = spec.volumeMax;
    if (lim.maxLots > 0) cap = std::min(cap, lim.maxLots);
    total = roundVolumeDown(std::min(total, cap), spec.volumeStep);  // always down, never up
    if (total + 1e-12 < spec.volumeMin) return reject("Broker minimum lot " + num(spec.volumeMin) + " exceeds the sizing budget; volume is never rounded up", checks);
    const double plannedRisk = lpl * total;
    if (acc.equity > 0 && plannedRisk / acc.equity * 100.0 > p.account.maxRiskPercentPerSignal + 1e-9)
        return reject("Planned risk " + num(plannedRisk / acc.equity * 100.0) + "% exceeds the per-signal limit " + num(p.account.maxRiskPercentPerSignal) + "%", checks);
    checks.push_back("volume " + num(total) + " (" + sizingMode + ")");

    // targets
    const TargetPolicy tp = p.targetsFor(b);
    std::vector<ChildOrder> children;
    auto tpPrice = [&](int ordinal) { return normalizeToTick(s.targets[static_cast<std::size_t>(ordinal - 1)].price, spec.tickSize); };
    auto singleChild = [&]() -> std::optional<std::string> {
        if (tp.selectedIndex > static_cast<int>(s.targets.size())) return "Selected TP" + std::to_string(tp.selectedIndex) + " is not present in the signal";
        children.push_back({0, total, tpPrice(tp.selectedIndex), tp.selectedIndex});
        return std::nullopt;
    };
    if (tp.mode == TargetPolicy::Mode::Single) {
        if (auto err = singleChild()) return reject(*err, checks);
    } else {
        if (acc.marginMode == MarginMode::Netting) return reject("Split targets are not supported on netting accounts", checks);
        std::optional<std::string> splitErr;
        if (s.targets.size() < tp.allocations.size()) splitErr = "Signal has fewer targets than split allocations";
        SplitResult sr;
        if (!splitErr) {
            sr = splitVolume(total, tp.allocations, spec);
            if (!sr.ok) splitErr = sr.reason;
        }
        if (splitErr) {
            if (!tp.singleTargetFallback) return reject(*splitErr, checks);
            checks.push_back("split fallback to single target: " + *splitErr);
            if (auto err = singleChild()) return reject(*err, checks);
        } else {
            for (std::size_t k = 0; k < sr.volumes.size(); ++k) children.push_back({static_cast<int>(k), sr.volumes[k], tpPrice(static_cast<int>(k + 1)), static_cast<int>(k + 1)});
        }
    }
    for (auto& c : children) {
        if (!c.takeProfit) continue;
        const double t = *c.takeProfit;
        if (buy ? !(t > entryPrice) : !(t < entryPrice)) return reject("TP" + std::to_string(c.tpIndex) + " " + num(t) + " is already reached or on the wrong side of the execution price", checks);
        if (std::fabs(t - entryPrice) < minStop) return reject("TP" + std::to_string(c.tpIndex) + " is closer than the broker stops level", checks);
    }
    checks.push_back(std::to_string(children.size()) + " child order(s)");

    // account/exposure limits
    const int n = static_cast<int>(children.size());
    if (kind == OrderKind::Market && acc.copierOpenPositions + n > p.account.maxOpenPositions) return reject("Open copier position limit reached", checks);
    if (kind != OrderKind::Market && acc.copierPendingOrders + n > p.account.maxPendingOrders) return reject("Pending order limit reached", checks);
    if (acc.copierOpenLots + total > p.account.maxTotalLots + 1e-9) return reject("Total copier lots would exceed " + num(p.account.maxTotalLots), checks);
    if (p.account.maxDailyLoss > 0 && -acc.copierDailyRealized >= p.account.maxDailyLoss) return reject("Daily realized loss limit reached", checks);
    if (in.localMinuteOfDay >= 0 && (in.localMinuteOfDay < p.account.tradingStartMinute || in.localMinuteOfDay >= p.account.tradingEndMinute))
        return reject("Outside configured trading hours", checks);
    if (acc.marginMode == MarginMode::Netting) {
        if (std::find(acc.netExposureSymbols.begin(), acc.netExposureSymbols.end(), *brokerSymbol) != acc.netExposureSymbols.end())
            return reject("Netting account already has exposure on " + *brokerSymbol + "; ownership cannot be assumed", checks);
    }
    if (spec.marginPerLot > 0 && spec.marginPerLot * total > acc.freeMargin) return reject("Insufficient free margin (estimate)", checks);
    if (kind == OrderKind::Market && (spec.fillingModes & 7) == 0) return reject("No supported filling mode for " + spec.name, checks);
    checks.push_back("limits ok");

    ExecutionPlan plan;
    plan.planId = newId_();
    plan.configVersion = p.configVersion;
    plan.account = acc.key;
    plan.source = s.source;
    plan.revision = s.revision;
    plan.parserVersion = s.parserId + "@" + s.parserVersion;
    plan.brokerSymbol = *brokerSymbol;
    plan.side = *s.side;
    plan.kind = kind;
    plan.entry = s.entry;
    if (plan.entry && b.entryMode == EntryMode::WaitForRange && plan.entry->low == plan.entry->high) {
        plan.entry = PriceRange{plan.entry->low - lim.maxEntryDeviationPrice, plan.entry->high + lim.maxEntryDeviationPrice};
    }
    if (plan.entry && b.entryMode == EntryMode::ImmediateWithinTolerance) {
        plan.entry = PriceRange{plan.entry->low - b.immediateTolerancePrice, plan.entry->high + b.immediateTolerancePrice};
    }
    plan.pendingPrice = pendingPrice;
    plan.stopLoss = sl;
    plan.totalVolume = total;
    plan.children = children;
    plan.expiryMs = expiryMs;
    plan.pendingExpiryMs = pendingExpiryMs;
    plan.maxDeviationPrice = std::max(lim.maxEntryDeviationPrice, forceSlack);
    plan.maxSpreadPrice = lim.maxSpreadPrice;
    plan.plannedRiskMoney = plannedRisk;
    plan.referencePrice = exec;
    plan.sizingMode = sizingMode;
    plan.riskPercent = sp.mode == SizingPolicy::Mode::RiskPercent ? sp.riskPercent : 0;
    plan.createdAtMs = in.nowMs;

    Decision d;
    d.kind = p.executionMode == ExecutionMode::Automatic ? DecisionKind::Planned : DecisionKind::PreviewOnly;
    d.reason = d.kind == DecisionKind::Planned ? "All checks passed" : "Preview: plan shown, nothing submitted";
    d.plan = std::move(plan);
    d.checks = std::move(checks);
    return d;
}

}  // namespace sr
