#pragma once
// utf-8 validation + the normalized copy the parser works on.
// every normalized byte maps back to the original range, for evidence highlighting.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Models.h"

namespace sr {

inline constexpr std::size_t kMaxMessageBytes = 64 * 1024;
inline constexpr std::size_t kMaxLineBytes = 4096;
inline constexpr std::size_t kMaxTokens = 4096;

bool isValidUtf8(std::string_view s);

// Decodes one code point at s[i]; advances i. Assumes valid UTF-8.
char32_t decodeUtf8(std::string_view s, std::size_t& i);
void appendUtf8(std::string& out, char32_t cp);

struct NormalizedText {
    std::string text;                 // normalized UTF-8
    std::vector<std::uint32_t> origBegin;  // per normalized byte: original begin offset
    std::vector<std::uint32_t> origEnd;    // per normalized byte: original end offset

    // Maps a normalized byte span back to an original byte span.
    Span toOriginal(std::uint32_t begin, std::uint32_t end) const;
};

// Normalization rules:
//  CRLF/CR -> LF; NBSP and exotic spaces -> ' '; Unicode dashes -> '-';
//  Arabic-Indic, superscript, subscript, fullwidth digits -> ASCII digits;
//  zero-width joiners, variation selectors and keycap combiners removed;
//  emojis and Markdown decoration characters (* _ ` ~) -> ' '; ASCII upper-cased.
NormalizedText normalize(std::string_view original);

std::string toUpperAscii(std::string s);
std::string trim(std::string_view s);

}  // namespace sr
