#include "ParserBuilder.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "JsonModels.h"
#include "Text.h"
#include "Tokenizer.h"

namespace sr {

using nlohmann::json;

// learning

namespace {

bool containsPhrase(const std::vector<std::string>& list, const std::string& phrase) {
    auto target = phraseTokens(phrase);
    for (auto& p : list)
        if (phraseTokens(p) == target) return true;
    return false;
}

// Returns the up-to-3 word tokens immediately before the value on the same line.
std::string precedingLabel(const std::vector<Token>& toks, std::size_t valueIdx, bool skipIndex) {
    if (valueIdx == 0) return {};
    std::size_t i = valueIdx;
    const int line = toks[valueIdx].line;
    auto prev = [&]() -> const Token* { return i > 0 ? &toks[i - 1] : nullptr; };
    while (prev() && prev()->line == line && prev()->type == TokenType::Punct) --i;
    if (skipIndex && prev() && prev()->line == line && prev()->type == TokenType::Number && prev()->isInteger && prev()->number <= 20) --i;
    std::vector<std::string> words;
    while (prev() && prev()->line == line && prev()->type == TokenType::Word && words.size() < 3) {
        words.insert(words.begin(), prev()->text);
        --i;
        // Stop at the first gap if the preceding token is punctuation like '/' inside "S/L".
        if (prev() && prev()->line == line && prev()->type == TokenType::Punct && prev()->text == "/" && i >= 2 && toks[i - 2].type == TokenType::Word) {
            words.insert(words.begin(), "/");
            words.insert(words.begin(), toks[i - 2].text);
            break;
        }
    }
    std::string out;
    for (auto& w : words) {
        if (!out.empty() && w != "/" && out.back() != '/') out += ' ';
        out += w;
    }
    return out;
}

}  // namespace

LearnResult learnFromHighlights(const ParserTemplate& base, const std::string& sample, const std::vector<HighlightLabel>& labels) {
    LearnResult r;
    r.updated = base;
    auto norm = normalize(sample);
    TokenizeOptions opt;
    opt.numberFormat = base.numberFormat;
    opt.allowThousandsSeparator = base.allowThousandsSeparator;
    auto toks = tokenize(norm.text, opt, kMaxTokens).tokens;

    auto tokenAt = [&](std::uint32_t origBegin) -> std::optional<std::size_t> {
        for (std::size_t k = 0; k < toks.size(); ++k) {
            if (toks[k].type == TokenType::Newline) continue;
            Span sp = norm.toOriginal(toks[k].begin, toks[k].end);
            if (sp.begin <= origBegin && origBegin < sp.end) return k;
        }
        return std::nullopt;
    };

    for (auto& lab : labels) {
        if (lab.end <= lab.begin || lab.end > sample.size()) {
            r.warnings.push_back("Ignored a highlight outside the sample text");
            continue;
        }
        std::string text = trim(std::string_view(sample).substr(lab.begin, lab.end - lab.begin));
        if (text.empty()) continue;
        if (lab.field == "buy" || lab.field == "sell") {
            auto& list = lab.field == "buy" ? r.updated.buyWords : r.updated.sellWords;
            auto& other = lab.field == "buy" ? r.updated.sellWords : r.updated.buyWords;
            if (containsPhrase(other, text)) {
                r.warnings.push_back("'" + text + "' is already a " + (lab.field == "buy" ? "SELL" : "BUY") + " keyword; not added");
                continue;
            }
            if (!containsPhrase(list, text)) {
                list.push_back(toUpperAscii(text));
                r.learned.push_back("Added " + toUpperAscii(lab.field) + " keyword '" + toUpperAscii(text) + "'");
            }
            continue;
        }
        if (lab.field == "symbol") {
            std::string key;
            for (auto& t : phraseTokens(text)) key += t;
            std::string canonical = lab.canonical.empty() ? key : toUpperAscii(lab.canonical);
            auto it = r.updated.symbolAliases.find(key);
            if (it == r.updated.symbolAliases.end() || it->second != canonical) {
                r.updated.symbolAliases[key] = canonical;
                r.learned.push_back("Mapped symbol alias '" + key + "' -> " + canonical);
            }
            continue;
        }
        if (lab.field != "entry" && lab.field != "sl" && lab.field != "tp" && lab.field != "lot") {
            r.warnings.push_back("Unknown field '" + lab.field + "'");
            continue;
        }
        bool isInt = false;
        if (!parseStrictNumber(text, opt, &isInt)) {
            r.warnings.push_back("Highlighted " + lab.field + " value '" + text + "' is not a number in the template's number format");
            continue;
        }
        auto idx = tokenAt(lab.begin);
        if (!idx) continue;
        std::string label = precedingLabel(toks, *idx, lab.field == "tp");
        if (label.empty()) {
            if (lab.field != "entry") r.warnings.push_back("No label text found before the " + lab.field + " value; add a label manually");
            continue;
        }
        if (lab.field == "entry" && (containsPhrase(r.updated.buyWords, label) || containsPhrase(r.updated.sellWords, label))) continue;  // direction-line entry
        auto& list = lab.field == "entry" ? r.updated.entryLabels : lab.field == "sl" ? r.updated.slLabels : lab.field == "tp" ? r.updated.tpLabels : r.updated.lotLabels;
        if (!containsPhrase(list, label)) {
            list.push_back(label);
            r.learned.push_back("Added " + toUpperAscii(lab.field) + " label '" + label + "'");
        }
    }
    auto v = r.updated.validate();
    if (!v) r.warnings.push_back(v.error().message);
    return r;
}

// fixtures

json Fixture::toJson() const {
    json j = {{"id", id}, {"text", text}, {"expect", expect}};
    if (side) j["side"] = *side;
    if (symbol) j["symbol"] = *symbol;
    if (entryLow) j["entryLow"] = *entryLow;
    if (entryHigh) j["entryHigh"] = *entryHigh;
    if (stopLoss) j["stopLoss"] = *stopLoss;
    if (!takeProfits.empty()) j["takeProfits"] = takeProfits;
    return j;
}

Fixture Fixture::fromJson(const json& j) {
    Fixture f;
    f.id = j.value("id", "");
    f.text = j.value("text", "");
    f.expect = j.value("expect", "signal");
    if (j.contains("side") && j["side"].is_string()) f.side = j["side"].get<std::string>();
    if (j.contains("symbol") && j["symbol"].is_string()) f.symbol = j["symbol"].get<std::string>();
    if (j.contains("entryLow") && j["entryLow"].is_number()) f.entryLow = j["entryLow"].get<double>();
    if (j.contains("entryHigh") && j["entryHigh"].is_number()) f.entryHigh = j["entryHigh"].get<double>();
    if (j.contains("stopLoss") && j["stopLoss"].is_number()) f.stopLoss = j["stopLoss"].get<double>();
    if (j.contains("takeProfits") && j["takeProfits"].is_array()) f.takeProfits = j["takeProfits"].get<std::vector<double>>();
    return f;
}

json SuiteResult::toJson() const {
    json arr = json::array();
    for (auto& r : results)
        arr.push_back({{"id", r.id}, {"passed", r.passed}, {"actual", r.actualOutcome}, {"status", r.status}, {"differences", r.differences}, {"micros", r.micros}});
    return {{"passed", passed}, {"failed", failed}, {"worstMicros", worstMicros}, {"results", arr}};
}

std::string outcomeOf(const Signal& s) {
    if (s.status == ParseStatus::Valid) return "signal";
    if (s.status == ParseStatus::Unsupported && (s.kind == MessageKind::Commentary || s.kind == MessageKind::Empty || s.kind == MessageKind::NonText)) return "ignore";
    return "review";
}

namespace {

bool near(double a, double b) { return std::fabs(a - b) < 1e-9 * std::max(1.0, std::fabs(a)); }

std::string numStr(double v) {
    std::string s = std::to_string(v);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

}  // namespace

SuiteResult runFixtures(const ISignalParser& parser, const std::vector<Fixture>& fixtures) {
    SuiteResult suite;
    for (auto& f : fixtures) {
        FixtureResult r;
        r.id = f.id;
        ParseContext ctx;
        Signal s = parser.parse(f.text, ctx);
        r.micros = s.parseMicros;
        r.actualOutcome = outcomeOf(s);
        r.status = toString(s.status);
        if (r.actualOutcome != f.expect) r.differences.push_back("outcome: expected " + f.expect + ", got " + r.actualOutcome + (s.diagnostics.empty() ? "" : " (" + s.diagnostics.back() + ")"));
        if (f.expect == "signal" && r.actualOutcome == "signal") {
            if (f.side && (!s.side || *f.side != toString(*s.side))) r.differences.push_back("side: expected " + *f.side + ", got " + (s.side ? toString(*s.side) : "none"));
            if (f.symbol && *f.symbol != s.symbol) r.differences.push_back("symbol: expected " + *f.symbol + ", got " + s.symbol);
            if (f.entryLow && (!s.entry || !near(*f.entryLow, s.entry->low))) r.differences.push_back("entry low: expected " + numStr(*f.entryLow) + ", got " + (s.entry ? numStr(s.entry->low) : "none"));
            if (f.entryHigh && (!s.entry || !near(*f.entryHigh, s.entry->high))) r.differences.push_back("entry high: expected " + numStr(*f.entryHigh) + ", got " + (s.entry ? numStr(s.entry->high) : "none"));
            if (f.stopLoss && (!s.stopLoss || !near(*f.stopLoss, *s.stopLoss))) r.differences.push_back("SL: expected " + numStr(*f.stopLoss) + ", got " + (s.stopLoss ? numStr(*s.stopLoss) : "none"));
            if (!f.takeProfits.empty()) {
                bool same = f.takeProfits.size() == s.targets.size();
                for (std::size_t k = 0; same && k < s.targets.size(); ++k) same = near(f.takeProfits[k], s.targets[k].price);
                if (!same) {
                    std::string got;
                    for (auto& t : s.targets) got += (got.empty() ? "" : ",") + numStr(t.price);
                    r.differences.push_back("TPs differ: got [" + got + "]");
                }
            }
        }
        r.passed = r.differences.empty();
        suite.worstMicros = std::max(suite.worstMicros, r.micros);
        (r.passed ? suite.passed : suite.failed)++;
        suite.results.push_back(std::move(r));
    }
    return suite;
}

const std::vector<Fixture>& sharedNegativeCorpus() {
    static const std::vector<Fixture> corpus = [] {
        std::vector<Fixture> v;
        auto add = [&](const char* id, const char* text, const char* expect) {
            Fixture f;
            f.id = id;
            f.text = text;
            f.expect = expect;
            v.push_back(f);
        };
        add("neg_tp1_hit", "TP1 HIT", "ignore");
        add("neg_great_profit", "great profit", "ignore");
        add("neg_buy_or_sell", "buy or sell?", "ignore");
        add("neg_tp_hit_emoji", "✅ TP2 HIT +80 pips 🔥🔥", "ignore");
        add("neg_empty", "   ", "ignore");
        add("neg_good_morning", "Good morning traders! Market opens soon.", "ignore");
        add("neg_close_all", "Close all gold trades now", "review");
        add("neg_move_sl_be", "Move SL to BE", "review");
        add("neg_both_dirs", "GOLD BUY 4300 SELL 4310 SL 4290 TP 4320", "review");
        add("neg_entry_only", "GOLD BUY 4300", "review");
        add("neg_reply_stops", "SL 4290\nTP 4310", "review");
        return v;
    }();
    return corpus;
}

std::vector<Fixture> requiredSpecFixtures() {
    std::vector<Fixture> v;
    Fixture a;
    a.id = "spec_gold_buy_range";
    a.text = "#GOLD BUY 4300 4295\nTP1 4305\nTP2 4310\nTP3 4315\nTP4 4320\nTP5 4325\nTP6 4330\nSL 4287";
    a.side = "BUY";
    a.symbol = "XAUUSD";
    a.entryLow = 4295;
    a.entryHigh = 4300;
    a.stopLoss = 4287;
    a.takeProfits = {4305, 4310, 4315, 4320, 4325, 4330};
    v.push_back(a);
    Fixture b;
    b.id = "spec_gold_sell_now";
    b.text = "GOLD (XAUUSD) Sell NOW\nEntry 4302–4306\nTP1 4296\nTP2 4288\nTP3 4282\nTP4 4276\nSL 4316";
    b.side = "SELL";
    b.symbol = "XAUUSD";
    b.entryLow = 4302;
    b.entryHigh = 4306;
    b.stopLoss = 4316;
    b.takeProfits = {4296, 4288, 4282, 4276};
    v.push_back(b);
    Fixture c;
    c.id = "spec_gold_sell_glued";
    c.text = "GOLD SELL4295\nTP1 4285\nTP2 4280\nSL 4305";
    c.side = "SELL";
    c.symbol = "XAUUSD";
    c.entryLow = 4295;
    c.entryHigh = 4295;
    c.stopLoss = 4305;
    c.takeProfits = {4285, 4280};
    v.push_back(c);
    for (auto& n : sharedNegativeCorpus()) v.push_back(n);
    return v;
}

std::vector<ParseDiff> compareParsers(const ISignalParser& before, const ISignalParser& after, const std::vector<std::string>& samples) {
    std::vector<ParseDiff> out;
    static const char* fields[] = {"status", "side", "symbol", "entry", "stopLoss", "targets"};
    for (auto& text : samples) {
        ParseContext ctx;
        json b = toJson(before.parse(text, ctx));
        json a = toJson(after.parse(text, ctx));
        ParseDiff d;
        d.sample = text;
        for (const char* f : fields) {
            json bv = b[f], av = a[f];
            if (std::string(f) == "targets") {
                std::vector<double> bp, ap;
                for (auto& t : bv) bp.push_back(t["price"].get<double>());
                for (auto& t : av) ap.push_back(t["price"].get<double>());
                bv = bp;
                av = ap;
            }
            if (bv != av) d.changedFields.push_back(f);
        }
        auto pick = [](const json& j) { return json{{"status", j["status"]}, {"side", j["side"]}, {"symbol", j["symbol"]}, {"entry", j["entry"]}, {"stopLoss", j["stopLoss"]}, {"targets", j["targets"]}}; };
        d.before = pick(b);
        d.after = pick(a);
        out.push_back(std::move(d));
    }
    return out;
}

// registry

Result<ParserVersionInfo> ParserRegistry::publish(const ParserTemplate& t, const std::string& origin, std::int64_t nowMs) {
    auto compiled = CompiledTemplate::compile(t);
    if (!compiled) return compiled.error();
    std::unique_lock lock(mu_);
    auto& byVersion = versions_[t.id];
    if (auto it = byVersion.find(t.version); it != byVersion.end()) {
        if (it->second.info.templateJson == t.toJson()) return it->second.info;
        return Error{"version_exists", "Parser " + t.id + " version " + t.version + " already exists with different content; published versions are immutable"};
    }
    Stored st;
    st.info.parserId = t.id;
    st.info.version = t.version;
    st.info.origin = origin;
    st.info.templateJson = t.toJson();
    st.info.createdAtMs = nowMs;
    st.parser = std::make_shared<TemplateSignalParser>(compiled.value());
    byVersion[t.version] = st;
    return st.info;
}

Status ParserRegistry::registerNative(const std::string& parserId, const std::string& version, std::shared_ptr<const ISignalParser> parser, const std::string& origin, std::int64_t nowMs) {
    std::unique_lock lock(mu_);
    auto& byVersion = versions_[parserId];
    if (byVersion.count(version)) return failStatus("version_exists", "Parser " + parserId + " " + version + " is already registered");
    Stored st;
    st.info.parserId = parserId;
    st.info.version = version;
    st.info.origin = origin;
    st.info.createdAtMs = nowMs;
    st.parser = std::move(parser);
    byVersion[version] = st;
    return okStatus();
}

Status ParserRegistry::activate(const std::string& parserId, const std::string& version, std::int64_t nowMs) {
    std::unique_lock lock(mu_);
    auto it = versions_.find(parserId);
    if (it == versions_.end() || !it->second.count(version)) return failStatus("not_found", "Parser " + parserId + " version " + version + " is not published");
    auto& st = it->second[version];
    auto gen = std::make_shared<ParserGeneration>();
    gen->generation = nextGeneration_++;
    gen->parserId = parserId;
    gen->version = version;
    gen->origin = st.info.origin;
    gen->parser = st.parser;
    for (auto& [v, s] : it->second) s.info.active = false;
    st.info.active = true;
    st.info.activatedAtMs = nowMs;
    active_[parserId] = gen;  // atomic switch: readers hold their own shared_ptr lease
    auto& h = history_[parserId];
    if (h.empty() || h.back() != version) h.push_back(version);
    return okStatus();
}

Result<std::string> ParserRegistry::rollback(const std::string& parserId, std::int64_t nowMs) {
    std::string target;
    {
        std::shared_lock lock(mu_);
        auto it = history_.find(parserId);
        if (it == history_.end() || it->second.size() < 2) return Error{"no_previous", "No previous active version to roll back to"};
        target = it->second[it->second.size() - 2];
    }
    auto st = activate(parserId, target, nowMs);
    if (!st) return st.error();
    std::unique_lock lock(mu_);
    auto& h = history_[parserId];
    // Collapse [.., prev, bad, prev] into [.., prev] so repeated rollbacks walk further back.
    if (h.size() >= 3 && h[h.size() - 1] == h[h.size() - 3]) h.erase(h.end() - 2, h.end());
    return target;
}

std::shared_ptr<const ParserGeneration> ParserRegistry::lease(const std::string& parserId) const {
    std::shared_lock lock(mu_);
    auto it = active_.find(parserId);
    return it == active_.end() ? nullptr : it->second;
}

std::shared_ptr<const ParserGeneration> ParserRegistry::leaseVersion(const std::string& parserId, const std::string& version) const {
    std::shared_lock lock(mu_);
    auto it = versions_.find(parserId);
    if (it == versions_.end()) return nullptr;
    auto v = it->second.find(version);
    if (v == it->second.end()) return nullptr;
    auto gen = std::make_shared<ParserGeneration>();
    gen->parserId = parserId;
    gen->version = version;
    gen->origin = v->second.info.origin;
    gen->parser = v->second.parser;
    return gen;
}

std::vector<ParserVersionInfo> ParserRegistry::list() const {
    std::shared_lock lock(mu_);
    std::vector<ParserVersionInfo> out;
    for (auto& [id, byVersion] : versions_)
        for (auto& [v, st] : byVersion) out.push_back(st.info);
    return out;
}

std::vector<std::string> ParserRegistry::activationHistory(const std::string& parserId) const {
    std::shared_lock lock(mu_);
    auto it = history_.find(parserId);
    return it == history_.end() ? std::vector<std::string>{} : it->second;
}

std::optional<ParserTemplate> ParserRegistry::templateOf(const std::string& parserId, const std::string& version) const {
    std::shared_lock lock(mu_);
    auto it = versions_.find(parserId);
    if (it == versions_.end()) return std::nullopt;
    auto v = it->second.find(version);
    if (v == it->second.end() || v->second.info.templateJson.is_null()) return std::nullopt;
    auto t = ParserTemplate::fromJson(v->second.info.templateJson);
    if (!t) return std::nullopt;
    return t.value();
}

}  // namespace sr
