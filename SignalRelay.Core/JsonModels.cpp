#include "JsonModels.h"

#include <cmath>

namespace sr {

using nlohmann::json;

std::int64_t jsonInt64(const json& v, std::int64_t fallback) {
    try {
        if (v.is_string()) return std::stoll(v.get<std::string>());
        if (v.is_number_integer()) return v.get<std::int64_t>();
        if (v.is_number_unsigned()) return static_cast<std::int64_t>(v.get<std::uint64_t>());
        if (v.is_number_float()) return static_cast<std::int64_t>(v.get<double>());
    } catch (...) {
    }
    return fallback;
}

namespace {

double finite(const json& j, const char* key, double fallback = 0) {
    if (!j.contains(key) || !j[key].is_number()) return fallback;
    double v = j[key].get<double>();
    return std::isfinite(v) ? v : fallback;
}

std::uint64_t u64(const json& j, const char* key) {
    if (!j.contains(key)) return 0;
    auto& v = j[key];
    try {
        if (v.is_string()) return std::stoull(v.get<std::string>());
        if (v.is_number_unsigned()) return v.get<std::uint64_t>();
        if (v.is_number_integer()) return static_cast<std::uint64_t>(v.get<std::int64_t>());
    } catch (...) {
    }
    return 0;
}

}  // namespace

json toJson(const AccountKey& k) { return {{"terminalId", k.terminalId}, {"login", std::to_string(k.login)}, {"server", k.server}}; }

AccountKey accountKeyFromJson(const json& j) {
    AccountKey k;
    if (!j.is_object()) return k;
    k.terminalId = j.value("terminalId", "");
    if (j.contains("login")) k.login = jsonInt64(j["login"]);
    k.server = j.value("server", "");
    return k;
}

json toJson(const Signal& s) {
    json ev = json::array();
    for (auto& e : s.evidence) ev.push_back({{"field", e.field}, {"begin", e.span.begin}, {"end", e.span.end}});
    json tps = json::array();
    for (auto& t : s.targets) tps.push_back({{"index", t.index}, {"price", t.price}});
    json j = {{"chatId", std::to_string(s.source.chatId)},
              {"messageId", std::to_string(s.source.messageId)},
              {"revision", s.revision},
              {"status", toString(s.status)},
              {"kind", toString(s.kind)},
              {"symbolAlias", s.symbolAlias},
              {"symbol", s.symbol},
              {"side", s.side ? toString(*s.side) : ""},
              {"action", s.side ? actionString(*s.side, s.orderKind) : ""},
              {"orderKind", toString(s.orderKind)},
              {"explicitKind", s.explicitKind},
              {"marketIntent", s.marketIntent},
              {"targets", tps},
              {"evidence", ev},
              {"diagnostics", s.diagnostics},
              {"parserId", s.parserId},
              {"parserVersion", s.parserVersion},
              {"publishedAtMs", s.publishedAtMs},
              {"receivedAtMs", s.receivedAtMs},
              {"parseMicros", s.parseMicros},
              {"reference", s.reference}};
    if (s.entry) j["entry"] = {{"low", s.entry->low}, {"high", s.entry->high}};
    else j["entry"] = nullptr;
    j["stopLoss"] = s.stopLoss ? json(*s.stopLoss) : json(nullptr);
    j["lot"] = s.lot ? json(*s.lot) : json(nullptr);
    j["expiryMinutes"] = s.expiryMinutes ? json(*s.expiryMinutes) : json(nullptr);
    return j;
}

json toJson(const ExecutionPlan& p) {
    json children = json::array();
    for (auto& c : p.children) {
        children.push_back({{"index", c.index}, {"volume", c.volume}, {"takeProfit", c.takeProfit ? json(*c.takeProfit) : json(nullptr)}, {"tpIndex", c.tpIndex}});
    }
    json j = {{"planId", p.planId},
              {"configVersion", p.configVersion},
              {"account", toJson(p.account)},
              {"chatId", std::to_string(p.source.chatId)},
              {"messageId", std::to_string(p.source.messageId)},
              {"revision", p.revision},
              {"parserVersion", p.parserVersion},
              {"symbol", p.brokerSymbol},
              {"side", toString(p.side)},
              {"orderKind", toString(p.kind)},
              {"action", actionString(p.side, p.kind)},
              {"pendingPrice", p.pendingPrice},
              {"stopLoss", p.stopLoss},
              {"totalVolume", p.totalVolume},
              {"children", children},
              {"expiryMs", p.expiryMs},
              {"pendingExpiryMs", p.pendingExpiryMs},
              {"maxDeviationPrice", p.maxDeviationPrice},
              {"maxSpreadPrice", p.maxSpreadPrice},
              {"plannedRiskMoney", p.plannedRiskMoney},
              {"referencePrice", p.referencePrice},
              {"sizingMode", p.sizingMode},
              {"riskPercent", p.riskPercent},
              {"createdAtMs", p.createdAtMs}};
    if (p.entry) j["entry"] = {{"low", p.entry->low}, {"high", p.entry->high}};
    else j["entry"] = nullptr;
    return j;
}

Result<ExecutionPlan> planFromJson(const json& j) {
    try {
        ExecutionPlan p;
        p.planId = j.at("planId").get<std::string>();
        p.configVersion = j.value("configVersion", static_cast<std::int64_t>(0));
        p.account = accountKeyFromJson(j.value("account", json::object()));
        p.source.chatId = jsonInt64(j.value("chatId", json("0")));
        p.source.messageId = jsonInt64(j.value("messageId", json("0")));
        p.revision = j.value("revision", 0);
        p.parserVersion = j.value("parserVersion", "");
        p.brokerSymbol = j.at("symbol").get<std::string>();
        p.side = j.at("side").get<std::string>() == "SELL" ? Side::Sell : Side::Buy;
        std::string k = j.value("orderKind", "MARKET");
        p.kind = k == "LIMIT" ? OrderKind::Limit : k == "STOP" ? OrderKind::Stop : OrderKind::Market;
        p.pendingPrice = finite(j, "pendingPrice");
        p.stopLoss = finite(j, "stopLoss");
        p.totalVolume = finite(j, "totalVolume");
        for (auto& c : j.at("children")) {
            ChildOrder co;
            co.index = c.value("index", 0);
            co.volume = finite(c, "volume");
            if (c.contains("takeProfit") && c["takeProfit"].is_number()) co.takeProfit = c["takeProfit"].get<double>();
            co.tpIndex = c.value("tpIndex", 0);
            p.children.push_back(co);
        }
        if (j.contains("entry") && j["entry"].is_object()) p.entry = PriceRange{finite(j["entry"], "low"), finite(j["entry"], "high")};
        p.expiryMs = j.value("expiryMs", static_cast<std::int64_t>(0));
        p.pendingExpiryMs = j.value("pendingExpiryMs", static_cast<std::int64_t>(0));
        p.maxDeviationPrice = finite(j, "maxDeviationPrice");
        p.maxSpreadPrice = finite(j, "maxSpreadPrice");
        p.plannedRiskMoney = finite(j, "plannedRiskMoney");
        p.referencePrice = finite(j, "referencePrice");
        p.sizingMode = j.value("sizingMode", "");
        p.riskPercent = finite(j, "riskPercent");
        p.createdAtMs = j.value("createdAtMs", static_cast<std::int64_t>(0));
        return p;
    } catch (const std::exception& e) {
        return Error{"plan_invalid", e.what()};
    }
}

json toJson(const Decision& d) {
    json j = {{"kind", toString(d.kind)}, {"reason", d.reason}, {"waitUntilMs", d.waitUntilMs}, {"checks", d.checks}};
    j["plan"] = d.plan ? toJson(*d.plan) : json(nullptr);
    return j;
}

json toJson(const AccountSnapshot& a) {
    return {{"account", toJson(a.key)},
            {"currency", a.currency},
            {"balance", a.balance},
            {"equity", a.equity},
            {"margin", a.margin},
            {"freeMargin", a.freeMargin},
            {"marginMode", toString(a.marginMode)},
            {"tradeAllowed", a.tradeAllowed},
            {"connected", a.connected},
            {"simulated", a.simulated},
            {"copierOpenPositions", a.copierOpenPositions},
            {"copierPendingOrders", a.copierPendingOrders},
            {"copierOpenLots", a.copierOpenLots},
            {"copierDailyRealized", a.copierDailyRealized},
            {"netExposureSymbols", a.netExposureSymbols},
            {"copierSymbols", a.copierSymbols},
            {"updatedAtMs", a.updatedAtMs}};
}

Result<AccountSnapshot> accountFromJson(const json& j) {
    try {
        AccountSnapshot a;
        a.key = accountKeyFromJson(j.value("account", json::object()));
        a.currency = j.value("currency", "USD");
        a.balance = finite(j, "balance");
        a.equity = finite(j, "equity");
        a.margin = finite(j, "margin");
        a.freeMargin = finite(j, "freeMargin");
        a.marginMode = j.value("marginMode", "hedging") == "netting" ? MarginMode::Netting : MarginMode::Hedging;
        a.tradeAllowed = j.value("tradeAllowed", false);
        a.connected = j.value("connected", false);
        a.simulated = j.value("simulated", false);
        a.copierOpenPositions = j.value("copierOpenPositions", 0);
        a.copierPendingOrders = j.value("copierPendingOrders", 0);
        a.copierOpenLots = finite(j, "copierOpenLots");
        a.copierDailyRealized = finite(j, "copierDailyRealized");
        if (j.contains("netExposureSymbols")) a.netExposureSymbols = j["netExposureSymbols"].get<std::vector<std::string>>();
        if (j.contains("copierSymbols")) a.copierSymbols = j["copierSymbols"].get<std::vector<std::string>>();
        a.updatedAtMs = j.value("updatedAtMs", static_cast<std::int64_t>(0));
        return a;
    } catch (const std::exception& e) {
        return Error{"account_invalid", e.what()};
    }
}

json toJson(const SymbolSpec& s) {
    return {{"name", s.name},
            {"digits", s.digits},
            {"point", s.point},
            {"tickSize", s.tickSize},
            {"tickValue", s.tickValue},
            {"contractSize", s.contractSize},
            {"volumeMin", s.volumeMin},
            {"volumeMax", s.volumeMax},
            {"volumeStep", s.volumeStep},
            {"stopsLevel", s.stopsLevelPoints},
            {"freezeLevel", s.freezeLevelPoints},
            {"tradeAllowed", s.tradeAllowed},
            {"sessionOpen", s.sessionOpen},
            {"fillingModes", s.fillingModes},
            {"expirationModes", s.expirationModes},
            {"marginPerLot", s.marginPerLot}};
}

Result<SymbolSpec> symbolFromJson(const json& j) {
    try {
        SymbolSpec s;
        s.name = j.at("name").get<std::string>();
        s.digits = j.value("digits", 5);
        s.point = finite(j, "point", 0.00001);
        s.tickSize = finite(j, "tickSize", s.point);
        s.tickValue = finite(j, "tickValue", 0);
        s.contractSize = finite(j, "contractSize", 0);
        s.volumeMin = finite(j, "volumeMin", 0.01);
        s.volumeMax = finite(j, "volumeMax", 100);
        s.volumeStep = finite(j, "volumeStep", 0.01);
        s.stopsLevelPoints = j.value("stopsLevel", 0);
        s.freezeLevelPoints = j.value("freezeLevel", 0);
        s.tradeAllowed = j.value("tradeAllowed", true);
        s.sessionOpen = j.value("sessionOpen", true);
        s.fillingModes = j.value("fillingModes", 1);
        s.expirationModes = j.value("expirationModes", 15);
        s.marginPerLot = finite(j, "marginPerLot", 0);
        if (!(s.tickSize > 0) || !(s.volumeStep > 0) || !(s.volumeMin > 0)) return Error{"symbol_invalid", "Symbol specification has non-positive tick size or volume step"};
        return s;
    } catch (const std::exception& e) {
        return Error{"symbol_invalid", e.what()};
    }
}

json toJson(const Quote& q) { return {{"symbol", q.symbol}, {"bid", q.bid}, {"ask", q.ask}, {"brokerTimeMs", q.brokerTimeMs}, {"receivedAtMs", q.receivedAtMs}}; }

Result<Quote> quoteFromJson(const json& j) {
    try {
        Quote q;
        q.symbol = j.at("symbol").get<std::string>();
        q.bid = finite(j, "bid");
        q.ask = finite(j, "ask");
        q.brokerTimeMs = j.value("brokerTimeMs", static_cast<std::int64_t>(0));
        if (!(q.bid > 0 && q.ask > 0 && q.ask >= q.bid)) return Error{"quote_invalid", "Quote must have positive bid <= ask"};
        return q;
    } catch (const std::exception& e) {
        return Error{"quote_invalid", e.what()};
    }
}

json toJson(const Deal& d) {
    return {{"ticket", std::to_string(d.ticket)},
            {"order", std::to_string(d.order)},
            {"positionId", std::to_string(d.positionId)},
            {"timeMs", d.timeMs},
            {"symbol", d.symbol},
            {"type", d.type},
            {"entry", d.entry},
            {"volume", d.volume},
            {"price", d.price},
            {"profit", d.profit},
            {"commission", d.commission},
            {"swap", d.swap},
            {"fee", d.fee},
            {"magic", std::to_string(d.magic)},
            {"commandId", d.commandId},
            {"chatId", std::to_string(d.sourceChatId)}};
}

Result<Deal> dealFromJson(const json& j) {
    try {
        Deal d;
        d.ticket = u64(j, "ticket");
        d.order = u64(j, "order");
        d.positionId = u64(j, "positionId");
        d.timeMs = j.contains("timeMs") ? jsonInt64(j["timeMs"]) : 0;
        d.symbol = j.value("symbol", "");
        d.type = j.value("type", 0);
        d.entry = j.value("entry", 0);
        d.volume = finite(j, "volume");
        d.price = finite(j, "price");
        d.profit = finite(j, "profit");
        d.commission = finite(j, "commission");
        d.swap = finite(j, "swap");
        d.fee = finite(j, "fee");
        d.magic = j.contains("magic") ? jsonInt64(j["magic"]) : 0;
        d.commandId = j.value("commandId", "");
        d.sourceChatId = j.contains("chatId") ? jsonInt64(j["chatId"]) : 0;
        if (d.ticket == 0) return Error{"deal_invalid", "Deal ticket is required"};
        return d;
    } catch (const std::exception& e) {
        return Error{"deal_invalid", e.what()};
    }
}

}  // namespace sr
