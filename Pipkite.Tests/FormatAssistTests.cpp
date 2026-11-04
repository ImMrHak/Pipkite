// AI format suggestions are checked before they become a format, and taught formats must read what the user marked.
#include "Pipkite.Core/FormatAssist.h"
#include "Pipkite.Core/ParserBuilder.h"
#include "Pipkite.Core/SignalParser.h"
#include "TestFramework.h"

using namespace sr;
using nlohmann::json;

namespace {

Signal read(const ParserTemplate& t, const std::string& text) {
    ParseContext ctx;
    return parseWithTemplate(text, *CompiledTemplate::compile(t).value(), ctx);
}

bool has(const std::vector<std::string>& v, const std::string& w) { return std::find(v.begin(), v.end(), w) != v.end(); }

}  // namespace

TEST(ai_suggestion_teaches_a_format_the_universal_one_cannot_read) {
    const std::string dutch = "GOUD KOPEN 2350\nVERLIES 2340\nDOEL 2360";
    CHECK(outcomeOf(read(universalTemplate(), dutch)) != "signal");
    const json suggestion = json::parse(R"J({
        "name": "Dutch gold", "explanation": "The channel writes in Dutch.",
        "add": {"buyWords": ["kopen"], "sellWords": ["VERKOPEN"], "slLabels": ["VERLIES:"], "tpLabels": ["DOEL"], "symbolAliases": {"GOUD": "xauusd"}}
    })J");
    auto r = applyFormatSuggestion(universalTemplate(), suggestion);
    CHECK(static_cast<bool>(r));
    Signal s = read(r.value().format, dutch);
    CHECK_EQ(outcomeOf(s), std::string("signal"));
    CHECK_EQ(s.symbol, std::string("XAUUSD"));
    CHECK(s.stopLoss && *s.stopLoss == 2340);
    CHECK(has(r.value().format.slLabels, "VERLIES"));  // upper-cased, trailing ':' dropped
    CHECK_EQ(r.value().explanation, std::string("The channel writes in Dutch."));
}

TEST(ai_suggestion_can_never_touch_the_safety_rules) {
    const json suggestion = json::parse(R"J({
        "requireStopLoss": false, "conflictBehavior": "first", "advancedRules": [{"field": "sl", "pattern": "(.*)"}],
        "set": {"requireStopLoss": false, "requireTakeProfit": false, "maxTargets": 20, "allowGluedTokens": false},
        "add": {"requireStopLoss": [false], "advancedRules": ["x"]}
    })J");
    auto r = applyFormatSuggestion(universalTemplate(), suggestion);
    CHECK(static_cast<bool>(r));
    const ParserTemplate& t = r.value().format;
    const ParserTemplate u = universalTemplate();
    CHECK(t.requireStopLoss && t.requireTakeProfit);
    CHECK_EQ(t.conflictBehavior, std::string("review"));
    CHECK(t.advancedRules.empty());
    CHECK_EQ(t.maxTargets, u.maxTargets);
    CHECK_EQ(t.allowGluedTokens, u.allowGluedTokens);
    CHECK(t.toJson() == u.toJson());  // nothing at all changed
}

TEST(ai_suggestion_skips_words_that_would_mean_two_things) {
    const json suggestion = json::parse(R"J({"add": {
        "buyWords": ["SELL", "TP", "12345", "", "A VERY LONG WORD THAT GOES ON AND ON PAST FORTY BYTES"],
        "slLabels": ["STOP"], "stopWords": ["STOP"],
        "symbolAliases": {"GOLD": "BTCUSD", "GOUD": "XAU/USD", "BAD": "not a symbol!"},
        "emojiLabels": {"🎯": "TP", "🚀": "BUY", "🎯🎯": "SL", "⛔️": "SL"}
    }})J");
    auto r = applyFormatSuggestion(universalTemplate(), suggestion);
    CHECK(static_cast<bool>(r));
    const ParserTemplate& t = r.value().format;
    const ParserTemplate u = universalTemplate();
    CHECK_EQ(t.buyWords.size(), u.buyWords.size());               // a SELL word, a label, a number, nothing and too long: all skipped
    CHECK_EQ(t.symbolAliases.at("GOLD"), std::string("XAUUSD"));  // an existing alias is never re-pointed
    CHECK_EQ(t.symbolAliases.at("GOUD"), std::string("XAUUSD"));
    CHECK(!t.symbolAliases.count("BAD"));
    CHECK_EQ(t.emojiLabels.size(), std::size_t(2));  // 🎯 as TP, and ⛔ (its variation selector dropped) as SL
    CHECK_EQ(t.emojiLabels.at("\xE2\x9B\x94"), std::string("SL"));
    CHECK(!applyFormatSuggestion(universalTemplate(), json::array()));
    CHECK(!applyFormatSuggestion(universalTemplate(), json::parse(R"J({"add": []})J")));
}

TEST(ai_suggestion_sets_the_number_format_only_when_asked) {
    auto r = applyFormatSuggestion(universalTemplate(), json::parse(R"J({"set": {"numberFormat": "comma_decimal"}})J"));
    CHECK(static_cast<bool>(r));
    CHECK(r.value().format.numberFormat == NumberFormat::CommaDecimal);
    Signal s = read(r.value().format, "GOLD BUY 2350,50\nSL 2340,00\nTP 2360,00");
    CHECK_EQ(outcomeOf(s), std::string("signal"));
    CHECK(s.entry && s.entry->low == 2350.5);
    auto bad = applyFormatSuggestion(universalTemplate(), json::parse(R"J({"set": {"numberFormat": "roman"}})J"));
    CHECK(bad.value().format.numberFormat == NumberFormat::DotDecimal);
}

TEST(ai_chat_reply_is_unwrapped_and_errors_are_plain) {
    const std::string ok = R"J({"choices":[{"message":{"role":"assistant","content":"```json\n{\"add\": {\"buyWords\": [\"KOPEN\"]}}\n```"}}]})J";
    auto s = suggestionFromChatReply(ok);
    CHECK(static_cast<bool>(s));
    CHECK_EQ(s.value()["add"]["buyWords"][0].get<std::string>(), std::string("KOPEN"));
    auto refused = suggestionFromChatReply(R"J({"error":{"message":"Incorrect API key provided"}})J");
    CHECK(!refused);
    CHECK(refused.error().message.find("Incorrect API key") != std::string::npos);
    CHECK(!suggestionFromChatReply("<html>bad gateway</html>"));
    CHECK(!suggestionFromChatReply(R"J({"choices":[{"message":{"content":"I can't help with that."}}]})J"));
}

TEST(ai_request_holds_known_words_and_capped_messages) {
    std::vector<std::string> msgs(20, "GOLD BUY 2350\nSL 2340\nTP 2360");
    for (std::size_t i = 0; i < msgs.size(); ++i) msgs[i] += "\n#" + std::to_string(i);
    msgs[1] = std::string(3000, 'x');  // too long: left out
    const std::string req = formatSuggestionRequest(universalTemplate(), msgs);
    CHECK(req.find("buyWords: BUY, LONG") != std::string::npos);
    CHECK(req.find("#0") != std::string::npos && req.find("#15") != std::string::npos && req.find("#16") == std::string::npos);
    CHECK(req.find("xxxx") == std::string::npos);
    const json body = chatCompletionBody("deepseek-chat", formatSuggestionPrompt(), req);
    CHECK_EQ(body["model"].get<std::string>(), std::string("deepseek-chat"));
    CHECK_EQ(body["response_format"]["type"].get<std::string>(), std::string("json_object"));
    CHECK_EQ(body["messages"][0]["role"].get<std::string>(), std::string("system"));
    CHECK(knownWords(universalTemplate())["symbolAliases"].size() > 20);
}

TEST(taught_format_must_read_what_the_user_marked) {
    const std::string text = "GOLD BUY 2350\nSL 2340\nTP 2360\nTP 2370";
    const ParserTemplate t = universalTemplate();
    Signal s = read(t, text);
    auto at = [&](const char* field, const std::string& word, const char* canonical = "") {
        const auto p = text.find(word);
        return HighlightLabel{field, static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(p + word.size()), canonical};
    };
    CHECK(marksDisagree(t, s, text, {at("buy", "BUY"), at("symbol", "GOLD"), at("entry", "2350"), at("sl", "2340"), at("tp", "2360"), at("tp", "2370")}).empty());
    CHECK_EQ(marksDisagree(t, s, text, {at("sell", "BUY")}).size(), std::size_t(1));
    CHECK_EQ(marksDisagree(t, s, text, {at("sl", "2360")}).size(), std::size_t(1));
    CHECK_EQ(marksDisagree(t, s, text, {at("tp", "2340")}).size(), std::size_t(1));
    CHECK_EQ(marksDisagree(t, s, text, {at("symbol", "GOLD", "XAGUSD")}).size(), std::size_t(1));
    CHECK(marksDisagree(t, s, text, {HighlightLabel{"sl", 500, 510, ""}}).empty());  // outside the text: ignored, not a crash
}
