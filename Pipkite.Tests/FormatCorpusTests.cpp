// Real-world signal layouts against the formats shipped with the app. Each case names the format that must
// read it and what it must read; result/commentary/management messages must never read as a signal in any format.
#include <cstdio>
#include <map>

#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/SignalParser.h"
#include "TestFramework.h"

using namespace sr;

namespace {

struct Case {
    const char* format;
    const char* text;
    const char* side;  // nullptr = expect not a signal
    const char* symbol;
    double entryLow, entryHigh;  // 0 = market (no entry price)
    double sl;
    std::vector<double> tps;
    const char* kind = nullptr;  // "LIMIT" / "STOP" when the case is a pending order
};

const std::vector<Case>& cases() {
    static const std::vector<Case> v{
        // gold
        {"universal", "XAUUSD SELL 2350-2355\nTP 2345/2340/2335\nSL 2360", "SELL", "XAUUSD", 2350, 2355, 2360, {2345, 2340, 2335}},
        {"universal", "GOLD BUY NOW @2346\nTP1: 2350\nTP2: 2355\nTP3: OPEN\nSL: 2338", "BUY", "XAUUSD", 2346, 2346, 2338, {2350, 2355}},
        {"universal", "🔥 XAU/USD BUY 2346 - 2342 🔥\n\n✅ TP 2350\n✅ TP 2355\n✅ TP 2360\n\n❌ SL 2335", "BUY", "XAUUSD", 2342, 2346, 2335, {2350, 2355, 2360}},
        {"universal", "Gold buy zone 2342 - 2338\nStop loss : 2332\nTake profit : 2350 , 2360 , 2370", "BUY", "XAUUSD", 2338, 2342, 2332, {2350, 2360, 2370}},
        {"universal", "SELL GOLD @ 2365.50\nSL 2372.00\nTP1 2360.00\nTP2 2355.00\nTP3 2345.00", "SELL", "XAUUSD", 2365.5, 2365.5, 2372, {2360, 2355, 2345}},
        {"universal", "Gold Sell Now 2358\n\nTP 2355\nTP 2352\nTP 2349\nTP 2340\n\nSL 2366", "SELL", "XAUUSD", 2358, 2358, 2366, {2355, 2352, 2349, 2340}},
        {"universal", "XAUUSD BUY NOW\nEntry: 2341.5\nSL: 2335 (65 pips)\nTP1: 2345 (35 pips)\nTP2: 2350\nTP3: 2360", "BUY", "XAUUSD", 2341.5, 2341.5, 2335, {2345, 2350, 2360}},
        {"universal", "**XAUUSD** | __SELL__ | 2361\nSL | 2368\nTP | 2355", "SELL", "XAUUSD", 2361, 2361, 2368, {2355}},
        {"universal", "#GOLD SELL LIMIT 2370\nSL 2380\nTP 2360", "SELL", "XAUUSD", 2370, 2370, 2380, {2360}, "LIMIT"},
        {"universal", "GOLD BUY STOP 2355\nSL 2345\nTP 2365", "BUY", "XAUUSD", 2355, 2355, 2345, {2365}, "STOP"},
        // forex
        {"universal", "EURUSD BUY @ 1.0850\nSL 1.0820\nTP 1.0880\nTP 1.0920", "BUY", "EURUSD", 1.085, 1.085, 1.082, {1.088, 1.092}},
        {"universal", "GBPJPY SELL LIMIT 191.50\nSL: 192.20\nTP: 190.80 - 190.10", "SELL", "GBPJPY", 191.5, 191.5, 192.2, {190.8, 190.1}, "LIMIT"},
        {"universal", "📉 USDJPY SHORT\nEntry: 151.20 - 151.40\nStop: 151.90\nTargets: 150.80, 150.40, 149.90", "SELL", "USDJPY", 151.2, 151.4, 151.9, {150.8, 150.4, 149.9}},
        {"universal", "AUDUSD buy 0.6550 sl 0.6520 tp 0.6600", "BUY", "AUDUSD", 0.655, 0.655, 0.652, {0.66}},
        {"universal", "EUR/USD LONG 1.0850\nSL 1.0800\nTP 1.0950", "BUY", "EURUSD", 1.085, 1.085, 1.08, {1.095}},
        {"universal", "Pair: GBPUSD\nDirection: SELL\nEntry price: 1.2710\nStop-loss: 1.2760\nTake-profit: 1.2640", "SELL", "GBPUSD", 1.271, 1.271, 1.276, {1.264}},
        // indices and crypto without separators
        {"universal", "US30 SELL 39250\nSL 39350\nTP 39100\nTP 38950", "SELL", "US30", 39250, 39250, 39350, {39100, 38950}},
        {"universal", "#BTC/USDT LONG\nLeverage: 10x\nEntry: 64500 - 65000\nTargets: 66000 - 67000 - 68000\nStop-loss: 63000", "BUY", "BTCUSD", 64500, 65000, 63000, {66000, 67000, 68000}},
        {"universal", "SOLUSDT SHORT 145.5\nSL 150\nTP 140 / 135", "SELL", "SOLUSD", 145.5, 145.5, 150, {140, 135}},
        {"universal", "ETH LONG 20x 3150\nSL 3050\nTP 3250", "BUY", "ETHUSD", 3150, 3150, 3050, {3250}},
        // languages
        {"universal", "XAUUSD ACHAT 2350\nSL 2340\nObjectif 2360", "BUY", "XAUUSD", 2350, 2350, 2340, {2360}},
        {"universal", "GOLD VERKAUFEN 2350\nSTOP LOSS 2360\nZIEL 2340", "SELL", "XAUUSD", 2350, 2350, 2360, {2340}},
        {"universal", "EURUSD COMPRA 1.0850\nSTOP 1.0820\nALVO 1.0900", "BUY", "EURUSD", 1.085, 1.085, 1.082, {1.09}},
        {"universal", "ORO VENDI 2350 SL 2360 TP 2340", "SELL", "XAUUSD", 2350, 2350, 2360, {2340}},
        {"universal", "الذهب شراء 2350\nوقف الخسارة 2340\nالهدف 2360", "BUY", "XAUUSD", 2350, 2350, 2340, {2360}},
        {"universal", "ЗОЛОТО ПРОДАЖА 2350\nСТОП 2360\nТЕЙК 2340", "SELL", "XAUUSD", 2350, 2350, 2360, {2340}},
        // emoji labels
        {"universal_emoji", "#XAUUSD 🔴SELL LIMIT 2370\n🎯2360\n🎯2350\n🛑2380", "SELL", "XAUUSD", 2370, 2370, 2380, {2360, 2350}, "LIMIT"},
        {"universal_emoji", "GOLD BUY 2340\n🎯 2345\n🎯 2350\n⛔ 2332", "BUY", "XAUUSD", 2340, 2340, 2332, {2345, 2350}},
        {"universal_emoji", "EURUSD SELL\n📍 1.0870\n💰 1.0840\n❌ 1.0900", "SELL", "EURUSD", 1.087, 1.087, 1.09, {1.084}},
        {"universal_emoji", "🎯 TP1: 2355\n🎯 TP2: 2360\nGOLD BUY 2350\n🛑 SL: 2340", "BUY", "XAUUSD", 2350, 2350, 2340, {2355, 2360}},
        // comma decimals
        {"universal_comma", "EURUSD KAUFEN 1,0850\nSL 1,0820\nTP 1,0900", "BUY", "EURUSD", 1.085, 1.085, 1.082, {1.09}},
        {"universal_comma", "XAUUSD VENTE 2350,50\nSL 2358,00\nTP 2340,00", "SELL", "XAUUSD", 2350.5, 2350.5, 2358, {2340}},
        // thousands separators
        {"universal_thousands", "NAS100 BUY 18,250 - 18,200\nSL 18,150\nTP 18,350", "BUY", "NAS100", 18200, 18250, 18150, {18350}},
        {"universal_thousands", "ETH/USDT Buy 3,150\nTP 3,250\nSL 3,050", "BUY", "ETHUSD", 3150, 3150, 3050, {3250}},
        {"universal_thousands", "📍 BTCUSDT LONG 20x\nEntry: 65,000\nTP: 66,000 / 67,500\nSL: 63,800", "BUY", "BTCUSD", 65000, 65000, 63800, {66000, 67500}},
        {"universal_thousands", "US30 SELL 39,250.5\nSL 39,350\nTP 39,100", "SELL", "US30", 39250.5, 39250.5, 39350, {39100}},
        // Turkish / Indonesian
        {"turkish_indonesian", "XAUUSD SAT 2350\nZARAR DURDUR 2360\nHEDEF 2340", "SELL", "XAUUSD", 2350, 2350, 2360, {2340}},
        {"turkish_indonesian", "ALTIN AL 2340\nSL 2330\nHEDEF 2350", "BUY", "XAUUSD", 2340, 2340, 2330, {2350}},
        {"turkish_indonesian", "EMAS BELI 2340\nSL 2330\nTP 2350", "BUY", "XAUUSD", 2340, 2340, 2330, {2350}},
        {"turkish_indonesian", "XAUUSD JUAL SEKARANG\nSL 2360\nTP 2340", "SELL", "XAUUSD", 0, 0, 2360, {2340}},
        // strict labels
        {"strict_labels", "XAUUSD BUY\nEntry: 2340\nSL: 2330\nTP: 2350", "BUY", "XAUUSD", 2340, 2340, 2330, {2350}},
        // must stay review: unsure about a required field
        {"universal", "GOLD BUY 2350\nTP 2360", nullptr, nullptr, 0, 0, 0, {}},                       // no SL
        {"universal", "BUY 2350\nSL 2340\nTP 2360", nullptr, nullptr, 0, 0, 0, {}},                   // no symbol
        {"universal", "XAUUSD BUY 2350 SL 50 pips TP 100 pips", nullptr, nullptr, 0, 0, 0, {}},       // SL in pips, not a price
        {"universal", "XAUUSD BUY 2350 2346 2342\nSL 2335\nTP 2360", nullptr, nullptr, 0, 0, 0, {}},  // three entries
        {"universal", "XAUUSD BUY 2350\nSL 2360\nTP 2370", nullptr, nullptr, 0, 0, 0, {}},            // SL on the wrong side
        {"universal", "NAS100 BUY 18,250\nSL 18,150\nTP 18,350", nullptr, nullptr, 0, 0, 0, {}},       // thousands: only its own format guesses
        {"universal", "XAUUSD BUY 2350 / EURUSD SELL 1.0850\nSL 2340\nTP 2360", nullptr, nullptr, 0, 0, 0, {}},  // two setups
    };
    return v;
}

// Results, updates and chatter: never a signal, whatever the format.
const std::vector<const char*>& nonSignals() {
    static const std::vector<const char*> v{
        "TP1 reached ✅✅",
        "Gold TP2 HIT 🔥 +80 pips",
        "XAUUSD BUY trade closed +50 pips ✅",
        "Close all gold trades now",
        "Move SL to entry on gold",
        "SL hit on EURUSD ❌ -30 pips, next one will be better",
        "🎯 Target 1 done! 🎯 Target 2 done!",
        "Gold running +120 pips 💰💰",
        "Weekly results: 18 wins, 4 losses, +1,240 pips",
        "Who is ready for NFP today? Gold could buy or sell hard",
        "Good morning traders! Market opens in 30 minutes.",
        "Cancel the gold buy limit",
        "🛑 Stop loss hit at 2340",
        "Join our VIP: https://t.me/example 50% off today",
        "TP 2355 ✅ TP 2360 ✅ TP 2365 ✅",
        "",
    };
    return v;
}

std::map<std::string, std::shared_ptr<const CompiledTemplate>>& formats() {
    static std::map<std::string, std::shared_ptr<const CompiledTemplate>> m = [] {
        std::map<std::string, std::shared_ptr<const CompiledTemplate>> out;
        for (auto& t : builtinTemplates()) out[t.id] = CompiledTemplate::compile(t).value();
        return out;
    }();
    return m;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9 * std::max(1.0, std::abs(a)); }

// empty when the signal reads exactly what the case says; else what differs
std::string differences(const Case& c, const Signal& s) {
    std::string why;
    if (toString(*s.side) != std::string(c.side)) why += " side " + std::string(toString(*s.side));
    if (s.symbol != c.symbol) why += " symbol " + s.symbol;
    if (c.entryLow == 0 ? s.entry.has_value() && !s.marketIntent : (!s.entry || !near(s.entry->low, c.entryLow) || !near(s.entry->high, c.entryHigh)))
        why += " entry " + (s.entry ? std::to_string(s.entry->low) + "-" + std::to_string(s.entry->high) : std::string("none"));
    if (!s.stopLoss || !near(*s.stopLoss, c.sl)) why += " sl " + (s.stopLoss ? std::to_string(*s.stopLoss) : std::string("none"));
    bool tpsOk = s.targets.size() == c.tps.size();
    for (std::size_t k = 0; tpsOk && k < c.tps.size(); ++k) tpsOk = near(s.targets[k].price, c.tps[k]);
    if (!tpsOk) {
        why += " tps";
        for (auto& t : s.targets) why += " " + std::to_string(t.price);
    }
    if (c.kind && !(s.explicitKind && toString(s.orderKind) == std::string(c.kind))) why += " kind " + std::string(toString(s.orderKind));
    return why;
}

// the same message as channels really post it: other spacing, line breaks, case, emojis and bold
std::vector<std::string> variants(const std::string& text) {
    auto replaceAll = [](std::string s, const std::string& from, const std::string& to) {
        for (std::size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size()) s.replace(p, from.size(), to);
        return s;
    };
    std::string lower = text;
    for (auto& ch : lower)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    const auto nl = text.find('\n');
    const std::string firstLine = text.substr(0, nl), rest = nl == std::string::npos ? "" : text.substr(nl);
    return {replaceAll(text, "\n", "\r\n"),       replaceAll(text, "\n", "\n\n"),   replaceAll(text, " ", "  "),
            "  " + replaceAll(text, "\n", " \n  ") + "  ", lower,                     firstLine + " \xF0\x9F\x94\xA5" + rest,
            "**" + firstLine + "**" + rest,              "\xE2\x9C\x85 " + text};
}

}  // namespace

TEST(shipped_formats_read_the_case_corpus) {
    int failures = 0;
    for (auto& c : cases()) {
        auto it = formats().find(c.format);
        CHECK(it != formats().end());
        if (it == formats().end()) continue;
        ParseContext ctx;
        Signal s = parseWithTemplate(c.text, *it->second, ctx);
        std::string why;
        if (!c.side) {
            if (outcomeOf(s) == "signal") why = "read as a signal but must not be";
        } else if (outcomeOf(s) != "signal") {
            why = std::string("not a signal: ") + toString(s.status) + (s.diagnostics.empty() ? "" : " - " + s.diagnostics.back());
        } else {
            why = differences(c, s);
        }
        if (!why.empty()) {
            ++failures;
            std::printf("    [%s] %s\n      -> %s\n", c.format, c.text, why.c_str());
        }
    }
    CHECK_EQ(failures, 0);
}

// Changing how a message looks may make it unreadable (then it goes to review), never a different trade.
TEST(shipped_formats_never_read_a_changed_message_as_a_different_trade) {
    int failures = 0, checked = 0;
    for (auto& c : cases()) {
        auto it = formats().find(c.format);
        if (it == formats().end()) continue;
        for (auto& v : variants(c.text)) {
            ParseContext ctx;
            Signal s = parseWithTemplate(v, *it->second, ctx);
            ++checked;
            if (outcomeOf(s) != "signal") continue;  // unsure = review: allowed
            const std::string why = c.side ? differences(c, s) : std::string(" read as a signal but must not be");
            if (!why.empty()) {
                ++failures;
                std::printf("    [%s] %s\n      -> %s\n", c.format, v.c_str(), why.c_str());
            }
        }
    }
    for (auto& [id, f] : formats())
        for (auto* text : nonSignals())
            for (auto& v : variants(text)) {
                ParseContext ctx;
                ++checked;
                if (outcomeOf(parseWithTemplate(v, *f, ctx)) == "signal") {
                    ++failures;
                    std::printf("    [%s] read as a signal: %s\n", id.c_str(), v.c_str());
                }
            }
    CHECK(checked > 500);
    CHECK_EQ(failures, 0);
}

TEST(shipped_formats_never_read_results_or_chatter_as_signals) {
    int failures = 0;
    for (auto& [id, f] : formats())
        for (auto* text : nonSignals()) {
            ParseContext ctx;
            if (outcomeOf(parseWithTemplate(text, *f, ctx)) == "signal") {
                ++failures;
                std::printf("    [%s] read as a signal: %s\n", id.c_str(), text);
            }
        }
    CHECK_EQ(failures, 0);
}

TEST(shipped_formats_pass_the_negative_corpus_and_detection_picks_the_right_one) {
    for (auto& [id, f] : formats()) {
        auto suite = runFixtures(TemplateSignalParser(f), sharedNegativeCorpus());
        for (auto& r : suite.results)
            for (auto& d : r.differences) std::printf("    [%s] %s: %s\n", id.c_str(), r.id.c_str(), d.c_str());
        CHECK(suite.allPassed());
    }
    // a channel's own samples rank the format that reads them first
    std::vector<NamedParser> all;
    for (auto& [id, f] : formats()) all.push_back({id, std::make_shared<TemplateSignalParser>(f)});
    for (const char* id : {"universal_emoji", "universal_comma", "universal_thousands", "turkish_indonesian"}) {
        std::vector<std::string> samples;
        for (auto& c : cases())
            if (std::string(c.format) == id && c.side) samples.push_back(c.text);
        auto ranked = detectFormat(all, samples);
        CHECK_EQ(ranked[0].signals, static_cast<int>(samples.size()));
        // the dedicated format reads every one of them (others may tie, never beat it)
        bool topHasIt = false;
        for (auto& r : ranked)
            if (r.parserId == id) topHasIt = r.signals == ranked[0].signals;
        CHECK(topHasIt);
    }
}

TEST(emoji_labels_keep_evidence_on_the_original_text) {
    ParseContext ctx;
    const std::string text = "GOLD BUY 2340\n🎯 2345\n⛔ 2332";
    Signal s = parseWithTemplate(text, *formats()["universal_emoji"], ctx);
    CHECK_EQ(outcomeOf(s), std::string("signal"));
    for (auto& e : s.evidence)
        if (e.field == "sl") CHECK_EQ(text.substr(e.span.begin, e.span.end - e.span.begin), std::string("2332"));
    auto bad = ParserTemplate::fromJson({{"id", "x"}, {"emojiLabels", {{"ab", "TP"}}}});
    CHECK(!bad);  // keys must be one emoji
}
