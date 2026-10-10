#include "Tokenizer.h"

#include <cmath>

#include "Text.h"

namespace sr {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAsciiLetter(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isSpace(char c) { return c == ' ' || c == '\t'; }
// Non-ASCII bytes (letters of other scripts after normalization) are word characters.
bool isWordByte(char c) { return isAsciiLetter(c) || (static_cast<unsigned char>(c) >= 0x80); }

bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isDigit(c)) return false;
    return true;
}

// Validates "1,234,567" style grouping: first group 1-3 digits, rest exactly 3.
bool validGrouping(std::string_view intPart, char sep) {
    std::size_t start = 0;
    bool first = true;
    while (true) {
        std::size_t pos = intPart.find(sep, start);
        std::string_view group = intPart.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start);
        if (!allDigits(group)) return false;
        if (first) {
            if (group.size() < 1 || group.size() > 3) return false;
        } else if (group.size() != 3) return false;
        first = false;
        if (pos == std::string_view::npos) return true;
        start = pos + 1;
    }
}

}  // namespace

std::optional<double> parseStrictNumber(std::string_view lex, const TokenizeOptions& opt, bool* isInteger) {
    if (lex.empty() || lex.size() > 32) return std::nullopt;
    const char decimalSep = opt.numberFormat == NumberFormat::DotDecimal ? '.' : ',';
    const char groupSep = opt.numberFormat == NumberFormat::DotDecimal ? ',' : '.';

    const std::size_t decPos = lex.find(decimalSep);
    if (decPos != std::string_view::npos && lex.find(decimalSep, decPos + 1) != std::string_view::npos) return std::nullopt;
    std::string_view intPart = lex.substr(0, decPos);
    std::string_view fracPart = decPos == std::string_view::npos ? std::string_view{} : lex.substr(decPos + 1);

    if (decPos != std::string_view::npos && !allDigits(fracPart)) return std::nullopt;
    if (fracPart.find(groupSep) != std::string_view::npos) return std::nullopt;

    std::string digits;
    if (intPart.find(groupSep) != std::string_view::npos) {
        // A group separator is only accepted with explicit template permission and exact 3-digit groups.
        if (!opt.allowThousandsSeparator) return std::nullopt;
        if (!validGrouping(intPart, groupSep)) return std::nullopt;
        for (char c : intPart)
            if (c != groupSep) digits.push_back(c);
    } else {
        if (!allDigits(intPart)) return std::nullopt;
        digits.assign(intPart);
    }

    std::size_t significant = digits.size() + fracPart.size();
    if (significant > 15) return std::nullopt;

    double value = 0;
    for (char c : digits) value = value * 10 + (c - '0');
    double scale = 0.1;
    for (char c : fracPart) {
        value += (c - '0') * scale;
        scale /= 10;
    }
    // Round-trip through a decimal-exact construction to avoid accumulated error for typical prices.
    if (!fracPart.empty()) {
        double p = std::pow(10.0, static_cast<double>(fracPart.size()));
        value = std::round(value * p) / p;
    }
    if (!std::isfinite(value)) return std::nullopt;
    if (isInteger) *isInteger = fracPart.empty();
    return value;
}

TokenizeResult tokenize(std::string_view s, const TokenizeOptions& opt, std::size_t maxTokens) {
    TokenizeResult r;
    int line = 0;
    std::size_t i = 0;
    bool prevWasSpace = true;
    while (i < s.size()) {
        if (r.tokens.size() >= maxTokens) {
            r.truncated = true;
            break;
        }
        const char c = s[i];
        if (c == '\n') {
            Token t;
            t.type = TokenType::Newline;
            t.text = "\n";
            t.begin = static_cast<std::uint32_t>(i);
            t.end = static_cast<std::uint32_t>(i + 1);
            t.line = line;
            r.tokens.push_back(std::move(t));
            ++line;
            ++i;
            prevWasSpace = true;
            continue;
        }
        if (isSpace(c)) {
            ++i;
            prevWasSpace = true;
            continue;
        }
        Token t;
        t.begin = static_cast<std::uint32_t>(i);
        t.line = line;
        t.glued = !prevWasSpace && !r.tokens.empty() && r.tokens.back().type != TokenType::Newline;
        if (isDigit(c)) {
            // Maximal run of digits with inner separators that are followed by a digit.
            std::size_t j = i;
            while (j < s.size()) {
                if (isDigit(s[j])) { ++j; continue; }
                if ((s[j] == '.' || s[j] == ',') && j + 1 < s.size() && isDigit(s[j + 1])) { ++j; continue; }
                break;
            }
            std::string_view lex = s.substr(i, j - i);
            bool isInt = false;
            auto v = parseStrictNumber(lex, opt, &isInt);
            t.type = v ? TokenType::Number : TokenType::BadNumber;
            t.number = v.value_or(0);
            t.isInteger = isInt;
            t.text.assign(lex);
            i = j;
        } else if (isWordByte(c)) {
            std::size_t j = i;
            while (j < s.size() && isWordByte(s[j])) ++j;
            t.type = TokenType::Word;
            t.text.assign(s.substr(i, j - i));
            i = j;
        } else {
            t.type = TokenType::Punct;
            t.text.assign(1, c);
            ++i;
        }
        t.end = static_cast<std::uint32_t>(i);
        prevWasSpace = false;
        r.tokens.push_back(std::move(t));
    }
    return r;
}

std::vector<std::string> phraseTokens(std::string_view phrase) {
    auto norm = normalize(phrase);
    TokenizeOptions opt;
    opt.allowThousandsSeparator = true;
    auto res = tokenize(norm.text, opt, 64);
    std::vector<std::string> out;
    for (auto& t : res.tokens)
        if (t.type != TokenType::Newline) out.push_back(t.text);
    return out;
}

}  // namespace sr
