#include "Text.h"

namespace sr {

bool isValidUtf8(std::string_view s) {
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        int len = 0;
        char32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n) return false;
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Reject overlong encodings, surrogates and out-of-range code points.
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        i += len;
    }
    return true;
}

char32_t decodeUtf8(std::string_view s, std::size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) { ++i; return c; }
    int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : 4;
    char32_t cp = len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    return cp;
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

Span NormalizedText::toOriginal(std::uint32_t begin, std::uint32_t end) const {
    if (text.empty() || begin >= end || begin >= origBegin.size()) return {};
    if (end > origEnd.size()) end = static_cast<std::uint32_t>(origEnd.size());
    return Span{origBegin[begin], origEnd[end - 1]};
}

namespace {

// Returns the replacement for a code point. 0 = drop, U'\xFFFF' = keep as-is.
constexpr char32_t kKeep = 0xFFFF;

char32_t mapCodePoint(char32_t cp) {
    if (cp == '\t' || cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000) return ' ';
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D || cp == 0x2060 || cp == 0xFEFF || cp == 0xFE0E || cp == 0xFE0F || cp == 0x20E3) return 0;
    if ((cp >= 0x2010 && cp <= 0x2015) || cp == 0x2212 || cp == 0xFE58 || cp == 0xFE63 || cp == 0xFF0D) return '-';
    if (cp >= 0x0660 && cp <= 0x0669) return U'0' + (cp - 0x0660);  // Arabic-Indic digits
    if (cp >= 0x06F0 && cp <= 0x06F9) return U'0' + (cp - 0x06F0);  // Extended Arabic-Indic digits
    if (cp == 0x066B) return '.';                                    // Arabic decimal separator
    if (cp == 0x066C) return ',';                                    // Arabic thousands separator
    if (cp == 0x00B9) return '1';
    if (cp == 0x00B2) return '2';
    if (cp == 0x00B3) return '3';
    if (cp == 0x2070) return '0';
    if (cp >= 0x2074 && cp <= 0x2079) return U'4' + (cp - 0x2074);  // superscript 4-9
    if (cp >= 0x2080 && cp <= 0x2089) return U'0' + (cp - 0x2080);  // subscript 0-9
    if (cp >= 0xFF10 && cp <= 0xFF19) return U'0' + (cp - 0xFF10);  // fullwidth digits
    if (cp >= 0xFF21 && cp <= 0xFF3A) return U'A' + (cp - 0xFF21);  // fullwidth A-Z
    if (cp >= 0xFF41 && cp <= 0xFF5A) return U'A' + (cp - 0xFF41);  // fullwidth a-z
    if (cp == 0xFF1A) return ':';
    if (cp == 0x2022 || cp == 0x25CF || cp == 0x25AA || cp == 0x2023) return ' ';  // bullets
    // Emoji and pictographic blocks become separators.
    if ((cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) ||
        (cp >= 0x2190 && cp <= 0x21FF) || (cp >= 0x25A0 && cp <= 0x25FF) || (cp >= 0x1F1E6 && cp <= 0x1F1FF) ||
        (cp >= 0xE0000 && cp <= 0xE007F))
        return ' ';
    if (cp == '*' || cp == '_' || cp == '`' || cp == '~') return ' ';  // Markdown decoration
    if (cp == 0x201C || cp == 0x201D || cp == 0x2018 || cp == 0x2019) return '\'';
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    return kKeep;
}

}  // namespace

NormalizedText normalize(std::string_view original) {
    NormalizedText out;
    out.text.reserve(original.size());
    out.origBegin.reserve(original.size());
    out.origEnd.reserve(original.size());
    std::size_t i = 0;
    while (i < original.size()) {
        const std::size_t start = i;
        char32_t cp = decodeUtf8(original, i);
        if (cp == '\r') {
            if (i < original.size() && original[i] == '\n') ++i;  // CRLF -> single LF
            cp = '\n';
        }
        char32_t mapped = mapCodePoint(cp);
        if (mapped == 0) continue;
        if (mapped == kKeep) mapped = cp;
        const std::size_t before = out.text.size();
        appendUtf8(out.text, mapped);
        for (std::size_t k = before; k < out.text.size(); ++k) {
            out.origBegin.push_back(static_cast<std::uint32_t>(start));
            out.origEnd.push_back(static_cast<std::uint32_t>(i));
        }
    }
    return out;
}

std::string toUpperAscii(std::string s) {
    for (auto& c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return s;
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
    return std::string(s.substr(b, e - b));
}

}  // namespace sr
