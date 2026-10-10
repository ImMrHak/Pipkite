#pragma once
// Source-identity deduplication (mandatory) and optional cross-source semantic duplicate detection.

#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_set>

#include "Models.h"

namespace sr {

// chat/message/edit identity. Bounded in memory; the database unique constraint is authoritative.
class RevisionDeduplicator {
public:
    explicit RevisionDeduplicator(std::size_t capacity = 50000) : capacity_(capacity) {}
    // Returns true when first seen.
    bool firstSeen(std::int64_t chatId, std::int64_t messageId, std::int64_t editVersion);

private:
    std::size_t capacity_;
    std::unordered_set<std::string> seen_;
    std::deque<std::string> order_;
    std::mutex mu_;
};

struct SemanticMatch {
    SourceKey original;
    std::int64_t atMs = 0;
};

// Normalized symbol/side/entry/SL/TP within a short window. Never merges silently: the caller
// routes a match to review/suppress with visible attribution.
class SemanticDuplicateDetector {
public:
    std::optional<SemanticMatch> check(const Signal& s, std::int64_t nowMs, int windowSeconds);
    static std::string key(const Signal& s);

private:
    struct Entry {
        std::string key;
        SourceKey source;
        std::int64_t atMs;
    };
    std::deque<Entry> recent_;
    std::mutex mu_;
};

}  // namespace sr
