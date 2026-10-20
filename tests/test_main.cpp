#include "minitest.h"
#include <iostream>

int main() {
    const auto &tests = minitest::registry();
    for (const auto &test : tests) test.fn();

    int failures = minitest::failureCount();
    std::cout << tests.size() << " test case(s) run, " << failures << " assertion failure(s).\n";
    return failures == 0 ? 0 : 1;
}
