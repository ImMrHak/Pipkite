#include "Policy.h"

#include <cmath>
#include <set>

namespace sr {

using nlohmann::json;

namespace {

json sizingJson(const SizingPolicy& s) {
    return {{"mode", s.mode == SizingPolicy::Mode::FixedTotalLots ? "fixed_total_lots" : "risk_percent"}, {"lots", s.lots}, {"riskPercent", s.riskPercent}};
}

Result<SizingPolicy> sizingFrom(const json& j) {
    SizingPolicy s;
    std::string mode = j.value("mode", "fixed_total_lots");
    if (mode == "fixed_total_lots") s.mode = SizingPolicy::Mode::FixedTotalLots;
    else if (mode == "risk_percent") s.mode = SizingPolicy::Mode::RiskPercent;
    else return Error{"policy_invalid", "sizing.mode must be fixed_total_lots or risk_percent"};
    s.lots = j.value("lots", 0.01);
    s.riskPercent = j.value("riskPercent", 0.5);
    return s;
}

json targetsJson(const TargetPolicy& t) {
    return {{"mode", t.mode == TargetPolicy::Mode::Single ? "single" : "split"}, {"selectedIndex", t.selectedIndex}, {"allocations", t.allocations}, {"singleTargetFallback", t.singleTargetFallback}};
}

Result<TargetPolicy> targetsFrom(const json& j) {
    TargetPolicy t;
    std::string mode = j.value("mode", "single");
    if (mode == "single") t.mode = TargetPolicy::Mode::Single;
    else if (mode == "split") t.mode = TargetPolicy::Mode::Split;
    else return Error{"policy_invalid", "targets.mode must be single or split"};
    t.selectedIndex = j.value("selectedIndex", 1);
    if (j.contains("allocations")) t.allocations = j["allocations"].get<std::vector<double>>();
    t.singleTargetFallback = j.value("singleTargetFallback", false);
    return t;
}

// int64 identifiers are strings in UI-facing JSON to avoid precision loss in other clients.
std::int64_t idFrom(const json& v) {
    if (v.is_string()) return std::stoll(v.get<std::string>());
    return v.get<std::int64_t>();
}

}  // namespace

json SourceBinding::toJson() const {
    return {{"chatId", std::to_string(chatId)},
            {"title", title},
            {"chatType", chatType},
            {"enabled", enabled},
            {"template", parserId},
            {"templateVersion", parserVersion},
            {"entryMode", toString(entryMode)},
            {"immediateTolerancePrice", immediateTolerancePrice},
            {"overrideSizing", overrideSizing},
            {"sizing", sizingJson(sizing)},
            {"overrideTargets", overrideTargets},
            {"targets", targetsJson(targets)},
            {"aiMode", toString(aiMode)},
            {"tradeSymbol", tradeSymbol},
            {"aliasOverrides", aliasOverrides}};
}

Result<SourceBinding> SourceBinding::fromJson(const json& j) {
    try {
        SourceBinding b;
        b.chatId = idFrom(j.at("chatId"));
        b.title = j.value("title", "");
        b.chatType = j.value("chatType", "channel");
        b.enabled = j.value("enabled", true);
        b.parserId = j.value("template", "gold_provider_v1");
        b.parserVersion = j.value("templateVersion", "");
        auto em = entryModeFromString(j.value("entryMode", "wait_for_range"));
        if (!em) return Error{"policy_invalid", "Unknown entryMode for source " + std::to_string(b.chatId)};
        b.entryMode = *em;
        b.immediateTolerancePrice = j.value("immediateTolerancePrice", 0.5);
        b.overrideSizing = j.value("overrideSizing", false);
        if (j.contains("sizing")) {
            auto s = sizingFrom(j["sizing"]);
            if (!s) return s.error();
            b.sizing = s.value();
        }
        b.overrideTargets = j.value("overrideTargets", false);
        if (j.contains("targets")) {
            auto t = targetsFrom(j["targets"]);
            if (!t) return t.error();
            b.targets = t.value();
        }
        auto ai = aiModeFromString(j.value("aiMode", "off"));
        if (!ai) return Error{"policy_invalid", "Unknown aiMode"};
        b.aiMode = *ai;
        b.tradeSymbol = j.value("tradeSymbol", "");
        if (j.contains("aliasOverrides")) b.aliasOverrides = j["aliasOverrides"].get<std::map<std::string, std::string>>();
        return b;
    } catch (const std::exception& e) {
        return Error{"policy_invalid", std::string("Source binding is malformed: ") + e.what()};
    }
}

json ExecutionPolicy::toJson(bool includeSources) const {
    json limits = json::object();
    for (auto& [sym, l] : symbolLimits) limits[sym] = {{"maxSpreadPrice", l.maxSpreadPrice}, {"maxEntryDeviationPrice", l.maxEntryDeviationPrice}, {"maxLots", l.maxLots}};
    json j = {{"schemaVersion", schemaVersion},
              {"configVersion", configVersion},
              {"executionMode", toString(executionMode)},
              {"requireStopLoss", requireStopLoss},
              {"maxImmediateAgeSeconds", maxImmediateAgeSeconds},
              {"rangeExpirySeconds", rangeExpirySeconds},
              {"pendingExpirySeconds", pendingExpirySeconds},
              {"futureToleranceSeconds", futureToleranceSeconds},
              {"quoteMaxAgeMs", quoteMaxAgeMs},
              {"catchUpAfterReconnect", catchUpAfterReconnect},
              {"catchUpMaxAgeSeconds", catchUpMaxAgeSeconds},
              {"allowMessageLot", allowMessageLot},
              {"forceEnterMissed", forceEnterMissed},
              {"forceEnterPips", forceEnterPips},
              {"semanticDuplicate", {{"enabled", semanticDuplicateEnabled}, {"windowSeconds", semanticDuplicateWindowSeconds}}},
              {"sizing", sizingJson(sizing)},
              {"targets", targetsJson(targets)},
              {"accountLimits",
               {{"maxOpenPositions", account.maxOpenPositions},
                {"maxPendingOrders", account.maxPendingOrders},
                {"maxTotalLots", account.maxTotalLots},
                {"maxDailyLoss", account.maxDailyLoss},
                {"maxRiskPercentPerSignal", account.maxRiskPercentPerSignal},
                {"lossResetUtcOffsetMinutes", account.lossResetUtcOffsetMinutes},
                {"tradingStartMinute", account.tradingStartMinute},
                {"tradingEndMinute", account.tradingEndMinute}}},
              {"ai",
               {{"endpoint", ai.endpoint},
                {"backendGateDeadlineMs", ai.backendGateDeadlineMs},
                {"gateAcceptThreshold", ai.gateAcceptThreshold},
                {"gateTimeoutAction", ai.gateTimeoutAction},
                {"transmitSignalOnly", true},
                {"alwaysAssess", ai.alwaysAssess},
                {"localModelPath", ai.localModelPath}}},
              {"symbolMap", symbolMap},
              {"symbolLimits", limits}};
    if (includeSources) {
        json arr = json::array();
        for (auto& s : sources) arr.push_back(s.toJson());
        j["sources"] = arr;
    }
    return j;
}

Result<ExecutionPolicy> ExecutionPolicy::fromJson(const json& j) {
    try {
        ExecutionPolicy p;
        p.schemaVersion = j.value("schemaVersion", 0);
        if (p.schemaVersion != kConfigSchemaVersion)
            return Error{"schema_unsupported", "Configuration schema version " + std::to_string(p.schemaVersion) + " is not supported by SignalRelay " + kProductVersion +
                                                   ". Export it from the version that created it or migrate it to schemaVersion 1."};
        p.configVersion = j.value("configVersion", static_cast<std::int64_t>(1));
        auto mode = executionModeFromString(j.value("executionMode", "preview"));
        if (!mode) return Error{"policy_invalid", "executionMode must be preview or automatic"};
        p.executionMode = *mode;
        p.requireStopLoss = j.value("requireStopLoss", true);
        p.maxImmediateAgeSeconds = j.value("maxImmediateAgeSeconds", 30);
        p.rangeExpirySeconds = j.value("rangeExpirySeconds", 120);
        p.pendingExpirySeconds = j.value("pendingExpirySeconds", 3600);
        p.futureToleranceSeconds = j.value("futureToleranceSeconds", 5);
        p.quoteMaxAgeMs = j.value("quoteMaxAgeMs", 3000);
        p.catchUpAfterReconnect = j.value("catchUpAfterReconnect", false);
        p.catchUpMaxAgeSeconds = j.value("catchUpMaxAgeSeconds", 60);
        p.allowMessageLot = j.value("allowMessageLot", false);
        p.forceEnterMissed = j.value("forceEnterMissed", false);
        p.forceEnterPips = j.value("forceEnterPips", 2.0);
        if (j.contains("semanticDuplicate")) {
            p.semanticDuplicateEnabled = j["semanticDuplicate"].value("enabled", false);
            p.semanticDuplicateWindowSeconds = j["semanticDuplicate"].value("windowSeconds", 60);
        }
        if (j.contains("sizing")) {
            auto s = sizingFrom(j["sizing"]);
            if (!s) return s.error();
            p.sizing = s.value();
        }
        if (j.contains("targets")) {
            auto t = targetsFrom(j["targets"]);
            if (!t) return t.error();
            p.targets = t.value();
        }
        if (j.contains("accountLimits")) {
            auto& a = j["accountLimits"];
            p.account.maxOpenPositions = a.value("maxOpenPositions", 5);
            p.account.maxPendingOrders = a.value("maxPendingOrders", 5);
            p.account.maxTotalLots = a.value("maxTotalLots", 1.0);
            p.account.maxDailyLoss = a.value("maxDailyLoss", 0.0);
            p.account.maxRiskPercentPerSignal = a.value("maxRiskPercentPerSignal", 2.0);
            p.account.lossResetUtcOffsetMinutes = a.value("lossResetUtcOffsetMinutes", 0);
            p.account.tradingStartMinute = a.value("tradingStartMinute", 0);
            p.account.tradingEndMinute = a.value("tradingEndMinute", 24 * 60);
        }
        if (j.contains("ai")) {
            auto& a = j["ai"];
            p.ai.endpoint = a.value("endpoint", p.ai.endpoint);
            p.ai.backendGateDeadlineMs = a.value("backendGateDeadlineMs", 250);
            p.ai.gateAcceptThreshold = a.value("gateAcceptThreshold", 0.6);
            p.ai.gateTimeoutAction = a.value("gateTimeoutAction", "review");
            p.ai.alwaysAssess = a.value("alwaysAssess", true);
            p.ai.localModelPath = a.value("localModelPath", "");
        }
        if (j.contains("symbolMap")) p.symbolMap = j["symbolMap"].get<std::map<std::string, std::string>>();
        if (j.contains("symbolLimits")) {
            for (auto& [sym, l] : j["symbolLimits"].items()) {
                SymbolLimits sl;
                sl.maxSpreadPrice = l.value("maxSpreadPrice", 0.0);
                sl.maxEntryDeviationPrice = l.value("maxEntryDeviationPrice", 0.0);
                sl.maxLots = l.value("maxLots", 0.0);
                p.symbolLimits[sym] = sl;
            }
        }
        // Backward-compatible single "source" object form from the specification example.
        if (j.contains("source") && j["source"].is_object()) {
            json s = j["source"];
            if (!s.contains("chatId")) return Error{"policy_invalid", "source.chatId is required"};
            auto b = SourceBinding::fromJson(s);
            if (!b) return b.error();
            p.sources.push_back(b.value());
        }
        if (j.contains("sources")) {
            for (auto& s : j["sources"]) {
                auto b = SourceBinding::fromJson(s);
                if (!b) return b.error();
                p.sources.push_back(b.value());
            }
        }
        auto v = p.validate();
        if (!v) return v.error();
        return p;
    } catch (const std::exception& e) {
        return Error{"policy_invalid", std::string("Configuration JSON is malformed: ") + e.what()};
    }
}

namespace {

Status validateSizing(const SizingPolicy& s, const std::string& where) {
    if (s.mode == SizingPolicy::Mode::FixedTotalLots && !(std::isfinite(s.lots) && s.lots > 0 && s.lots <= 100))
        return failStatus("policy_invalid", where + ": fixed lots must be > 0 and <= 100");
    if (s.mode == SizingPolicy::Mode::RiskPercent && !(std::isfinite(s.riskPercent) && s.riskPercent > 0 && s.riskPercent <= 10))
        return failStatus("policy_invalid", where + ": risk percent must be > 0 and <= 10");
    return okStatus();
}

Status validateTargets(const TargetPolicy& t, const std::string& where) {
    if (t.selectedIndex < 1 || t.selectedIndex > 20) return failStatus("policy_invalid", where + ": selected TP index must be 1-20");
    if (t.mode == TargetPolicy::Mode::Split) {
        if (t.allocations.size() < 2) return failStatus("policy_invalid", where + ": split mode needs at least two allocations");
        double sum = 0;
        for (double a : t.allocations) {
            if (!(a > 0)) return failStatus("policy_invalid", where + ": allocations must be positive");
            sum += a;
        }
        if (std::fabs(sum - 100.0) > 1e-6) return failStatus("policy_invalid", where + ": split allocations must sum to 100%");
    }
    return okStatus();
}

}  // namespace

Status ExecutionPolicy::validate() const {
    if (maxImmediateAgeSeconds < 1 || maxImmediateAgeSeconds > 3600) return failStatus("policy_invalid", "maxImmediateAgeSeconds must be 1-3600");
    if (rangeExpirySeconds < 1 || rangeExpirySeconds > 86400) return failStatus("policy_invalid", "rangeExpirySeconds must be 1-86400");
    if (pendingExpirySeconds < 60 || pendingExpirySeconds > 7 * 86400) return failStatus("policy_invalid", "pendingExpirySeconds must be 60-604800");
    if (futureToleranceSeconds < 0 || futureToleranceSeconds > 60) return failStatus("policy_invalid", "futureToleranceSeconds must be 0-60");
    if (quoteMaxAgeMs < 100 || quoteMaxAgeMs > 60000) return failStatus("policy_invalid", "quoteMaxAgeMs must be 100-60000");
    if (forceEnterPips < 0 || forceEnterPips > 50) return failStatus("policy_invalid", "forceEnterPips must be 0-50 (keep the force-enter tolerance small)");
    if (!requireStopLoss) return failStatus("policy_invalid", "requireStopLoss cannot be disabled in this version");
    if (auto s = validateSizing(sizing, "Global sizing"); !s) return s;
    if (auto t = validateTargets(targets, "Global targets"); !t) return t;
    if (account.maxOpenPositions < 1 || account.maxPendingOrders < 0 || !(account.maxTotalLots > 0)) return failStatus("policy_invalid", "Account limits must be positive");
    if (!(account.maxRiskPercentPerSignal > 0 && account.maxRiskPercentPerSignal <= 20)) return failStatus("policy_invalid", "maxRiskPercentPerSignal must be > 0 and <= 20");
    if (account.tradingStartMinute < 0 || account.tradingEndMinute > 1440 || account.tradingStartMinute >= account.tradingEndMinute)
        return failStatus("policy_invalid", "Trading hours are invalid");
    if (ai.backendGateDeadlineMs < 50 || ai.backendGateDeadlineMs > 10000) return failStatus("policy_invalid", "AI gate deadline must be 50-10000 ms");
    if (ai.gateTimeoutAction != "review" && ai.gateTimeoutAction != "skip") return failStatus("policy_invalid", "AI gate timeout action must be review or skip");
    for (auto& [sym, l] : symbolLimits) {
        if (!(l.maxSpreadPrice >= 0 && l.maxEntryDeviationPrice >= 0 && l.maxLots >= 0)) return failStatus("policy_invalid", "Symbol limits for " + sym + " must be non-negative");
    }
    std::set<std::int64_t> ids;
    for (auto& s : sources) {
        if (s.chatId == 0) return failStatus("policy_invalid", "Source chat id cannot be 0");
        if (!ids.insert(s.chatId).second) return failStatus("policy_invalid", "Duplicate source " + std::to_string(s.chatId));
        if (s.overrideSizing)
            if (auto r = validateSizing(s.sizing, "Source " + s.title); !r) return r;
        if (s.overrideTargets)
            if (auto r = validateTargets(s.targets, "Source " + s.title); !r) return r;
        if (s.immediateTolerancePrice < 0) return failStatus("policy_invalid", "Immediate tolerance must be non-negative");
    }
    return okStatus();
}

const SourceBinding* ExecutionPolicy::findSource(std::int64_t chatId) const {
    for (auto& s : sources)
        if (s.chatId == chatId) return &s;
    return nullptr;
}

bool requiresExplicitBrokerMapping(const std::string& canonical) {
    static const std::set<std::string> kMustMap{"USOIL", "UKOIL", "WTI", "BRENT", "US30", "NAS100"};
    return kMustMap.count(canonical) > 0;
}

}  // namespace sr
