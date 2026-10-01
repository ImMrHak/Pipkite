#include "SignalParser.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>

#include "Text.h"
#include "Tokenizer.h"

namespace sr {

namespace {

const std::set<std::string> kIsoCurrencies{"USD", "EUR", "GBP", "JPY", "CHF", "AUD", "NZD", "CAD", "XAU", "XAG", "SGD", "HKD", "NOK", "SEK", "ZAR", "MXN", "TRY", "PLN", "CNH"};
const std::set<std::string> kSeparators{":", "-", "=", "@", ">", "|", "/", ",", "."};
const std::set<std::string> kPipWords{"PIPS", "PIP", "POINTS", "POINT", "PTS", "RR", "R"};

std::string fmt(double v) {
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

struct NumHit {
    double value = 0;
    std::uint32_t nb = 0, ne = 0;  // normalized span
};

struct SymbolHit {
    std::string alias, canonical;
    std::uint32_t nb, ne;
};

struct DirHit {
    Side side;
    std::uint32_t nb, ne;
    int line;
};

struct EntryHit {
    std::vector<NumHit> nums;
    int line;
    bool fromDirection;
};

struct TpHit {
    int index;
    NumHit num;
};

class Extractor {
public:
    Extractor(const CompiledTemplate& t, const NormalizedText& norm, const std::vector<Token>& toks) : t_(t), norm_(norm), toks_(toks) {
        std::vector<int> cur;
        for (int i = 0; i < static_cast<int>(toks.size()); ++i) {
            if (toks[i].type == TokenType::Newline) {
                lines_.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(i);
            }
        }
        lines_.push_back(std::move(cur));
    }

    void run() {
        for (auto& line : lines_) scanLine(line);
    }

    // Collected facts
    std::vector<SymbolHit> symbols;
    std::vector<DirHit> directions;
    std::vector<EntryHit> entries;
    std::vector<NumHit> slValues;
    std::vector<TpHit> tps;
    std::vector<NumHit> lots;
    std::string reference;
    std::optional<int> expiryMinutes;
    std::optional<OrderKind> kind;
    std::uint32_t kindNb = 0, kindNe = 0;
    bool marketIntent = false;
    std::uint32_t nowNb = 0, nowNe = 0;
    int managementHits = 0;
    int commentaryHits = 0;
    int tpLabelsWithoutPrice = 0;
    int slLabelsWithoutValue = 0;
    bool question = false;
    bool gluedRejected = false;
    bool badNumberInValue = false;
    std::vector<std::string> diagnostics;

private:
    const CompiledTemplate& t_;
    const NormalizedText& norm_;
    const std::vector<Token>& toks_;
    std::vector<std::vector<int>> lines_;

    const Token& tk(const std::vector<int>& L, std::size_t i) const { return toks_[L[i]]; }

    std::size_t matchAny(const std::vector<int>& L, std::size_t i, const std::vector<std::vector<std::string>>& phrases) const {
        for (auto& ph : phrases) {
            if (i + ph.size() > L.size()) continue;
            bool ok = true;
            for (std::size_t k = 0; k < ph.size() && ok; ++k) {
                const Token& t = tk(L, i + k);
                if (t.type == TokenType::Number || t.type == TokenType::BadNumber) ok = false;
                else ok = t.text == ph[k];
            }
            if (ok) return ph.size();
        }
        return 0;
    }

    bool isLabelAt(const std::vector<int>& L, std::size_t i) const {
        return matchAny(L, i, t_.sl) || matchAny(L, i, t_.tp) || matchAny(L, i, t_.entry) || matchAny(L, i, t_.lot) ||
               matchAny(L, i, t_.buy) || matchAny(L, i, t_.sell) || matchAny(L, i, t_.management);
    }

    bool isSeparator(const Token& t) const { return t.type == TokenType::Punct && kSeparators.count(t.text); }

    std::optional<std::string> symbolFor(const std::string& text) const {
        auto it = t_.aliases.find(text);
        if (it != t_.aliases.end()) return it->second;
        if (t_.source.recognizeCurrencyPairs && text.size() == 6) {
            bool letters = std::all_of(text.begin(), text.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
            if (letters && kIsoCurrencies.count(text.substr(0, 3)) && kIsoCurrencies.count(text.substr(3, 3)) && text.substr(0, 3) != text.substr(3, 3))
                return text;
        }
        return std::nullopt;
    }

    // Tries to match a symbol at i using glued runs (e.g. "US"+"30"). Returns tokens consumed.
    std::size_t matchSymbol(const std::vector<int>& L, std::size_t i) {
        const Token& first = tk(L, i);
        if (first.type != TokenType::Word) return 0;
        std::size_t runEnd = i + 1;
        while (runEnd < L.size() && runEnd - i < 3 && tk(L, runEnd).glued && (tk(L, runEnd).type == TokenType::Word || tk(L, runEnd).type == TokenType::Number))
            ++runEnd;
        for (std::size_t e = runEnd; e > i; --e) {
            std::string joined;
            for (std::size_t k = i; k < e; ++k) joined += tk(L, k).text;
            if (auto canon = symbolFor(joined)) {
                symbols.push_back({joined, *canon, first.begin, tk(L, e - 1).end});
                return e - i;
            }
        }
        return 0;
    }

    std::optional<NumHit> numberAt(const std::vector<int>& L, std::size_t i) {
        const Token& t = tk(L, i);
        if (t.type == TokenType::Number) return NumHit{t.number, t.begin, t.end};
        if (t.type == TokenType::BadNumber) {
            badNumberInValue = true;
            diagnostics.push_back("Ambiguous or unsupported number format '" + t.text + "'");
        }
        return std::nullopt;
    }

    void checkGlue(const Token& number, const char* what) {
        if (number.glued && !t_.source.allowGluedTokens) {
            gluedRejected = true;
            diagnostics.push_back(std::string("Missing whitespace between ") + what + " and number is not accepted by template '" + t_.source.id + "'");
        }
    }

    std::size_t skipSeparators(const std::vector<int>& L, std::size_t j) const {
        while (j < L.size() && isSeparator(tk(L, j))) ++j;
        return j;
    }

    bool followedByPipWord(const std::vector<int>& L, std::size_t j) const {
        return j + 1 < L.size() && tk(L, j + 1).type == TokenType::Word && kPipWords.count(tk(L, j + 1).text);
    }

    void scanLine(const std::vector<int>& L) {
        std::size_t i = 0;
        while (i < L.size()) {
            const Token& t = tk(L, i);
            if (t.type == TokenType::Punct && t.text == "?") { question = true; ++i; continue; }
            if (std::size_t n = matchAny(L, i, t_.management)) { ++managementHits; i += n; continue; }
            if (std::size_t n = matchAny(L, i, t_.buy)) { i = scanDirection(L, i, n, Side::Buy); continue; }
            if (std::size_t n = matchAny(L, i, t_.sell)) { i = scanDirection(L, i, n, Side::Sell); continue; }
            if (std::size_t n = matchAny(L, i, t_.sl)) { i = scanStopLoss(L, i + n); continue; }
            if (std::size_t n = matchAny(L, i, t_.tp)) { i = scanTakeProfit(L, i + n, tk(L, i + n - 1)); continue; }
            if (std::size_t n = matchAny(L, i, t_.entry)) { i = scanEntryLabel(L, i + n); continue; }
            if (std::size_t n = matchAny(L, i, t_.lot)) {
                std::size_t j = skipSeparators(L, i + n);
                if (j < L.size()) {
                    if (auto num = numberAt(L, j)) { lots.push_back(*num); ++j; }
                }
                i = j;
                continue;
            }
            if (std::size_t n = matchAny(L, i, t_.ref)) {
                std::size_t j = skipSeparators(L, i + n);
                while (j < L.size() && tk(L, j).type == TokenType::Punct && tk(L, j).text == "#") ++j;
                if (j < L.size() && tk(L, j).type != TokenType::Punct) { reference = tk(L, j).text; ++j; }
                i = j;
                continue;
            }
            if (std::size_t n = matchAny(L, i, t_.expiry)) {
                std::size_t j = skipSeparators(L, i + n);
                if (j < L.size() && tk(L, j).type == TokenType::Number && tk(L, j).isInteger) {
                    int v = static_cast<int>(tk(L, j).number);
                    int mult = 1;
                    if (j + 1 < L.size() && tk(L, j + 1).type == TokenType::Word) {
                        const std::string& u = tk(L, j + 1).text;
                        if (u == "H" || u == "HR" || u == "HRS" || u == "HOUR" || u == "HOURS") mult = 60;
                        ++j;
                    }
                    if (v > 0 && v <= 7 * 24 * 60) expiryMinutes = v * mult;
                    ++j;
                }
                i = j;
                continue;
            }
            if (std::size_t n = matchAny(L, i, t_.now)) {
                if (!marketIntent) { nowNb = t.begin; nowNe = tk(L, i + n - 1).end; }
                marketIntent = true;
                i += n;
                continue;
            }
            if (std::size_t n = matchSymbol(L, i)) { i += n; continue; }
            if (t.type == TokenType::Word && std::find(t_.commentary.begin(), t_.commentary.end(), t.text) != t_.commentary.end()) ++commentaryHits;
            if (t.type == TokenType::BadNumber) diagnostics.push_back("Ignored ambiguous number '" + t.text + "'");
            ++i;
        }
    }

    std::size_t scanDirection(const std::vector<int>& L, std::size_t i, std::size_t n, Side side) {
        directions.push_back({side, tk(L, i).begin, tk(L, i + n - 1).end, tk(L, i).line});
        std::size_t j = i + n;
        // Order kind must follow the direction keyword directly. Never inferred.
        if (j < L.size() && !matchAny(L, j, t_.sl)) {
            if (std::size_t m = matchAny(L, j, t_.limit)) {
                kind = OrderKind::Limit;
                kindNb = tk(L, j).begin;
                kindNe = tk(L, j + m - 1).end;
                j += m;
            } else if (std::size_t m2 = matchAny(L, j, t_.stop)) {
                kind = OrderKind::Stop;
                kindNb = tk(L, j).begin;
                kindNe = tk(L, j + m2 - 1).end;
                j += m2;
            }
        }
        EntryHit eh{{}, tk(L, i).line, true};
        bool firstNumber = true;
        while (j < L.size()) {
            const Token& t = tk(L, j);
            if (t.type == TokenType::Number || t.type == TokenType::BadNumber) {
                if (followedByPipWord(L, j)) { j += 2; continue; }
                if (firstNumber) checkGlue(t, "direction");
                firstNumber = false;
                if (auto num = numberAt(L, j)) eh.nums.push_back(*num);
                ++j;
                continue;
            }
            if (t.type == TokenType::Punct) {
                if (t.text == "?") { question = true; ++j; continue; }
                if (isSeparator(t) || t.text == "(" || t.text == ")" || t.text == "#" || t.text == "[" || t.text == "]") { ++j; continue; }
                break;
            }
            if (isLabelAt(L, j) && !matchAny(L, j, t_.entry)) break;
            if (std::size_t m = matchAny(L, j, t_.now)) {
                if (!marketIntent) { nowNb = t.begin; nowNe = tk(L, j + m - 1).end; }
                marketIntent = true;
                j += m;
                continue;
            }
            if (std::size_t m = matchAny(L, j, t_.entry)) { j += m; continue; }  // "BUY ENTRY 4300"
            if (std::size_t m = matchSymbol(L, j)) { j += m; continue; }
            if (t.type == TokenType::Word && (t.text == "TO" || t.text == "AT")) { ++j; continue; }
            break;
        }
        if (!eh.nums.empty()) entries.push_back(std::move(eh));
        return j;
    }

    std::size_t scanStopLoss(const std::vector<int>& L, std::size_t j) {
        const std::size_t labelEnd = j;
        j = skipSeparators(L, j);
        if (j < L.size() && (tk(L, j).type == TokenType::Number || tk(L, j).type == TokenType::BadNumber)) {
            if (j == labelEnd) checkGlue(tk(L, j), "SL label");
            if (auto num = numberAt(L, j)) slValues.push_back(*num);
            ++j;
            if (j < L.size() && tk(L, j).type == TokenType::Word && kPipWords.count(tk(L, j).text)) ++j;
        } else {
            ++slLabelsWithoutValue;
        }
        return j;
    }

    std::size_t scanTakeProfit(const std::vector<int>& L, std::size_t j, const Token& label) {
        (void)label;
        int index = 0;
        // "TP1 4305", "TP 1: 4305", "TP¹ 4305" (normalized to TP1). Index only when a price follows.
        if (j < L.size() && tk(L, j).type == TokenType::Number && tk(L, j).isInteger && tk(L, j).number >= 1 && tk(L, j).number <= 20) {
            std::size_t k = skipSeparators(L, j + 1);
            if (k < L.size() && (tk(L, k).type == TokenType::Number || tk(L, k).type == TokenType::BadNumber)) {
                index = static_cast<int>(tk(L, j).number);
                j = j + 1;
            } else if (k >= L.size() || tk(L, k).type == TokenType::Word) {
                // "TP1 HIT" / "TP1" alone: numbered label without a price.
                ++tpLabelsWithoutPrice;
                return j + 1;
            }
        }
        j = skipSeparators(L, j);
        if (j < L.size() && (tk(L, j).type == TokenType::Number || tk(L, j).type == TokenType::BadNumber)) {
            if (auto num = numberAt(L, j)) {
                if (followedByPipWord(L, j)) {
                    diagnostics.push_back("TP given in pips is informational only and was not used as a price");
                    return j + 2;
                }
                tps.push_back({index, *num});
            }
            ++j;
            // Unnumbered lists on one line: "TP: 4305 - 4310 - 4315"
            if (index == 0) {
                while (j + 1 < L.size() && isSeparator(tk(L, j)) && tk(L, j + 1).type == TokenType::Number && !followedByPipWord(L, j + 1)) {
                    tps.push_back({0, NumHit{tk(L, j + 1).number, tk(L, j + 1).begin, tk(L, j + 1).end}});
                    j += 2;
                }
            }
            if (j < L.size() && tk(L, j).type == TokenType::Word && kPipWords.count(tk(L, j).text)) ++j;
        } else {
            ++tpLabelsWithoutPrice;
        }
        return j;
    }

    std::size_t scanEntryLabel(const std::vector<int>& L, std::size_t j) {
        EntryHit eh{{}, L.empty() ? 0 : tk(L, 0).line, false};
        while (j < L.size() && eh.nums.size() < 3) {
            const Token& t = tk(L, j);
            if (t.type == TokenType::Number || t.type == TokenType::BadNumber) {
                if (followedByPipWord(L, j)) break;
                if (auto num = numberAt(L, j)) eh.nums.push_back(*num);
                ++j;
                continue;
            }
            if (isSeparator(t) || (t.type == TokenType::Word && (t.text == "TO" || t.text == "AT" || t.text == "AND"))) { ++j; continue; }
            if (std::size_t m = matchAny(L, j, t_.now)) {
                if (!marketIntent) { nowNb = t.begin; nowNe = tk(L, j + m - 1).end; }
                marketIntent = true;
                j += m;
                continue;
            }
            break;
        }
        if (!eh.nums.empty()) entries.push_back(std::move(eh));
        return j;
    }
};

void addEvidence(Signal& s, const NormalizedText& norm, const std::string& field, std::uint32_t nb, std::uint32_t ne) {
    if (ne <= nb) return;
    s.evidence.push_back({field, norm.toOriginal(nb, ne)});
}

void finishTiming(Signal& s, std::chrono::steady_clock::time_point start) {
    s.parseMicros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
}

}  // namespace

std::string checkGeometry(const Signal& s) {
    if (!s.side) return {};
    const bool buy = *s.side == Side::Buy;
    auto bad = [](double v) { return !(std::isfinite(v) && v > 0); };
    if (s.stopLoss && bad(*s.stopLoss)) return "Stop loss must be a positive finite price";
    for (auto& tp : s.targets)
        if (bad(tp.price)) return "Take profit must be a positive finite price";
    if (s.entry) {
        if (bad(s.entry->low) || bad(s.entry->high)) return "Entry must be a positive finite price";
        if (s.stopLoss) {
            if (buy && !(*s.stopLoss < s.entry->low)) return "BUY stop loss " + fmt(*s.stopLoss) + " must be below entry " + fmt(s.entry->low);
            if (!buy && !(*s.stopLoss > s.entry->high)) return "SELL stop loss " + fmt(*s.stopLoss) + " must be above entry " + fmt(s.entry->high);
        }
        for (auto& tp : s.targets) {
            if (buy && !(tp.price > s.entry->high)) return "BUY take profit " + fmt(tp.price) + " must be above entry " + fmt(s.entry->high);
            if (!buy && !(tp.price < s.entry->low)) return "SELL take profit " + fmt(tp.price) + " must be below entry " + fmt(s.entry->low);
        }
    } else if (s.stopLoss) {
        for (auto& tp : s.targets) {
            if (buy && !(tp.price > *s.stopLoss)) return "BUY take profit " + fmt(tp.price) + " must be above stop loss " + fmt(*s.stopLoss);
            if (!buy && !(tp.price < *s.stopLoss)) return "SELL take profit " + fmt(tp.price) + " must be below stop loss " + fmt(*s.stopLoss);
        }
    }
    return {};
}

Signal TemplateSignalParser::parse(std::string_view rawText, const ParseContext& ctx) const { return parseWithTemplate(rawText, *t_, ctx); }

Signal parseWithTemplate(std::string_view raw, const CompiledTemplate& t, const ParseContext& ctx) {
    const auto start = std::chrono::steady_clock::now();
    Signal s;
    s.source = ctx.source;
    s.revision = ctx.revision;
    s.publishedAtMs = ctx.publishedAtMs;
    s.receivedAtMs = ctx.receivedAtMs;
    s.parserId = t.source.id;
    s.parserVersion = t.source.version;

    auto finish = [&](ParseStatus st, MessageKind k, std::string diag = {}) {
        s.status = st;
        s.kind = k;
        if (!diag.empty()) s.diagnostics.push_back(std::move(diag));
        finishTiming(s, start);
        return s;
    };

    if (raw.size() > kMaxMessageBytes) {
        s.rawText.assign(raw.substr(0, 256));
        return finish(ParseStatus::Invalid, MessageKind::TradeSetup, "Message exceeds the 64 KiB limit");
    }
    s.rawText.assign(raw);
    if (!isValidUtf8(raw)) return finish(ParseStatus::Invalid, MessageKind::TradeSetup, "Message is not valid UTF-8");

    const NormalizedText norm = normalize(raw);
    {
        std::size_t lineStart = 0;
        for (std::size_t k = 0; k <= norm.text.size(); ++k) {
            if (k == norm.text.size() || norm.text[k] == '\n') {
                if (k - lineStart > kMaxLineBytes) return finish(ParseStatus::Invalid, MessageKind::TradeSetup, "A line exceeds the 4096-byte limit");
                lineStart = k + 1;
            }
        }
    }
    TokenizeOptions opt;
    opt.numberFormat = t.source.numberFormat;
    opt.allowThousandsSeparator = t.source.allowThousandsSeparator;
    auto tr = tokenize(norm.text, opt, kMaxTokens);
    if (tr.truncated) return finish(ParseStatus::Invalid, MessageKind::TradeSetup, "Message exceeds the token limit");
    const bool anyToken = std::any_of(tr.tokens.begin(), tr.tokens.end(), [](const Token& k) { return k.type != TokenType::Newline; });
    if (!anyToken) {
        if (ctx.hasMedia) return finish(ParseStatus::Unsupported, MessageKind::NonText, "Media without text is shown for information only and cannot execute");
        return finish(ParseStatus::Unsupported, MessageKind::Empty, "Empty message");
    }

    Extractor ex(t, norm, tr.tokens);
    ex.run();
    for (auto& d : ex.diagnostics) s.diagnostics.push_back(d);
    if (ctx.hasMedia) s.diagnostics.push_back("Attached media is ignored; only the text/caption was parsed");

    // Advanced bounded-regex rules fill missing fields; disagreements require review.
    bool advancedConflict = false;
    std::optional<Side> advSide;
    std::optional<double> advSl, advEntry, advLot;
    std::optional<std::pair<std::string, Span>> advSymbol;
    std::vector<std::pair<double, Span>> advTps;
    for (auto& rule : t.rules) {
        auto m = rule.re->search(norm.text);
        if (!m || m->groups.size() < 2 || !m->groups[1]) continue;
        auto [gb, ge] = *m->groups[1];
        std::string value = trim(std::string_view(norm.text).substr(gb, ge - gb));
        Span span = norm.toOriginal(static_cast<std::uint32_t>(gb), static_cast<std::uint32_t>(ge));
        if (rule.field == "side") {
            auto toks = phraseTokens(value);
            for (auto& ph : t.buy) if (toks == ph) advSide = Side::Buy;
            for (auto& ph : t.sell) if (toks == ph) advSide = Side::Sell;
            if (advSide) s.evidence.push_back({"side", span});
        } else if (rule.field == "symbol") {
            std::string key;
            for (auto& tok : phraseTokens(value)) key += tok;
            auto it = t.aliases.find(key);
            advSymbol = std::make_pair(it != t.aliases.end() ? it->second : key, span);
        } else {
            bool isInt = false;
            auto num = parseStrictNumber(value, opt, &isInt);
            if (!num) {
                s.diagnostics.push_back("Advanced rule for " + rule.field + " captured a non-numeric value '" + value + "'");
                advancedConflict = true;
                continue;
            }
            if (rule.field == "sl") advSl = *num;
            else if (rule.field == "entry") advEntry = *num;
            else if (rule.field == "lot") advLot = *num;
            else if (rule.field == "tp") advTps.push_back({*num, span});
            s.evidence.push_back({rule.field == "tp" ? "tp" : rule.field, span});
        }
    }

    // Classification
    std::set<Side> sides;
    for (auto& d : ex.directions) sides.insert(d.side);
    if (advSide) sides.insert(*advSide);
    const bool hasNumbers = !ex.slValues.empty() || !ex.tps.empty() || !ex.entries.empty() || advSl || !advTps.empty() || advEntry;

    if (ex.managementHits > 0 && ex.entries.empty() && !advEntry)
        return finish(ParseStatus::Unsupported, MessageKind::Management, "Trade-management message (close/move SL/cancel) is review-only in this version; broker state is authoritative");
    if (sides.size() > 1) {
        if (ex.question || !hasNumbers) return finish(ParseStatus::Unsupported, MessageKind::Commentary, "Question or commentary mentioning both directions; not an entry");
        return finish(ParseStatus::Ambiguous, MessageKind::TradeSetup, "Both BUY and SELL directions found; review required");
    }
    if (sides.empty()) {
        if (hasNumbers) return finish(ParseStatus::Incomplete, MessageKind::TradeSetup, "No trade direction found (possibly a reply fragment); incomplete entries are never executed automatically");
        return finish(ParseStatus::Unsupported, MessageKind::Commentary, ex.tpLabelsWithoutPrice > 0 ? "Result/commentary message (e.g. 'TP hit'); provider claims are not broker fills" : "Commentary; no trade setup found");
    }
    if (ex.question && ex.slValues.empty() && ex.tps.empty())
        return finish(ParseStatus::Unsupported, MessageKind::Commentary, "Question without trade levels; not an entry");

    s.kind = MessageKind::TradeSetup;
    s.side = *sides.begin();
    if (!ex.directions.empty()) addEvidence(s, norm, "side", ex.directions.front().nb, ex.directions.front().ne);
    if (ex.kind) {
        s.orderKind = *ex.kind;
        s.explicitKind = true;
        addEvidence(s, norm, "kind", ex.kindNb, ex.kindNe);
    }
    if (ex.marketIntent) {
        s.marketIntent = true;
        addEvidence(s, norm, "now", ex.nowNb, ex.nowNe);
    }

    std::vector<std::string> ambiguity;

    // Symbols: every mention must agree on the canonical symbol.
    std::set<std::string> canon;
    for (auto& sh : ex.symbols) canon.insert(sh.canonical);
    if (advSymbol) {
        if (!canon.empty() && !canon.count(advSymbol->first)) advancedConflict = true;
        canon.insert(advSymbol->first);
    }
    if (canon.size() > 1) {
        std::string list;
        for (auto& c : canon) list += (list.empty() ? "" : ", ") + c;
        ambiguity.push_back("Contradictory symbols: " + list);
    } else if (canon.size() == 1) {
        s.symbol = *canon.begin();
        if (!ex.symbols.empty()) {
            s.symbolAlias = ex.symbols.front().alias;
            addEvidence(s, norm, "symbol", ex.symbols.front().nb, ex.symbols.front().ne);
        } else if (advSymbol) {
            s.symbolAlias = advSymbol->first;
            s.evidence.push_back({"symbol", advSymbol->second});
        }
    }

    // Multiple same-side direction lines each carrying their own entry -> multiple setups.
    int directionEntries = 0;
    for (auto& e : ex.entries)
        if (e.fromDirection) ++directionEntries;
    if (directionEntries > 1) ambiguity.push_back("More than one trade setup in one message; only one setup per message is supported");

    // Entry
    std::vector<NumHit> entryNums;
    std::uint32_t enb = 0, ene = 0;
    {
        std::optional<PriceRange> fromDir, fromLabel;
        for (auto& e : ex.entries) {
            if (e.nums.size() > 2) {
                ambiguity.push_back("Entry has more than two values");
                continue;
            }
            double lo = e.nums[0].value, hi = e.nums[0].value;
            for (auto& n : e.nums) { lo = std::min(lo, n.value); hi = std::max(hi, n.value); }
            auto& target = e.fromDirection ? fromDir : fromLabel;
            if (target && (target->low != lo || target->high != hi)) ambiguity.push_back("Conflicting entry values");
            target = PriceRange{lo, hi};
            if (enb == 0 && ene == 0) { enb = e.nums.front().nb; ene = e.nums.back().ne; }
        }
        if (fromDir && fromLabel && (fromDir->low != fromLabel->low || fromDir->high != fromLabel->high))
            ambiguity.push_back("Entry on the direction line conflicts with the Entry label");
        if (fromLabel) s.entry = fromLabel;
        else if (fromDir) s.entry = fromDir;
        if (advEntry) {
            if (s.entry && !(s.entry->low <= *advEntry && *advEntry <= s.entry->high)) advancedConflict = true;
            if (!s.entry) s.entry = PriceRange{*advEntry, *advEntry};
        }
        if (s.entry) addEvidence(s, norm, "entry", enb, ene);
    }

    // Stop loss: duplicate conflicting values require review.
    {
        std::set<double> distinct;
        for (auto& v : ex.slValues) distinct.insert(v.value);
        if (advSl) {
            if (!distinct.empty() && !distinct.count(*advSl)) advancedConflict = true;
            distinct.insert(*advSl);
        }
        if (distinct.size() > 1) ambiguity.push_back("Conflicting stop-loss values");
        else if (distinct.size() == 1) {
            s.stopLoss = *distinct.begin();
            if (!ex.slValues.empty()) addEvidence(s, norm, "sl", ex.slValues.front().nb, ex.slValues.front().ne);
        }
    }

    // Take profits: numbered labels must not conflict; preserve provider order.
    {
        std::map<int, double> byIndex;
        std::vector<TpHit> ordered;
        for (auto& tp : ex.tps) {
            if (tp.index > 0) {
                auto it = byIndex.find(tp.index);
                if (it != byIndex.end()) {
                    if (it->second != tp.num.value) ambiguity.push_back("Conflicting values for TP" + std::to_string(tp.index));
                    continue;
                }
                byIndex[tp.index] = tp.num.value;
            }
            ordered.push_back(tp);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](const TpHit& a, const TpHit& b) {
            if (a.index == 0 || b.index == 0) return false;
            return a.index < b.index;
        });
        int expect = 1;
        for (auto& tp : ordered) {
            if (tp.index > 0 && tp.index != expect) s.diagnostics.push_back("TP numbering has a gap or starts above 1; targets are selected by position");
            expect = (tp.index > 0 ? tp.index : expect) + 1;
            TakeProfit out;
            out.index = tp.index;
            out.price = tp.num.value;
            out.span = norm.toOriginal(tp.num.nb, tp.num.ne);
            s.targets.push_back(out);
        }
        for (auto& [v, span] : advTps) {
            bool seen = std::any_of(s.targets.begin(), s.targets.end(), [&](const TakeProfit& x) { return x.price == v; });
            if (!seen) s.targets.push_back({0, v, span});
        }
        for (std::size_t k = 0; k < s.targets.size(); ++k) s.evidence.push_back({"tp" + std::to_string(k + 1), s.targets[k].span});
        if (static_cast<int>(s.targets.size()) > t.source.maxTargets)
            return finish(ParseStatus::Invalid, MessageKind::TradeSetup, "More take-profit levels than the template allows (" + std::to_string(t.source.maxTargets) + ")");
    }

    if (!ex.lots.empty()) {
        s.lot = ex.lots.front().value;
        addEvidence(s, norm, "lot", ex.lots.front().nb, ex.lots.front().ne);
    } else if (advLot) {
        s.lot = advLot;
    }
    s.reference = ex.reference;
    s.expiryMinutes = ex.expiryMinutes;

    if (advancedConflict) ambiguity.push_back("Advanced rule result conflicts with the standard extraction");
    if (ex.badNumberInValue) ambiguity.push_back("Ambiguous number format in a price field");

    if (ex.gluedRejected) {
        for (auto& a : ambiguity) s.diagnostics.push_back(a);
        return finish(ParseStatus::Unsupported, MessageKind::TradeSetup);
    }
    if (!ambiguity.empty()) {
        for (auto& a : ambiguity) s.diagnostics.push_back(a);
        return finish(ParseStatus::Ambiguous, MessageKind::TradeSetup);
    }

    std::vector<std::string> missing;
    if (s.symbol.empty()) missing.push_back("symbol");
    if (!s.entry && !s.marketIntent) missing.push_back("entry price or explicit NOW");
    if (t.source.requireStopLoss && !s.stopLoss) missing.push_back("stop loss");
    if (t.source.requireTakeProfit && s.targets.empty()) missing.push_back("take profit");
    if (!missing.empty()) {
        std::string m = "Missing required fields: ";
        for (std::size_t k = 0; k < missing.size(); ++k) m += (k ? ", " : "") + missing[k];
        return finish(ParseStatus::Incomplete, MessageKind::TradeSetup, m);
    }
    if (s.explicitKind && s.marketIntent) return finish(ParseStatus::Ambiguous, MessageKind::TradeSetup, "Both a pending order type and NOW/market intent were given");

    std::string geo = checkGeometry(s);
    if (!geo.empty()) return finish(ParseStatus::Invalid, MessageKind::TradeSetup, geo);

    return finish(ParseStatus::Valid, MessageKind::TradeSetup);
}

}  // namespace sr
