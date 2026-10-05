#pragma once
// Minimal self-contained unit-test framework (no external dependency, runs in CI or from VS).

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace srtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

struct Failure {
    std::string message;
};

inline int& checkCount() {
    static int c = 0;
    return c;
}

template <typename A, typename B>
std::string describe(const A& a, const B& b) {
    std::ostringstream os;
    os.precision(12);
    os << "[" << a << "] vs [" << b << "]";
    return os.str();
}

}  // namespace srtest

#define SR_CONCAT2(a, b) a##b
#define SR_CONCAT(a, b) SR_CONCAT2(a, b)
#define TEST(name)                                                              \
    static void SR_CONCAT(test_, name)();                                       \
    static srtest::Registrar SR_CONCAT(reg_, name)(#name, SR_CONCAT(test_, name)); \
    static void SR_CONCAT(test_, name)()

#define CHECK(cond)                                                                                           \
    do {                                                                                                      \
        ++srtest::checkCount();                                                                               \
        if (!(cond)) throw srtest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK(" #cond ")"}; \
    } while (0)

#define CHECK_EQ(a, b)                                                                                                                  \
    do {                                                                                                                                \
        ++srtest::checkCount();                                                                                                         \
        auto&& _a = (a);                                                                                                                \
        auto&& _b = (b);                                                                                                                \
        if (!(_a == _b)) throw srtest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK_EQ(" #a ", " #b ") " + srtest::describe(_a, _b)}; \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                                                  \
    do {                                                                                                                                       \
        ++srtest::checkCount();                                                                                                                \
        double _a = (a), _b = (b);                                                                                                             \
        if (!(std::fabs(_a - _b) <= (eps))) throw srtest::Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": CHECK_NEAR(" #a ", " #b ") " + srtest::describe(_a, _b)}; \
    } while (0)
