#pragma once
// Small deterministic tokenizer over the normalized text. No regular expressions.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sr {

enum class NumberFormat { DotDecimal, CommaDecimal };

enum class TokenType { Word, Number, Punct, Newline, BadNumber };

struct Token {
    TokenType type = TokenType::Word;
    std::string text;
    std::uint32_t begin = 0;  // normalized byte offsets
    std::uint32_t end = 0;
    int line = 0;
    bool glued = false;  // no whitespace between this token and the previous one on the same line
    double number = 0;   // valid when type == Number
    bool isInteger = false;
};

struct TokenizeOptions {
    NumberFormat numberFormat = NumberFormat::DotDecimal;
    bool allowThousandsSeparator = false;
};

// Parses a numeric lexeme strictly and locale-independently.
// Rejects NaN/inf, overflow, >15 significant digits, and ambiguous separators.
std::optional<double> parseStrictNumber(std::string_view lexeme, const TokenizeOptions& opt, bool* isInteger = nullptr);

struct TokenizeResult {
    std::vector<Token> tokens;
    bool truncated = false;  // token limit reached
};

TokenizeResult tokenize(std::string_view normalized, const TokenizeOptions& opt, std::size_t maxTokens);

// Tokenizes a label/keyword phrase the same way so phrases can be matched token by token.
std::vector<std::string> phraseTokens(std::string_view phrase);

}  // namespace sr
