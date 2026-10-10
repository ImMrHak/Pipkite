#pragma once
// Non-backtracking regular expressions (Thompson NFA simulated by a Pike VM).
// Matching time is O(pattern x input); there is no catastrophic backtracking.
// Supported: literals, '.', [classes] with ranges and negation, \d \w \s \D \W \S,
// escapes, ^ $, groups ( ) and (?: ), alternation |, quantifiers * + ? {m} {m,} {m,n}
// (lazy suffix '?' accepted). Rejected: backreferences, lookaround, possessive
// quantifiers and other constructs that require backtracking.

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Result.h"

namespace sr {

inline constexpr std::size_t kMaxRegexPatternLength = 512;
inline constexpr int kMaxRegexRepeat = 32;
inline constexpr std::size_t kMaxRegexProgram = 4096;

struct RegexMatch {
    std::size_t begin = 0;
    std::size_t end = 0;
    std::vector<std::optional<std::pair<std::size_t, std::size_t>>> groups;  // index 0 = whole match
};

class BoundedRegex {
public:
    static Result<std::shared_ptr<const BoundedRegex>> compile(std::string_view pattern, bool caseInsensitive = true);

    // Leftmost-first search, like Perl/ECMAScript priorities, but in linear time.
    std::optional<RegexMatch> search(std::string_view input) const;
    int groupCount() const { return groups_; }

    struct Inst;

private:
    std::vector<Inst> prog_;
    int groups_ = 0;
    bool icase_ = true;
    friend class RegexCompiler;
};

struct BoundedRegex::Inst {
    enum Op { Char, Any, Class, Split, Jmp, Save, Match, Bol, Eol } op = Match;
    char c = 0;
    bool negate = false;
    std::vector<std::pair<unsigned char, unsigned char>> ranges;
    int x = 0, y = 0;  // jump targets / save slot
};

}  // namespace sr
