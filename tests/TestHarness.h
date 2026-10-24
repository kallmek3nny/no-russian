#pragma once
// Shared test harness (Milestone A): lightweight CHECK / CHECK_EQ macros,
// named sections, and a single summary/exit code used by every test binary.
#include <cstdio>
#include <string_view>

namespace lt::test {

struct State {
    int checks = 0;
    int failures = 0;
    const char* section = "";
};

inline State& state() noexcept {
    static State s;
    return s;
}

inline void section(const char* name) noexcept { state().section = name; }

inline bool check(bool ok, const char* what, const char* file, int line) noexcept {
    State& s = state();
    ++s.checks;
    if (!ok) {
        ++s.failures;
        std::printf("FAIL %s:%d [%s]: %s\n", file, line, s.section, what);
    }
    return ok;
}

// Prints the summary and returns the process exit code.
inline int finish(std::string_view suite) noexcept {
    const State& s = state();
    if (s.failures == 0) {
        std::printf("%.*s: all %d checks passed\n", static_cast<int>(suite.size()), suite.data(),
                    s.checks);
        return 0;
    }
    std::printf("%.*s: %d of %d checks failed\n", static_cast<int>(suite.size()), suite.data(),
                s.failures, s.checks);
    return 1;
}

} // namespace lt::test

#define CHECK(expr) ::lt::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::lt::test::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define TEST_SECTION(name) ::lt::test::section(name)
