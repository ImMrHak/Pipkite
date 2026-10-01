#pragma once
// Declarative, versioned parser mapping ("transformer" in the Parser Builder UI).
// Stored as validated JSON, compiled once on activation, never interpreted as a script.

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "BoundedRegex.h"
#include "Result.h"
#include "Tokenizer.h"
#include <nlohmann/json.hpp>

namespace sr {

struct AdvancedRule {
    std::string field;    // symbol, side, entry, sl, tp, lot
    std::string pattern;  // bounded regex; capture group 1 is the value
};

struct ParserTemplate {
    int schemaVersion = 1;
    std::string id = "custom";
    std::string version = "1.0.0";
    std::string name = "Custom parser";
    std::string description;

    std::vector<std::string> buyWords{"BUY", "LONG"};
    std::vector<std::string> sellWords{"SELL", "SHORT"};
    std::vector<std::string> limitWords{"LIMIT"};
    std::vector<std::string> stopWords{"STOP"};
    std::vector<std::string> nowWords{"NOW", "MARKET", "CMP", "MARKET EXECUTION"};

    std::vector<std::string> entryLabels{"ENTRY", "ENTRY ZONE", "ENTRY PRICE", "ENTER", "ZONE", "RANGE", "OPEN"};
    std::vector<std::string> slLabels{"SL", "STOP LOSS", "STOPLOSS", "S/L", "S.L", "STOP-LOSS"};
    std::vector<std::string> tpLabels{"TP", "TAKE PROFIT", "TAKEPROFIT", "T/P", "T.P", "TARGET"};
    std::vector<std::string> lotLabels{"LOT", "LOTS", "VOLUME"};
    std::vector<std::string> referenceLabels{"REF", "REFERENCE", "SIGNAL ID"};
    std::vector<std::string> expiryLabels{"EXPIRY", "EXPIRES", "VALID FOR", "VALID"};

    std::vector<std::string> commentaryWords{"HIT", "PROFIT", "PROFITS", "PIPS", "CONGRATS", "CONGRATULATIONS", "RUNNING", "BOOKED", "SECURED", "SMASHED", "BOOM", "RESULT", "RESULTS"};
    std::vector<std::string> managementPhrases{"CLOSE ALL", "CLOSE NOW", "CLOSE HALF", "CLOSE PARTIAL", "MOVE SL", "SL TO BE", "SL TO ENTRY", "BREAKEVEN", "BREAK EVEN", "CANCEL", "DELETE ORDER", "SET BE"};

    // Provider text -> canonical symbol. Broker symbols are mapped later by policy.
    std::map<std::string, std::string> symbolAliases{{"GOLD", "XAUUSD"}, {"XAUUSD", "XAUUSD"}, {"SILVER", "XAGUSD"}, {"XAGUSD", "XAGUSD"},
                                                     {"USOIL", "USOIL"}, {"WTI", "USOIL"}, {"UKOIL", "UKOIL"}, {"BRENT", "UKOIL"},
                                                     {"US30", "US30"}, {"DOW", "US30"}, {"NAS100", "NAS100"}, {"NASDAQ", "NAS100"},
                                                     {"BTC", "BTCUSD"}, {"BTCUSD", "BTCUSD"}};
    bool recognizeCurrencyPairs = true;  // EURUSD-style 6-letter ISO pairs

    NumberFormat numberFormat = NumberFormat::DotDecimal;
    bool allowThousandsSeparator = false;
    bool allowGluedTokens = true;  // accept "SELL4295"; only enable when fixtures cover it
    bool requireStopLoss = true;
    bool requireTakeProfit = true;
    int maxTargets = 10;
    std::string conflictBehavior = "review";  // deterministic: conflicting values always require review

    std::vector<AdvancedRule> advancedRules;

    nlohmann::json toJson() const;
    static Result<ParserTemplate> fromJson(const nlohmann::json& j);
    Status validate() const;
};

// Compiled form: phrases tokenized, regexes compiled. Immutable once built.
struct CompiledTemplate {
    ParserTemplate source;
    std::vector<std::vector<std::string>> buy, sell, limit, stop, now, entry, sl, tp, lot, ref, expiry, management;
    std::vector<std::string> commentary;
    std::map<std::string, std::string> aliases;  // upper-cased keys
    struct Rule {
        std::string field;
        std::shared_ptr<const BoundedRegex> re;
    };
    std::vector<Rule> rules;

    static Result<std::shared_ptr<const CompiledTemplate>> compile(const ParserTemplate& t);
};

// Starter templates offered by the Parser Builder.
ParserTemplate starterGoldTemplate();
ParserTemplate starterForexTemplate();
ParserTemplate starterStrictTemplate();  // no glued tokens, no market words
void applyArabicPreset(ParserTemplate& t);
void applyRussianPreset(ParserTemplate& t);
void applySpanishPreset(ParserTemplate& t);

}  // namespace sr
