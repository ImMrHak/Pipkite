#include "FormatAssist.h"

#include <algorithm>
#include <cctype>
#include <optional>

#include "Text.h"
#include "Tokenizer.h"

namespace sr {

using nlohmann::json;

const std::string& formatSuggestionPrompt() {
    static const std::string p = R"(You help Pipkite, a trade copier, read the trading signals that one Telegram channel posts. You never read or decide a trade yourself. You describe the channel's wording so that Pipkite's own rule-based parser can read its messages. Pipkite then tests your suggestion on the channel's messages, and the user checks every reading before anything is traded.

How the parser reads a signal:
- A direction word (one of the BUY words or SELL words) and a symbol, then prices.
- The entry is the price after the direction word or after an entry label. Two prices make an entry range.
- The stop loss is the price after an SL label. Take profits are the prices after TP labels (TP1, TP2... work).
- A LIMIT or STOP word right after the direction makes a pending order. A NOW word means market.
- Words and labels match as whole words, in any letter case. A label can have several words, like STOP LOSS.
- Symbol aliases map the name the channel writes to a standard name, like GOLD to XAUUSD.
- Messages about results or updates (TP1 hit, +40 pips, move SL to entry) must not read as signals. Their typical words go in commentaryWords (results) or managementPhrases (instructions about open trades).

Answer with one JSON object and nothing else:
{
  "name": "a short name for this channel's format, at most 40 characters",
  "explanation": "one or two plain sentences: what you added and why",
  "add": {
    "buyWords": [], "sellWords": [], "nowWords": [], "limitWords": [], "stopWords": [],
    "entryLabels": [], "slLabels": [], "tpLabels": [],
    "commentaryWords": [], "managementPhrases": [],
    "symbolAliases": {"NAME AS WRITTEN": "STANDARD NAME"},
    "emojiLabels": {"EMOJI": "TP"}
  },
  "set": {"numberFormat": "dot_decimal", "allowThousandsSeparator": false}
}

Rules:
- Only add words and emojis that appear in the messages. Never invent prices, symbols or words.
- Write words in upper case, without the punctuation around them (leave out a trailing ":" or "-").
- Leave out everything the request lists as already known, and leave out empty lists. If nothing is needed, answer with "add": {}.
- symbolAliases values are standard names such as XAUUSD, XAGUSD, EURUSD, GBPJPY, US30, NAS100, USOIL, BTCUSD.
- emojiLabels: one emoji per key; the value is TP, SL or ENTRY.
- A word is never both a BUY word and a SELL word, and never both a label and a direction word.
- Use "set" only when the channel writes prices with a decimal comma (2350,50 means comma_decimal) or with thousands separators (65,000 means allowThousandsSeparator true). Otherwise leave "set" out.
- If the messages are not trading signals at all, answer with "add": {} and say so in the explanation.)";
    return p;
}

namespace {

constexpr std::size_t kMaxWord = 40;     // bytes
constexpr std::size_t kMaxPerList = 30;  // additions per list
constexpr std::size_t kMaxMessages = 15;
constexpr std::size_t kMaxSampleBytes = 2000;

std::string joinList(const std::vector<std::string>& v) {
    std::string out;
    for (auto& w : v) out += (out.empty() ? "" : ", ") + w;
    return out.empty() ? "(none)" : out;
}

// the lists a suggestion may add to, by their JSON name
std::vector<std::pair<const char*, std::vector<std::string> ParserTemplate::*>> wordLists() {
    return {{"buyWords", &ParserTemplate::buyWords},           {"sellWords", &ParserTemplate::sellWords},
            {"nowWords", &ParserTemplate::nowWords},           {"limitWords", &ParserTemplate::limitWords},
            {"stopWords", &ParserTemplate::stopWords},         {"entryLabels", &ParserTemplate::entryLabels},
            {"slLabels", &ParserTemplate::slLabels},           {"tpLabels", &ParserTemplate::tpLabels},
            {"commentaryWords", &ParserTemplate::commentaryWords}, {"managementPhrases", &ParserTemplate::managementPhrases}};
}

bool hasPhrase(const std::vector<std::string>& list, const std::vector<std::string>& phrase) {
    return std::any_of(list.begin(), list.end(), [&](const std::string& p) { return phraseTokens(p) == phrase; });
}

// a word to add: valid UTF-8, short, not just a number or punctuation
std::optional<std::string> cleanWord(const json& v) {
    if (!v.is_string()) return std::nullopt;
    std::string w = toUpperAscii(trim(v.get<std::string>()));
    while (!w.empty() && (w.back() == ':' || w.back() == '-' || w.back() == '=')) w = trim(w.substr(0, w.size() - 1));
    if (w.empty() || w.size() > kMaxWord || !isValidUtf8(w) || w.find('\n') != std::string::npos) return std::nullopt;
    if (phraseTokens(w).empty()) return std::nullopt;
    if (std::all_of(w.begin(), w.end(), [](unsigned char c) { return std::isdigit(c) || c == '.' || c == ',' || c == ' '; })) return std::nullopt;
    return w;
}

}  // namespace

json knownWords(const ParserTemplate& base) {
    json j = json::object();
    for (auto& [name, member] : wordLists()) j[name] = base.*member;
    json aliases = json::array();
    for (auto& [k, v] : base.symbolAliases) aliases.push_back(k);
    j["symbolAliases"] = aliases;
    json emoji = json::array();
    for (auto& [k, v] : base.emojiLabels) emoji.push_back(k);
    j["emojiLabels"] = emoji;
    j["numberFormat"] = base.numberFormat == NumberFormat::DotDecimal ? "dot_decimal" : "comma_decimal";
    return j;
}

std::string formatSuggestionRequest(const ParserTemplate& base, const std::vector<std::string>& messages) {
    std::string out = "Already known (don't repeat these):\n";
    const json known = knownWords(base);
    for (auto& [name, member] : wordLists()) out += std::string(name) + ": " + joinList(base.*member) + "\n";
    out += "symbolAliases: " + joinList(known["symbolAliases"].get<std::vector<std::string>>()) + "\n";
    out += "emojiLabels: " + joinList(known["emojiLabels"].get<std::vector<std::string>>()) + "\n";
    out += "numberFormat: " + known["numberFormat"].get<std::string>() + "\n\n";
    out += "Messages from the channel, newest first, separated by lines with ---:\n";
    std::size_t n = 0;
    for (auto& m : messages) {
        if (n == kMaxMessages) break;
        std::string text = trim(m);
        if (text.empty()) continue;
        if (text.size() > kMaxSampleBytes) continue;  // long posts are rarely signals
        out += (n++ ? "---\n" : "") + text + "\n";
    }
    return out;
}

json chatCompletionBody(const std::string& model, const std::string& system, const std::string& user) {
    return {{"model", model},
            {"temperature", 0},
            {"response_format", {{"type", "json_object"}}},
            {"messages", json::array({{{"role", "system"}, {"content", system}}, {{"role", "user"}, {"content", user}}})}};
}

Result<json> suggestionFromChatReply(const std::string& body) {
    const json r = json::parse(body, nullptr, false);
    if (r.is_discarded() || !r.is_object()) return Error{"ai_reply", "The AI provider's answer is not JSON"};
    if (r.contains("error")) {
        const json& e = r["error"];
        const std::string msg = e.is_object() ? e.value("message", "") : e.is_string() ? e.get<std::string>() : "";
        return Error{"ai_reply", "The AI provider refused: " + (msg.empty() ? std::string("no reason given") : msg.substr(0, 300))};
    }
    if (!r.contains("choices") || !r["choices"].is_array() || r["choices"].empty()) return Error{"ai_reply", "The AI provider sent no answer"};
    const json& msg = r["choices"][0].value("message", json::object());
    std::string content = msg.is_object() ? msg.value("content", "") : "";
    // some models wrap the object in a ``` code block despite json_object mode
    if (auto a = content.find('{'), b = content.rfind('}'); a != std::string::npos && b != std::string::npos && b > a) content = content.substr(a, b - a + 1);
    const json s = json::parse(content, nullptr, false);
    if (s.is_discarded() || !s.is_object()) return Error{"ai_reply", "The AI's answer is not the JSON object Pipkite asked for"};
    return s;
}

Result<AppliedSuggestion> applyFormatSuggestion(const ParserTemplate& base, const json& suggestion) {
    if (!suggestion.is_object()) return Error{"ai_suggestion", "The suggestion is not a JSON object"};
    AppliedSuggestion r;
    r.format = base;
    ParserTemplate& t = r.format;
    if (suggestion.contains("explanation") && suggestion["explanation"].is_string()) r.explanation = trim(suggestion["explanation"].get<std::string>()).substr(0, 400);
    if (!isValidUtf8(r.explanation)) r.explanation.clear();
    const json add = suggestion.value("add", json::object());
    if (!add.is_object()) return Error{"ai_suggestion", "\"add\" must be an object"};

    // words that already mean something else, so a new word can't become ambiguous
    auto directionOrLabel = [&](const std::vector<std::string>& p, const char* except) {
        for (auto& [name, member] : wordLists()) {
            const std::string n = name;
            if (n == except || n == "commentaryWords" || n == "managementPhrases") continue;
            if (hasPhrase(t.*member, p)) return n;
        }
        return std::string();
    };
    for (auto& [name, member] : wordLists()) {
        if (!add.contains(name)) continue;
        if (!add[name].is_array()) {
            r.added.push_back(std::string("Skipped ") + name + ": not a list");
            continue;
        }
        std::size_t n = 0;
        for (auto& v : add[name]) {
            auto w = cleanWord(v);
            if (!w) continue;
            const auto p = phraseTokens(*w);
            if (hasPhrase(t.*member, p)) continue;
            // "STOP" is both an order kind and an SL label on purpose (the parser decides by position); anything else can't mean two things
            const std::string clash = directionOrLabel(p, name);
            const bool stopKindAndSl = (std::string(name) == "slLabels" && clash == "stopWords") || (std::string(name) == "stopWords" && clash == "slLabels");
            if (!clash.empty() && !stopKindAndSl) {
                r.added.push_back("Skipped '" + *w + "': it is already in " + clash);
                continue;
            }
            if (n++ == kMaxPerList) break;
            (t.*member).push_back(*w);
            r.added.push_back(std::string("Added '") + *w + "' to " + name);
        }
    }
    if (add.contains("symbolAliases") && add["symbolAliases"].is_object()) {
        std::size_t n = 0;
        for (auto& [k, v] : add["symbolAliases"].items()) {
            auto alias = cleanWord(json(k));
            if (!alias || !v.is_string()) continue;
            std::string key;
            for (auto& tok : phraseTokens(*alias)) key += tok;
            std::string canon = toUpperAscii(trim(v.get<std::string>()));
            canon.erase(std::remove(canon.begin(), canon.end(), '/'), canon.end());
            const bool okCanon = canon.size() >= 3 && canon.size() <= 12 && std::all_of(canon.begin(), canon.end(), [](unsigned char c) { return std::isupper(c) || std::isdigit(c); });
            if (key.empty() || !okCanon) {
                r.added.push_back("Skipped symbol alias '" + *alias + "'");
                continue;
            }
            if (auto it = t.symbolAliases.find(key); it != t.symbolAliases.end()) {
                if (it->second != canon) r.added.push_back("Skipped '" + key + "' -> " + canon + ": it already means " + it->second);
                continue;
            }
            if (n++ == kMaxPerList) break;
            t.symbolAliases[key] = canon;
            r.added.push_back("Added symbol " + key + " = " + canon);
        }
    }
    if (add.contains("emojiLabels") && add["emojiLabels"].is_object()) {
        std::size_t n = 0;
        for (auto& [k, v] : add["emojiLabels"].items()) {
            if (!v.is_string() || !isValidUtf8(k) || k.empty()) continue;
            std::size_t i = 0;
            decodeUtf8(k, i);
            if (i != k.size()) {  // a variation selector after the emoji is fine, the normalizer drops it
                std::size_t j = i;
                if (decodeUtf8(k, j) != 0xFE0F || j != k.size()) continue;
            }
            const std::string label = toUpperAscii(trim(v.get<std::string>()));
            if (label != "TP" && label != "SL" && label != "ENTRY") continue;
            const std::string key = k.substr(0, i);
            if (t.emojiLabels.count(key)) continue;
            if (n++ == kMaxPerList) break;
            t.emojiLabels[key] = label;
            r.added.push_back("Added " + key + " as " + label);
        }
    }
    const json set = suggestion.value("set", json::object());
    if (set.is_object()) {
        if (set.contains("numberFormat") && set["numberFormat"].is_string()) {
            const std::string nf = set["numberFormat"].get<std::string>();
            const auto f = nf == "comma_decimal" ? NumberFormat::CommaDecimal : NumberFormat::DotDecimal;
            if ((nf == "comma_decimal" || nf == "dot_decimal") && f != t.numberFormat) {
                t.numberFormat = f;
                r.added.push_back("Prices use a decimal " + std::string(nf == "comma_decimal" ? "comma" : "point"));
            }
        }
        if (set.contains("allowThousandsSeparator") && set["allowThousandsSeparator"].is_boolean() && set["allowThousandsSeparator"].get<bool>() != t.allowThousandsSeparator) {
            t.allowThousandsSeparator = set["allowThousandsSeparator"].get<bool>();
            r.added.push_back(t.allowThousandsSeparator ? "Prices may have thousands separators" : "Prices have no thousands separators");
        }
    }
    if (auto v = t.validate(); !v) return Error{"ai_suggestion", "The suggested format is not valid: " + v.error().message};
    return r;
}

}  // namespace sr
