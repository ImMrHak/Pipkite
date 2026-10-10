#include "Dedup.h"

#include <cmath>

namespace sr {

bool RevisionDeduplicator::firstSeen(std::int64_t chatId, std::int64_t messageId, std::int64_t editVersion) {
    std::string k = std::to_string(chatId) + ":" + std::to_string(messageId) + ":" + std::to_string(editVersion);
    std::lock_guard lock(mu_);
    if (!seen_.insert(k).second) return false;
    order_.push_back(std::move(k));
    while (order_.size() > capacity_) {
        seen_.erase(order_.front());
        order_.pop_front();
    }
    return true;
}

std::string SemanticDuplicateDetector::key(const Signal& s) {
    auto q = [](double v) { return std::to_string(std::llround(v * 1000.0)); };  // milli-price units
    std::string k = s.symbol + "|" + (s.side ? toString(*s.side) : "?") + "|";
    if (s.entry) k += q(s.entry->low) + "-" + q(s.entry->high);
    k += "|";
    if (s.stopLoss) k += q(*s.stopLoss);
    k += "|";
    if (!s.targets.empty()) k += q(s.targets.front().price);
    return k;
}

std::optional<SemanticMatch> SemanticDuplicateDetector::check(const Signal& s, std::int64_t nowMs, int windowSeconds) {
    const std::string k = key(s);
    std::lock_guard lock(mu_);
    while (!recent_.empty() && nowMs - recent_.front().atMs > static_cast<std::int64_t>(windowSeconds) * 1000) recent_.pop_front();
    for (auto& e : recent_) {
        if (e.key == k && !(e.source == s.source)) return SemanticMatch{e.source, e.atMs};
    }
    recent_.push_back({k, s.source, nowMs});
    if (recent_.size() > 4096) recent_.pop_front();
    return std::nullopt;
}

}  // namespace sr
