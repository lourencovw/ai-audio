#pragma once
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// A minimal, header-only test framework with no external dependencies.
// Supports self-registering TEST_CASE blocks and simple CHECK assertions.
namespace minitest {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase> &registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string &name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline int &failureCount() {
    static int count = 0;
    return count;
}

inline void reportFailure(const char *file, int line, const std::string &message) {
    std::cerr << file << ":" << line << ": FAILED: " << message << "\n";
    ++failureCount();
}

} // namespace minitest

#define TEST_CASE(name)                                                 \
    static void name();                                                 \
    static ::minitest::Registrar name##_registrar(#name, name);         \
    static void name()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) ::minitest::reportFailure(__FILE__, __LINE__, #cond); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                        \
    do {                                                                             \
        if (std::fabs((a) - (b)) > (eps))                                            \
            ::minitest::reportFailure(                                               \
                __FILE__, __LINE__,                                                   \
                std::string(#a " ~= " #b " (got ") + std::to_string(a) + " vs " +     \
                    std::to_string(b) + ")");                                        \
    } while (0)
