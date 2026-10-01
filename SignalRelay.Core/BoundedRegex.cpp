#include "BoundedRegex.h"

#include <cstdint>
#include <limits>

namespace sr {

namespace {

struct Node {
    enum Kind { Lit, Any, Class, Cat, Alt, Group, Repeat, Bol, Eol, Empty } kind = Empty;
    char c = 0;
    bool negate = false;
    std::vector<std::pair<unsigned char, unsigned char>> ranges;
    std::vector<std::unique_ptr<Node>> kids;
    int capture = -1;
    int min = 0, max = 0;  // max -1 = unbounded
    bool greedy = true;
};

using NodePtr = std::unique_ptr<Node>;

struct ParseError {
    std::string message;
};

class Parser {
public:
    explicit Parser(std::string_view p) : p_(p) {}
    int groups = 0;

    NodePtr parse() {
        auto n = parseAlt();
        if (pos_ != p_.size()) fail("unmatched ')'");
        return n;
    }

private:
    std::string_view p_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& m) { throw ParseError{m + " at position " + std::to_string(pos_)}; }
    bool eof() const { return pos_ >= p_.size(); }
    char peek() const { return p_[pos_]; }

    NodePtr make(Node::Kind k) {
        auto n = std::make_unique<Node>();
        n->kind = k;
        return n;
    }

    NodePtr parseAlt() {
        auto left = parseCat();
        if (!eof() && peek() == '|') {
            auto alt = make(Node::Alt);
            alt->kids.push_back(std::move(left));
            while (!eof() && peek() == '|') {
                ++pos_;
                alt->kids.push_back(parseCat());
            }
            return alt;
        }
        return left;
    }

    NodePtr parseCat() {
        auto cat = make(Node::Cat);
        while (!eof() && peek() != '|' && peek() != ')') cat->kids.push_back(parseRepeat());
        if (cat->kids.empty()) return make(Node::Empty);
        if (cat->kids.size() == 1) return std::move(cat->kids[0]);
        return cat;
    }

    int parseInt() {
        int v = 0;
        bool any = false;
        while (!eof() && peek() >= '0' && peek() <= '9') {
            v = v * 10 + (peek() - '0');
            if (v > 1000) fail("repeat count too large");
            ++pos_;
            any = true;
        }
        if (!any) fail("expected number in {m,n}");
        return v;
    }

    NodePtr parseRepeat() {
        auto atom = parseAtom();
        if (eof()) return atom;
        char q = peek();
        int mn = -1, mx = -1;
        if (q == '*') { mn = 0; mx = -1; ++pos_; }
        else if (q == '+') { mn = 1; mx = -1; ++pos_; }
        else if (q == '?') { mn = 0; mx = 1; ++pos_; }
        else if (q == '{') {
            ++pos_;
            mn = parseInt();
            mx = mn;
            if (!eof() && peek() == ',') {
                ++pos_;
                mx = (!eof() && peek() == '}') ? -1 : parseInt();
            }
            if (eof() || peek() != '}') fail("expected '}'");
            ++pos_;
            if (mx != -1 && mx < mn) fail("invalid repeat range");
            if (mn > kMaxRegexRepeat || mx > kMaxRegexRepeat) fail("repeat count exceeds " + std::to_string(kMaxRegexRepeat));
        } else {
            return atom;
        }
        if (atom->kind == Node::Bol || atom->kind == Node::Eol || atom->kind == Node::Empty) fail("nothing to repeat");
        auto rep = make(Node::Repeat);
        rep->min = mn;
        rep->max = mx;
        if (!eof() && peek() == '?') { rep->greedy = false; ++pos_; }
        else if (!eof() && peek() == '+') fail("possessive quantifiers are not supported");
        if (!eof() && (peek() == '*' || peek() == '+' || peek() == '?' || peek() == '{')) fail("nested quantifier");
        rep->kids.push_back(std::move(atom));
        return rep;
    }

    void addEscapeClass(Node& n, char e) {
        switch (e) {
            case 'd': n.ranges.push_back({'0', '9'}); break;
            case 'w':
                n.ranges.push_back({'0', '9'});
                n.ranges.push_back({'A', 'Z'});
                n.ranges.push_back({'a', 'z'});
                n.ranges.push_back({'_', '_'});
                n.ranges.push_back({static_cast<unsigned char>(0x80), static_cast<unsigned char>(0xFF)});
                break;
            case 's':
                n.ranges.push_back({' ', ' '});
                n.ranges.push_back({'\t', '\t'});
                n.ranges.push_back({'\n', '\n'});
                n.ranges.push_back({'\r', '\r'});
                break;
            default: fail("unsupported class escape");
        }
    }

    char literalEscape(char e) {
        if (e == 'n') return '\n';
        if (e == 't') return '\t';
        if (e == 'r') return '\r';
        if ((e >= '0' && e <= '9')) fail("backreferences are not supported");
        if ((e >= 'a' && e <= 'z') || (e >= 'A' && e <= 'Z')) fail(std::string("unsupported escape \\") + e);
        return e;
    }

    NodePtr parseAtom() {
        char c = peek();
        if (c == '(') {
            ++pos_;
            int cap = -1;
            if (!eof() && peek() == '?') {
                if (pos_ + 1 < p_.size() && p_[pos_ + 1] == ':') pos_ += 2;
                else fail("lookaround and inline flags are not supported");
            } else {
                cap = ++groups;
            }
            auto inner = parseAlt();
            if (eof() || peek() != ')') fail("missing ')'");
            ++pos_;
            auto g = make(Node::Group);
            g->capture = cap;
            g->kids.push_back(std::move(inner));
            return g;
        }
        if (c == '[') return parseClass();
        if (c == '.') { ++pos_; return make(Node::Any); }
        if (c == '^') { ++pos_; return make(Node::Bol); }
        if (c == '$') { ++pos_; return make(Node::Eol); }
        if (c == '*' || c == '+' || c == '?' || c == '{') fail("nothing to repeat");
        if (c == ')') fail("unmatched ')'");
        if (c == '\\') {
            ++pos_;
            if (eof()) fail("trailing backslash");
            char e = peek();
            ++pos_;
            if (e == 'd' || e == 'w' || e == 's' || e == 'D' || e == 'W' || e == 'S') {
                auto n = make(Node::Class);
                n->negate = (e >= 'A' && e <= 'Z');
                addEscapeClass(*n, static_cast<char>(e | 0x20));
                return n;
            }
            if (e == 'b' || e == 'B') fail("word boundaries are not supported");
            auto n = make(Node::Lit);
            n->c = literalEscape(e);
            return n;
        }
        ++pos_;
        auto n = make(Node::Lit);
        n->c = c;
        return n;
    }

    NodePtr parseClass() {
        ++pos_;  // '['
        auto n = make(Node::Class);
        if (!eof() && peek() == '^') { n->negate = true; ++pos_; }
        bool first = true;
        while (!eof() && (peek() != ']' || first)) {
            first = false;
            unsigned char lo;
            if (peek() == '\\') {
                ++pos_;
                if (eof()) fail("trailing backslash");
                char e = peek();
                ++pos_;
                if (e == 'd' || e == 'w' || e == 's') { addEscapeClass(*n, e); continue; }
                if (e == 'D' || e == 'W' || e == 'S') fail("negated escapes inside classes are not supported");
                lo = static_cast<unsigned char>(literalEscape(e));
            } else {
                lo = static_cast<unsigned char>(peek());
                ++pos_;
            }
            unsigned char hi = lo;
            if (pos_ + 1 < p_.size() && peek() == '-' && p_[pos_ + 1] != ']') {
                ++pos_;
                if (peek() == '\\') {
                    ++pos_;
                    if (eof()) fail("trailing backslash");
                    hi = static_cast<unsigned char>(literalEscape(peek()));
                    ++pos_;
                } else {
                    hi = static_cast<unsigned char>(peek());
                    ++pos_;
                }
                if (hi < lo) fail("invalid class range");
            }
            n->ranges.push_back({lo, hi});
        }
        if (eof()) fail("missing ']'");
        ++pos_;
        return n;
    }
};

using Inst = BoundedRegex::Inst;

class Emitter {
public:
    std::vector<Inst> prog;

    void emit(const Node& n) {
        if (prog.size() > kMaxRegexProgram) throw ParseError{"pattern too complex"};
        switch (n.kind) {
            case Node::Empty: break;
            case Node::Lit: { Inst i; i.op = Inst::Char; i.c = n.c; prog.push_back(i); break; }
            case Node::Any: { Inst i; i.op = Inst::Any; prog.push_back(i); break; }
            case Node::Class: { Inst i; i.op = Inst::Class; i.negate = n.negate; i.ranges = n.ranges; prog.push_back(i); break; }
            case Node::Bol: { Inst i; i.op = Inst::Bol; prog.push_back(i); break; }
            case Node::Eol: { Inst i; i.op = Inst::Eol; prog.push_back(i); break; }
            case Node::Cat:
                for (auto& k : n.kids) emit(*k);
                break;
            case Node::Group:
                if (n.capture >= 0) { Inst s; s.op = Inst::Save; s.x = n.capture * 2; prog.push_back(s); }
                emit(*n.kids[0]);
                if (n.capture >= 0) { Inst s; s.op = Inst::Save; s.x = n.capture * 2 + 1; prog.push_back(s); }
                break;
            case Node::Alt: {
                std::vector<std::size_t> jumps;
                for (std::size_t k = 0; k < n.kids.size(); ++k) {
                    if (k + 1 < n.kids.size()) {
                        std::size_t split = prog.size();
                        prog.push_back(Inst{});
                        prog[split].op = Inst::Split;
                        prog[split].x = static_cast<int>(prog.size());
                        emit(*n.kids[k]);
                        jumps.push_back(prog.size());
                        prog.push_back(Inst{});
                        prog.back().op = Inst::Jmp;
                        prog[split].y = static_cast<int>(prog.size());
                    } else {
                        emit(*n.kids[k]);
                    }
                }
                for (auto j : jumps) prog[j].x = static_cast<int>(prog.size());
                break;
            }
            case Node::Repeat: emitRepeat(n); break;
        }
    }

private:
    void split(std::size_t at, int preferred, int other, bool greedy) {
        prog[at].op = Inst::Split;
        prog[at].x = greedy ? preferred : other;
        prog[at].y = greedy ? other : preferred;
    }

    void emitRepeat(const Node& n) {
        const Node& body = *n.kids[0];
        for (int k = 0; k < n.min; ++k) emit(body);
        if (n.max == -1) {
            // L1: split L2, L3; L2: body; jmp L1; L3:
            std::size_t l1 = prog.size();
            prog.push_back(Inst{});
            emit(body);
            Inst j;
            j.op = Inst::Jmp;
            j.x = static_cast<int>(l1);
            prog.push_back(j);
            split(l1, static_cast<int>(l1 + 1), static_cast<int>(prog.size()), n.greedy);
        } else {
            std::vector<std::size_t> splits;
            for (int k = n.min; k < n.max; ++k) {
                std::size_t s = prog.size();
                prog.push_back(Inst{});
                splits.push_back(s);
                emit(body);
            }
            for (auto s : splits) split(s, static_cast<int>(s + 1), static_cast<int>(prog.size()), n.greedy);
        }
    }
};

}  // namespace

class RegexCompiler {
public:
    static Result<std::shared_ptr<const BoundedRegex>> run(std::string_view pattern, bool icase) {
        if (pattern.size() > kMaxRegexPatternLength) return Error{"regex_too_long", "pattern exceeds 512 characters"};
        try {
            Parser parser(pattern);
            auto ast = parser.parse();
            Emitter em;
            Inst s0;
            s0.op = Inst::Save;
            s0.x = 0;
            em.prog.push_back(s0);
            em.emit(*ast);
            Inst s1;
            s1.op = Inst::Save;
            s1.x = 1;
            em.prog.push_back(s1);
            Inst m;
            m.op = Inst::Match;
            em.prog.push_back(m);
            if (em.prog.size() > kMaxRegexProgram) return Error{"regex_too_complex", "pattern expands beyond the program limit"};
            auto re = std::shared_ptr<BoundedRegex>(new BoundedRegex());
            re->prog_ = std::move(em.prog);
            re->groups_ = parser.groups;
            re->icase_ = icase;
            return std::shared_ptr<const BoundedRegex>(re);
        } catch (const ParseError& e) {
            return Error{"regex_unsupported", e.message};
        }
    }
};

Result<std::shared_ptr<const BoundedRegex>> BoundedRegex::compile(std::string_view pattern, bool caseInsensitive) {
    return RegexCompiler::run(pattern, caseInsensitive);
}

namespace {

inline unsigned char fold(unsigned char c) { return (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 32) : c; }

struct Thread {
    int pc;
    std::vector<std::size_t> caps;
};

}  // namespace

std::optional<RegexMatch> BoundedRegex::search(std::string_view in) const {
    constexpr std::size_t kUnset = std::numeric_limits<std::size_t>::max();
    const std::size_t nslots = static_cast<std::size_t>(groups_ + 1) * 2;
    std::vector<std::size_t> stamp(prog_.size(), kUnset);
    std::vector<Thread> clist, nlist;
    std::optional<std::vector<std::size_t>> matched;

    auto classMatch = [&](const Inst& ins, unsigned char ch) {
        bool hit = false;
        for (auto& r : ins.ranges) {
            if (ch >= r.first && ch <= r.second) { hit = true; break; }
            if (icase_) {
                unsigned char f = fold(ch);
                unsigned char lower = (f >= 'A' && f <= 'Z') ? static_cast<unsigned char>(f + 32) : f;
                if ((f >= r.first && f <= r.second) || (lower >= r.first && lower <= r.second)) { hit = true; break; }
            }
        }
        return hit != ins.negate;
    };

    // Iterative epsilon closure preserving priority order (explicit stack, no recursion depth issues).
    auto addThread = [&](std::vector<Thread>& list, int pc0, const std::vector<std::size_t>& caps0, std::size_t pos) {
        std::vector<std::pair<int, std::vector<std::size_t>>> stack;
        stack.push_back({pc0, caps0});
        while (!stack.empty()) {
            auto [pc, caps] = std::move(stack.back());
            stack.pop_back();
            if (stamp[pc] == pos) continue;
            stamp[pc] = pos;
            const Inst& ins = prog_[pc];
            switch (ins.op) {
                case Inst::Jmp: stack.push_back({ins.x, std::move(caps)}); break;
                case Inst::Split:
                    stack.push_back({ins.y, caps});  // lower priority pushed first
                    stack.push_back({ins.x, std::move(caps)});
                    break;
                case Inst::Save:
                    caps[ins.x] = pos;
                    stack.push_back({pc + 1, std::move(caps)});
                    break;
                case Inst::Bol:
                    if (pos == 0 || in[pos - 1] == '\n') stack.push_back({pc + 1, std::move(caps)});
                    break;
                case Inst::Eol:
                    if (pos == in.size() || in[pos] == '\n') stack.push_back({pc + 1, std::move(caps)});
                    break;
                default: list.push_back(Thread{pc, std::move(caps)}); break;
            }
        }
    };

    std::vector<std::size_t> empty(nslots, kUnset);
    for (std::size_t pos = 0; pos <= in.size(); ++pos) {
        if (!matched) addThread(clist, 0, empty, pos);
        if (clist.empty()) {
            if (matched) break;
            continue;  // no live thread here (e.g. '^' failed mid-line); keep scanning later positions
        }
        nlist.clear();
        for (auto& t : clist) {
            const Inst& ins = prog_[t.pc];
            if (ins.op == Inst::Match) {
                matched = t.caps;
                break;  // cut lower-priority threads
            }
            if (pos >= in.size()) continue;
            const unsigned char ch = static_cast<unsigned char>(in[pos]);
            bool ok = false;
            if (ins.op == Inst::Char) ok = icase_ ? fold(ch) == fold(static_cast<unsigned char>(ins.c)) : ch == static_cast<unsigned char>(ins.c);
            else if (ins.op == Inst::Any) ok = ch != '\n';
            else if (ins.op == Inst::Class) ok = classMatch(ins, ch);
            if (ok) addThread(nlist, t.pc + 1, t.caps, pos + 1);
        }
        std::swap(clist, nlist);
    }
    if (!matched) return std::nullopt;
    RegexMatch m;
    auto& caps = *matched;
    m.begin = caps[0];
    m.end = caps[1];
    m.groups.resize(groups_ + 1);
    for (int g = 0; g <= groups_; ++g) {
        if (caps[g * 2] != kUnset && caps[g * 2 + 1] != kUnset) m.groups[g] = std::make_pair(caps[g * 2], caps[g * 2 + 1]);
    }
    return m;
}

}  // namespace sr
