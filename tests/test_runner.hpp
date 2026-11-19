#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace saycheese::test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        Registry().push_back(TestCase{name, std::move(fn)});
    }
};

class AssertionFailure : public std::runtime_error {
public:
    explicit AssertionFailure(const std::string& msg) : std::runtime_error(msg) {}
};

inline void Fail(const char* file, int line, const std::string& msg) {
    std::ostringstream oss;
    oss << file << ":" << line << ": " << msg;
    throw AssertionFailure(oss.str());
}

inline int RunAll(const char* suiteName) {
    int passed = 0;
    int failed = 0;
    std::cout << "=== Running suite: " << suiteName << " (" << Registry().size() << " tests) ===" << std::endl;
    for (const auto& tc : Registry()) {
        try {
            tc.fn();
            std::cout << "  [PASS] " << tc.name << std::endl;
            ++passed;
        } catch (const std::exception& ex) {
            std::cerr << "  [FAIL] " << tc.name << "\n         " << ex.what() << std::endl;
            ++failed;
        } catch (...) {
            std::cerr << "  [FAIL] " << tc.name << "\n         Unknown exception" << std::endl;
            ++failed;
        }
    }
    std::cout << "=== Result: " << passed << "/" << (passed + failed) << " passed ===" << std::endl;
    return failed == 0 ? 0 : 1;
}

} // namespace saycheese::test

#define SC_TEST_CONCAT_IMPL(a, b) a##b
#define SC_TEST_CONCAT(a, b) SC_TEST_CONCAT_IMPL(a, b)

#define SC_TEST(name)                                                                            \
    static void SC_TEST_CONCAT(sc_test_fn_, __LINE__)();                                         \
    static ::saycheese::test::Registrar SC_TEST_CONCAT(sc_test_reg_, __LINE__)(                  \
        name, &SC_TEST_CONCAT(sc_test_fn_, __LINE__));                                           \
    static void SC_TEST_CONCAT(sc_test_fn_, __LINE__)()

#define SC_EXPECT_TRUE(cond)                                                                     \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            ::saycheese::test::Fail(__FILE__, __LINE__, "Expected true: " #cond);                \
        }                                                                                        \
    } while (false)

#define SC_EXPECT_FALSE(cond)                                                                    \
    do {                                                                                         \
        if ((cond)) {                                                                            \
            ::saycheese::test::Fail(__FILE__, __LINE__, "Expected false: " #cond);               \
        }                                                                                        \
    } while (false)

#define SC_EXPECT_EQ(actual, expected)                                                           \
    do {                                                                                         \
        const auto& _sc_a = (actual);                                                            \
        const auto& _sc_e = (expected);                                                          \
        if (!(_sc_a == _sc_e)) {                                                                 \
            std::ostringstream _sc_oss;                                                          \
            _sc_oss << "Expected " #actual " == " #expected " (actual=" << _sc_a                 \
                    << ", expected=" << _sc_e << ")";                                            \
            ::saycheese::test::Fail(__FILE__, __LINE__, _sc_oss.str());                          \
        }                                                                                        \
    } while (false)

#define SC_EXPECT_NEAR(actual, expected, eps)                                                    \
    do {                                                                                         \
        const double _sc_a = static_cast<double>(actual);                                        \
        const double _sc_e = static_cast<double>(expected);                                      \
        const double _sc_eps = static_cast<double>(eps);                                         \
        if (std::fabs(_sc_a - _sc_e) > _sc_eps) {                                                \
            std::ostringstream _sc_oss;                                                          \
            _sc_oss << "Expected |" #actual " - " #expected "| <= " << _sc_eps                   \
                    << " (actual=" << _sc_a << ", expected=" << _sc_e << ")";                    \
            ::saycheese::test::Fail(__FILE__, __LINE__, _sc_oss.str());                          \
        }                                                                                        \
    } while (false)
