#include "ParserTemplate.h"

#include <algorithm>
#include <set>

#include "Text.h"

namespace sr {

using nlohmann::json;

json ParserTemplate::toJson() const {
    json j;
    j["schemaVersion"] = schemaVersion;
    j["id"] = id;
    j["version"] = version;
    j["name"] = name;
    j["description"] = description;
    j["keywords"] = {{"buy", buyWords}, {"sell", sellWords}, {"limit", limitWords}, {"stop", stopWords}, {"now", nowWords}};
    j["labels"] = {{"entry", entryLabels}, {"sl", slLabels}, {"tp", tpLabels}, {"lot", lotLabels}, {"reference", referenceLabels}, {"expiry", expiryLabels}};
    j["commentary"] = commentaryWords;
    j["management"] = managementPhrases;
    j["symbolAliases"] = symbolAliases;
    j["recognizeCurrencyPairs"] = recognizeCurrencyPairs;
    j["numberFormat"] = numberFormat == NumberFormat::DotDecimal ? "dot_decimal" : "comma_decimal";
    j["allowThousandsSeparator"] = allowThousandsSeparator;
    j["allowGluedTokens"] = allowGluedTokens;
    j["requireStopLoss"] = requireStopLoss;
    j["requireTakeProfit"] = requireTakeProfit;
    j["maxTargets"] = maxTargets;
    j["conflictBehavior"] = conflictBehavior;
    json rules = json::array();
    for (auto& r : advancedRules) rules.push_back({{"field", r.field}, {"pattern", r.pattern}});
    j["advancedRules"] = rules;
    return j;
}

namespace {

template <typename T>
void readIf(const json& j, const char* key, T& out) {
    if (j.contains(key) && !j[key].is_null()) out = j[key].get<T>();
}

}  // namespace

Result<ParserTemplate> ParserTemplate::fromJson(const json& j) {
    try {
        ParserTemplate t;
        readIf(j, "schemaVersion", t.schemaVersion);
        if (t.schemaVersion != 1) return Error{"schema_unsupported", "Parser template schema version " + std::to_string(t.schemaVersion) + " is not supported by Pipkite 1.0; migrate the template first."};
        readIf(j, "id", t.id);
        readIf(j, "version", t.version);
        readIf(j, "name", t.name);
        readIf(j, "description", t.description);
        if (j.contains("keywords")) {
            auto& k = j["keywords"];
            readIf(k, "buy", t.buyWords);
            readIf(k, "sell", t.sellWords);
            readIf(k, "limit", t.limitWords);
            readIf(k, "stop", t.stopWords);
            readIf(k, "now", t.nowWords);
        }
        if (j.contains("labels")) {
            auto& l = j["labels"];
            readIf(l, "entry", t.entryLabels);
            readIf(l, "sl", t.slLabels);
            readIf(l, "tp", t.tpLabels);
            readIf(l, "lot", t.lotLabels);
            readIf(l, "reference", t.referenceLabels);
            readIf(l, "expiry", t.expiryLabels);
        }
        readIf(j, "commentary", t.commentaryWords);
        readIf(j, "management", t.managementPhrases);
        readIf(j, "symbolAliases", t.symbolAliases);
        readIf(j, "recognizeCurrencyPairs", t.recognizeCurrencyPairs);
        std::string nf = "dot_decimal";
        readIf(j, "numberFormat", nf);
        if (nf == "dot_decimal") t.numberFormat = NumberFormat::DotDecimal;
        else if (nf == "comma_decimal") t.numberFormat = NumberFormat::CommaDecimal;
        else return Error{"template_invalid", "numberFormat must be dot_decimal or comma_decimal"};
        readIf(j, "allowThousandsSeparator", t.allowThousandsSeparator);
        readIf(j, "allowGluedTokens", t.allowGluedTokens);
        readIf(j, "requireStopLoss", t.requireStopLoss);
        readIf(j, "requireTakeProfit", t.requireTakeProfit);
        readIf(j, "maxTargets", t.maxTargets);
        readIf(j, "conflictBehavior", t.conflictBehavior);
        if (j.contains("advancedRules")) {
            for (auto& r : j["advancedRules"]) t.advancedRules.push_back({r.value("field", ""), r.value("pattern", "")});
        }
        auto v = t.validate();
        if (!v) return v.error();
        return t;
    } catch (const std::exception& e) {
        return Error{"template_invalid", std::string("Template JSON is malformed: ") + e.what()};
    }
}

Status ParserTemplate::validate() const {
    if (id.empty() || id.size() > 64) return failStatus("template_invalid", "Template id must be 1-64 characters");
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return failStatus("template_invalid", "Template id may contain only lowercase letters, digits, '_', '-' and '.'");
    if (version.empty()) return failStatus("template_invalid", "Template version is required");
    if (buyWords.empty() || sellWords.empty()) return failStatus("template_invalid", "BUY and SELL keyword lists cannot be empty");
    if (slLabels.empty() && requireStopLoss) return failStatus("template_invalid", "Stop-loss labels are required when SL is mandatory");
    if (tpLabels.empty() && requireTakeProfit) return failStatus("template_invalid", "Take-profit labels are required when TP is mandatory");
    if (maxTargets < 1 || maxTargets > 20) return failStatus("template_invalid", "maxTargets must be between 1 and 20");
    if (conflictBehavior != "review") return failStatus("template_invalid", "conflictBehavior must be 'review' in V1");
    // Keyword lists must not overlap between BUY and SELL, or direction becomes nondeterministic.
    std::set<std::string> buys;
    for (auto& w : buyWords) buys.insert(toUpperAscii(w));
    for (auto& w : sellWords)
        if (buys.count(toUpperAscii(w))) return failStatus("template_invalid", "Keyword '" + w + "' is listed as both BUY and SELL");
    static const std::set<std::string> fields{"symbol", "side", "entry", "sl", "tp", "lot"};
    for (auto& r : advancedRules) {
        if (!fields.count(r.field)) return failStatus("template_invalid", "Advanced rule field '" + r.field + "' is not supported");
        auto re = BoundedRegex::compile(r.pattern);
        if (!re) return failStatus("template_invalid", "Advanced rule for '" + r.field + "': " + re.error().message);
        if (re.value()->groupCount() < 1) return failStatus("template_invalid", "Advanced rule for '" + r.field + "' needs a capture group ( ) around the value");
    }
    for (auto& [k, v] : symbolAliases)
        if (k.empty() || v.empty()) return failStatus("template_invalid", "Symbol aliases cannot be empty");
    return okStatus();
}

namespace {

std::vector<std::vector<std::string>> compilePhrases(const std::vector<std::string>& phrases) {
    std::vector<std::vector<std::string>> out;
    for (auto& p : phrases) {
        auto toks = phraseTokens(p);
        if (!toks.empty()) out.push_back(std::move(toks));
    }
    // Longest phrases first so "STOP LOSS" wins over "STOP" deterministically.
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.size() > b.size(); });
    return out;
}

}  // namespace

Result<std::shared_ptr<const CompiledTemplate>> CompiledTemplate::compile(const ParserTemplate& t) {
    auto v = t.validate();
    if (!v) return v.error();
    auto c = std::make_shared<CompiledTemplate>();
    c->source = t;
    c->buy = compilePhrases(t.buyWords);
    c->sell = compilePhrases(t.sellWords);
    c->limit = compilePhrases(t.limitWords);
    c->stop = compilePhrases(t.stopWords);
    c->now = compilePhrases(t.nowWords);
    c->entry = compilePhrases(t.entryLabels);
    c->sl = compilePhrases(t.slLabels);
    c->tp = compilePhrases(t.tpLabels);
    c->lot = compilePhrases(t.lotLabels);
    c->ref = compilePhrases(t.referenceLabels);
    c->expiry = compilePhrases(t.expiryLabels);
    c->management = compilePhrases(t.managementPhrases);
    for (auto& w : t.commentaryWords) {
        auto toks = phraseTokens(w);
        if (toks.size() == 1) c->commentary.push_back(toks[0]);
    }
    for (auto& [k, val] : t.symbolAliases) {
        std::string key;
        for (auto& tok : phraseTokens(k)) key += tok;
        c->aliases[key] = toUpperAscii(val);
    }
    for (auto& r : t.advancedRules) {
        auto re = BoundedRegex::compile(r.pattern);
        if (!re) return re.error();
        c->rules.push_back({r.field, re.value()});
    }
    return std::shared_ptr<const CompiledTemplate>(c);
}

ParserTemplate starterGoldTemplate() {
    ParserTemplate t;
    t.id = "gold_provider_v1";
    t.version = "1.0.0";
    t.name = "Gold provider (starter)";
    t.description = "Common XAUUSD layouts: '#GOLD BUY 4300 4295', 'GOLD SELL NOW' with Entry range, numbered TP lines, SL line.";
    t.allowGluedTokens = true;
    return t;
}

ParserTemplate starterForexTemplate() {
    ParserTemplate t;
    t.id = "forex_generic_v1";
    t.version = "1.0.0";
    t.name = "Forex pairs (starter)";
    t.description = "EURUSD-style pairs with Entry/SL/TP labels. Glued tokens disabled.";
    t.allowGluedTokens = false;
    return t;
}

ParserTemplate starterStrictTemplate() {
    ParserTemplate t;
    t.id = "strict_v1";
    t.version = "1.0.0";
    t.name = "Strict labels";
    t.description = "Requires explicit labels; rejects glued tokens and market words.";
    t.allowGluedTokens = false;
    t.nowWords.clear();
    return t;
}

void applyArabicPreset(ParserTemplate& t) {
    for (const char* w : {"شراء", "اشتري"}) t.buyWords.push_back(w);
    for (const char* w : {"بيع", "بع"}) t.sellWords.push_back(w);
    for (const char* w : {"وقف الخسارة", "وقف"}) t.slLabels.push_back(w);
    for (const char* w : {"الهدف", "هدف", "جني الربح"}) t.tpLabels.push_back(w);
    for (const char* w : {"دخول", "سعر الدخول"}) t.entryLabels.push_back(w);
    t.symbolAliases["الذهب"] = "XAUUSD";
    t.symbolAliases["ذهب"] = "XAUUSD";
}

void applyRussianPreset(ParserTemplate& t) {
    for (const char* w : {"ПОКУПКА", "Покупка", "покупка", "КУПИТЬ", "Купить"}) t.buyWords.push_back(w);
    for (const char* w : {"ПРОДАЖА", "Продажа", "продажа", "ПРОДАТЬ", "Продать"}) t.sellWords.push_back(w);
    for (const char* w : {"СТОП", "Стоп", "стоп"}) t.slLabels.push_back(w);
    for (const char* w : {"ТЕЙК", "Тейк", "тейк", "ЦЕЛЬ", "Цель"}) t.tpLabels.push_back(w);
    for (const char* w : {"ВХОД", "Вход", "вход"}) t.entryLabels.push_back(w);
    t.symbolAliases["ЗОЛОТО"] = "XAUUSD";
    t.symbolAliases["Золото"] = "XAUUSD";
}

void applySpanishPreset(ParserTemplate& t) {
    for (const char* w : {"COMPRA", "COMPRAR"}) t.buyWords.push_back(w);
    for (const char* w : {"VENTA", "VENDER"}) t.sellWords.push_back(w);
    for (const char* w : {"ENTRADA"}) t.entryLabels.push_back(w);
    for (const char* w : {"OBJETIVO"}) t.tpLabels.push_back(w);
    t.symbolAliases["ORO"] = "XAUUSD";
}

}  // namespace sr
