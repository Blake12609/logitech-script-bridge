// Tiny test framework: TEST(name) { CHECK(cond); }
#pragma once

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

namespace test {
struct Case {
    const char* name;
    void (*fn)();
};
std::vector<Case>& registry();
extern int failures;
struct Register {
    Register(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};
inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
}  // namespace test

#define TEST(name)                                           \
    static void name();                                      \
    static test::Register register_##name(#name, name);      \
    static void name()

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            test::failures++;                                                \
        }                                                                    \
    } while (0)
