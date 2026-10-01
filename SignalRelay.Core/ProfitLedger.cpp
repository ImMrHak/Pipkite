#include "ProfitLedger.h"

#include <algorithm>

namespace sr {

bool ProfitLedger::addDeal(const Deal& d) {
    if (d.ticket == 0) return false;
    auto [it, inserted] = deals_.emplace(d.ticket, d);
    if (!inserted && it->second.commandId.empty() && !d.commandId.empty()) {
        // Later attribution evidence may resolve a previously unresolved deal; amounts never change.
        it->second.commandId = d.commandId;
        it->second.sourceChatId = d.sourceChatId;
    }
    return inserted;
}

bool ProfitLedger::matches(const Deal& d, const ProfitFilter& f) const {
    if (d.timeMs < f.fromMs || d.timeMs > f.toMs) return false;
    if (!f.symbol.empty() && d.symbol != f.symbol) return false;
    // Copier statistics contain only copier-owned trades; account cash flows belong to whole-account stats.
    if (f.copierOnly && (!isTrade(d) || d.magic != kCopierMagic)) return false;
    if (f.sourceChatId != 0 && d.sourceChatId != f.sourceChatId) return false;
    return true;
}

ProfitSummary ProfitLedger::summarize(const ProfitFilter& f) const {
    ProfitSummary s;
    for (auto& [ticket, d] : deals_) {
        if (!matches(d, f)) continue;
        if (!isTrade(d)) {
            // Balance operations are cash flows, never trading profit.
            if (d.type == 2) {
                if (d.profit >= 0) s.deposits += d.profit;
                else s.withdrawals += -d.profit;
            }
            continue;
        }
        if (f.copierOnly && d.commandId.empty()) ++s.unresolvedCount;
        s.profit += d.profit;
        s.commission += d.commission;
        s.swap += d.swap;
        s.fee += d.fee;
        ++s.dealCount;
        if (d.entry == 1 || d.entry == 2 || d.entry == 3) {
            const double closed = d.profit + d.swap + d.commission + d.fee;
            if (closed > 0) ++s.wins;
            else if (closed < 0) ++s.losses;
        }
    }
    s.net = s.profit + s.commission + s.swap + s.fee;
    return s;
}

std::vector<CurvePoint> ProfitLedger::cumulativeRealized(const ProfitFilter& f, std::size_t maxPoints) const {
    std::vector<const Deal*> sorted;
    for (auto& [t, d] : deals_)
        if (isTrade(d) && matches(d, f)) sorted.push_back(&d);
    std::sort(sorted.begin(), sorted.end(), [](const Deal* a, const Deal* b) { return a->timeMs < b->timeMs || (a->timeMs == b->timeMs && a->ticket < b->ticket); });
    std::vector<CurvePoint> pts;
    double cum = 0;
    for (auto* d : sorted) {
        cum += d->profit + d->commission + d->swap + d->fee;
        pts.push_back({d->timeMs, cum});
    }
    return downsample(pts, maxPoints);
}

std::vector<CurvePoint> downsample(const std::vector<CurvePoint>& in, std::size_t maxPoints) {
    if (in.size() <= maxPoints || maxPoints < 4) return in;
    std::vector<CurvePoint> out;
    out.push_back(in.front());
    const std::size_t buckets = (maxPoints - 2) / 2;
    const double width = static_cast<double>(in.size() - 2) / static_cast<double>(buckets);
    for (std::size_t b = 0; b < buckets; ++b) {
        std::size_t lo = 1 + static_cast<std::size_t>(b * width);
        std::size_t hi = std::min(in.size() - 1, 1 + static_cast<std::size_t>((b + 1) * width));
        if (lo >= hi) continue;
        auto mm = std::minmax_element(in.begin() + lo, in.begin() + hi, [](auto& a, auto& c) { return a.value < c.value; });
        if (mm.first->timeMs <= mm.second->timeMs) {
            out.push_back(*mm.first);
            if (mm.second != mm.first) out.push_back(*mm.second);
        } else {
            out.push_back(*mm.second);
            out.push_back(*mm.first);
        }
    }
    out.push_back(in.back());
    return out;
}

}  // namespace sr
