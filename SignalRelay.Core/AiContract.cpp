#include "AiContract.h"

#include <chrono>
#include <cmath>

namespace sr {

using nlohmann::json;

const std::vector<std::string>& allowedAiRequestKeys() {
    static const std::vector<std::string> k{"schemaVersion", "requestId", "signal"};
    return k;
}

const std::vector<std::string>& allowedAiSignalKeys() {
    static const std::vector<std::string> k{"symbol", "side", "orderKind", "marketIntent", "entryLow", "entryHigh", "stopLoss", "takeProfits"};
    return k;
}

json buildAiRequest(const Signal& s, const std::string& requestId) {
    json sig;
    sig["symbol"] = s.symbol;
    sig["side"] = s.side ? toString(*s.side) : "";
    sig["orderKind"] = toString(s.orderKind);
    sig["marketIntent"] = s.marketIntent;
    sig["entryLow"] = s.entry ? json(s.entry->low) : json(nullptr);
    sig["entryHigh"] = s.entry ? json(s.entry->high) : json(nullptr);
    sig["stopLoss"] = s.stopLoss ? json(*s.stopLoss) : json(nullptr);
    json tps = json::array();
    for (auto& t : s.targets) tps.push_back(t.price);
    sig["takeProfits"] = tps;
    return {{"schemaVersion", 1}, {"requestId", requestId}, {"signal", sig}};
}

AiAssessment parseAiResponse(const std::string& body, const std::string& expectedRequestId) {
    AiAssessment a;
    a.provider = "backend";
    a.assessedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto fail = [&](const std::string& why) {
        a.verdict = AiVerdict::Unavailable;
        a.unavailableReason = why;
        a.reasons.clear();
        a.score.reset();
        return a;
    };
    if (body.size() > 32 * 1024) return fail("AI response too large");
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return fail("AI response is not a JSON object");
    if (j.value("requestId", "") != expectedRequestId) return fail("AI response requestId mismatch");
    const std::string v = j.value("verdict", "");
    if (v == "Good") a.verdict = AiVerdict::Good;
    else if (v == "Bad") a.verdict = AiVerdict::Bad;
    else if (v == "Uncertain") a.verdict = AiVerdict::Uncertain;
    else return fail("AI response has an invalid verdict");
    if (!j.contains("reasons") || !j["reasons"].is_array() || j["reasons"].size() > 10) return fail("AI response reasons are malformed");
    for (auto& r : j["reasons"]) {
        if (!r.is_string()) return fail("AI response reasons are malformed");
        std::string text = r.get<std::string>();
        if (text.size() > 300) text = text.substr(0, 300);
        a.reasons.push_back(text);
    }
    a.modelVersion = j.value("modelVersion", "");
    if (a.modelVersion.empty() || a.modelVersion.size() > 100) return fail("AI response modelVersion is missing");
    if (j.contains("score") && !j["score"].is_null()) {
        if (!j["score"].is_number()) return fail("AI response score is not numeric");
        double sc = j["score"].get<double>();
        if (!std::isfinite(sc) || sc < 0 || sc > 1) return fail("AI response score out of range");
        a.score = sc;
    }
    a.scoreCalibrated = j.value("scoreCalibrated", false);
    return a;
}

const char* toString(GateOutcome g) {
    switch (g) {
        case GateOutcome::Pass: return "Pass";
        case GateOutcome::Skip: return "Skip";
        case GateOutcome::Review: return "Review";
    }
    return "Review";
}

GateOutcome evaluateGate(const AiAssessment& a, double acceptThreshold, const std::string& timeoutAction) {
    if (a.verdict == AiVerdict::Unavailable) return timeoutAction == "skip" ? GateOutcome::Skip : GateOutcome::Review;
    if (a.verdict == AiVerdict::Bad) return GateOutcome::Skip;
    if (a.verdict == AiVerdict::Uncertain) return GateOutcome::Review;
    if (a.score && *a.score < acceptThreshold) return GateOutcome::Review;
    return GateOutcome::Pass;
}

std::vector<std::pair<std::string, double>> LocalLinearModel::features(const Signal& s) {
    std::vector<std::pair<std::string, double>> f;
    if (!s.side || !s.stopLoss || s.targets.empty()) return f;
    const double entry = s.entry ? (s.entry->low + s.entry->high) / 2 : s.targets.front().price;
    const double risk = std::fabs(entry - *s.stopLoss);
    const double reward1 = std::fabs(s.targets.front().price - entry);
    const double rewardLast = std::fabs(s.targets.back().price - entry);
    f.push_back({"rr_first", risk > 0 ? reward1 / risk : 0});
    f.push_back({"rr_last", risk > 0 ? rewardLast / risk : 0});
    f.push_back({"target_count", static_cast<double>(s.targets.size())});
    f.push_back({"range_width_rel", s.entry && entry > 0 ? (s.entry->high - s.entry->low) / entry * 1000 : 0});
    f.push_back({"risk_rel", entry > 0 ? risk / entry * 1000 : 0});
    f.push_back({"market_intent", s.marketIntent ? 1.0 : 0.0});
    return f;
}

Result<LocalLinearModel> LocalLinearModel::load(const std::string& text) {
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return Error{"model_invalid", "Local model file is not JSON"};
    if (j.value("format", "") != "signalrelay-linear-v1") return Error{"model_invalid", "Unsupported local model format"};
    LocalLinearModel m;
    m.version_ = j.value("modelVersion", "");
    if (m.version_.empty()) return Error{"model_invalid", "Local model needs modelVersion"};
    m.bias_ = j.value("bias", 0.0);
    if (!j.contains("weights") || !j["weights"].is_object()) return Error{"model_invalid", "Local model needs weights"};
    for (auto& [k, v] : j["weights"].items()) {
        if (!v.is_number()) return Error{"model_invalid", "Weights must be numeric"};
        m.weights_.push_back({k, v.get<double>()});
    }
    m.goodThreshold_ = j.value("goodThreshold", 0.6);
    m.badThreshold_ = j.value("badThreshold", 0.4);
    return m;
}

AiAssessment LocalLinearModel::assess(const Signal& s) const {
    const auto t0 = std::chrono::steady_clock::now();
    AiAssessment a;
    a.provider = "local";
    a.modelVersion = version_;
    a.assessedAtMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto f = features(s);
    if (f.empty()) {
        a.verdict = AiVerdict::Unavailable;
        a.unavailableReason = "Signal lacks the fields the local model needs";
        return a;
    }
    double z = bias_;
    for (auto& [name, w] : weights_)
        for (auto& [fn, fv] : f)
            if (fn == name) z += w * fv;
    const double p = 1.0 / (1.0 + std::exp(-z));
    a.score = p;
    a.scoreCalibrated = false;
    a.verdict = p >= goodThreshold_ ? AiVerdict::Good : p <= badThreshold_ ? AiVerdict::Bad : AiVerdict::Uncertain;
    a.reasons.push_back("Local model score " + std::to_string(p).substr(0, 5));
    a.latencyMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return a;
}

}  // namespace sr
