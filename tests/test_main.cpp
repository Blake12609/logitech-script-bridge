#include <cstring>

#include "test.h"

namespace test {
std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}
int failures = 0;
}  // namespace test

// ./tests [filter]  runs every test whose name contains `filter`
int main(int argc, char** argv) {
    int run = 0;
    for (const auto& c : test::registry()) {
        if (argc > 1 && !std::strstr(c.name, argv[1])) continue;
        const int before = test::failures;
        c.fn();
        run++;
        std::printf("%s %s\n", test::failures == before ? "ok  " : "FAIL", c.name);
    }
    std::printf("%s: %d tests, %d failure%s\n", test::failures ? "FAILED" : "PASSED", run, test::failures,
                test::failures == 1 ? "" : "s");
    return test::failures ? 1 : 0;
}
