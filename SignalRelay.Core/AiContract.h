#pragma once
// AI assessment contract. Only the canonical signal is transmitted: never chat/message/sender IDs,
// account data, balances, history, fixtures or logs. AI can never override deterministic checks.

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "Models.h"
#include "Result.h"

namespace sr {

struct AiAssessment {
    AiVerdict verdict = AiVerdict::Unavailable;
    std::vector<std::string> reasons;
    std::string modelVersion;
    std::optional<double> score;  // provider score; NOT a calibrated win probability unless stated
    bool scoreCalibrated = false;
    std::string provider;         // backend | local
    std::int64_t assessedAtMs = 0;
    double latencyMs = 0;
    std::string unavailableReason;
};

// Builds the exact request body sent to POST /api/v1/ai/suggest.
nlohmann::json buildAiRequest(const Signal& s, const std::string& requestId);

// Keys allowed in the request; used by tests to prove nothing else is transmitted.
const std::vector<std::string>& allowedAiRequestKeys();
const std::vector<std::string>& allowedAiSignalKeys();

// Validates structured backend output; malformed output becomes Unavailable with a reason.
AiAssessment parseAiResponse(const std::string& body, const std::string& expectedRequestId);

enum class GateOutcome { Pass, Skip, Review };
const char* toString(GateOutcome g);
// Gate evaluation: explicit threshold; timeouts/unavailable follow the configured action (default review).
GateOutcome evaluateGate(const AiAssessment& a, double acceptThreshold, const std::string& timeoutAction);

// Optional compact local CPU model: logistic regression over locally computed signal features.
// Without a model file the local gate is Unavailable; no fake model is ever substituted.
class LocalLinearModel {
public:
    static Result<LocalLinearModel> load(const std::string& json);
    AiAssessment assess(const Signal& s) const;
    const std::string& version() const { return version_; }
    static std::vector<std::pair<std::string, double>> features(const Signal& s);

private:
    std::string version_;
    double bias_ = 0;
    std::vector<std::pair<std::string, double>> weights_;
    double goodThreshold_ = 0.6;
    double badThreshold_ = 0.4;
};

}  // namespace sr
