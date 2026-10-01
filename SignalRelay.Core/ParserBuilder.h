#pragma once
// No-code Parser Builder support: learn labels from highlighted samples, run fixture suites,
// and keep immutable parser versions with atomic generation switching and rollback.

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ParserTemplate.h"
#include "SignalParser.h"

namespace sr {

// learning from highlights
struct HighlightLabel {
    std::string field;  // symbol | buy | sell | entry | sl | tp | lot
    std::uint32_t begin = 0;  // original byte offsets
    std::uint32_t end = 0;
    std::string canonical;  // symbol only: canonical symbol (defaults to the highlighted text)
};

struct LearnResult {
    ParserTemplate updated;
    std::vector<std::string> learned;
    std::vector<std::string> warnings;
};

LearnResult learnFromHighlights(const ParserTemplate& base, const std::string& sample, const std::vector<HighlightLabel>& labels);

// fixtures
struct Fixture {
    std::string id;
    std::string text;
    std::string expect = "signal";  // signal | ignore | review
    std::optional<std::string> side, symbol;
    std::optional<double> entryLow, entryHigh, stopLoss;
    std::vector<double> takeProfits;

    nlohmann::json toJson() const;
    static Fixture fromJson(const nlohmann::json& j);
};

struct FixtureResult {
    std::string id;
    bool passed = false;
    std::string actualOutcome;
    std::string status;
    std::vector<std::string> differences;
    double micros = 0;
};

struct SuiteResult {
    int passed = 0;
    int failed = 0;
    double worstMicros = 0;
    std::vector<FixtureResult> results;
    bool allPassed() const { return failed == 0; }
    nlohmann::json toJson() const;
};

std::string outcomeOf(const Signal& s);  // signal | ignore | review
SuiteResult runFixtures(const ISignalParser& parser, const std::vector<Fixture>& fixtures);
// Built-in commentary/invalid corpus that every parser version must classify as non-signal.
const std::vector<Fixture>& sharedNegativeCorpus();
// The specification's required parser fixtures.
std::vector<Fixture> requiredSpecFixtures();

// Field-level comparison between two parser versions on the same samples (for "What's changed").
struct ParseDiff {
    std::string sample;
    nlohmann::json before;
    nlohmann::json after;
    std::vector<std::string> changedFields;
};
std::vector<ParseDiff> compareParsers(const ISignalParser& before, const ISignalParser& after, const std::vector<std::string>& samples);

// registry
struct ParserGeneration {
    std::uint64_t generation = 0;
    std::string parserId;
    std::string version;
    std::string origin;  // builtin | local | package | plugin
    std::shared_ptr<const ISignalParser> parser;
};

struct ParserVersionInfo {
    std::string parserId;
    std::string version;
    std::string origin;
    nlohmann::json templateJson;  // null for native plugin parsers
    std::int64_t createdAtMs = 0;
    std::int64_t activatedAtMs = 0;
    bool active = false;
};

class ParserRegistry {
public:
    // Publishes an immutable version. Re-publishing identical content is idempotent; different content
    // under an existing id+version is refused.
    Result<ParserVersionInfo> publish(const ParserTemplate& t, const std::string& origin, std::int64_t nowMs);
    Status registerNative(const std::string& parserId, const std::string& version, std::shared_ptr<const ISignalParser> parser, const std::string& origin, std::int64_t nowMs);
    // Compiles and switches the active generation atomically; in-flight parses keep their old lease.
    Status activate(const std::string& parserId, const std::string& version, std::int64_t nowMs);
    Result<std::string> rollback(const std::string& parserId, std::int64_t nowMs);
    std::shared_ptr<const ParserGeneration> lease(const std::string& parserId) const;
    std::shared_ptr<const ParserGeneration> leaseVersion(const std::string& parserId, const std::string& version) const;
    std::vector<ParserVersionInfo> list() const;
    std::vector<std::string> activationHistory(const std::string& parserId) const;
    std::optional<ParserTemplate> templateOf(const std::string& parserId, const std::string& version) const;

private:
    struct Stored {
        ParserVersionInfo info;
        std::shared_ptr<const ISignalParser> parser;
    };
    mutable std::shared_mutex mu_;
    std::map<std::string, std::map<std::string, Stored>> versions_;
    std::map<std::string, std::shared_ptr<const ParserGeneration>> active_;
    std::map<std::string, std::vector<std::string>> history_;
    std::uint64_t nextGeneration_ = 1;
};

}  // namespace sr
