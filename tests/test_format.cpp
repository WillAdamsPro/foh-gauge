#include <catch2/catch_test_macros.hpp>
#include "fohcore/format.h"
#include <limits>
#include <utility>

TEST_CASE("D-DSP console delay display: 0.01 ms, half away from zero", "[PartA][D-DSP]") {
    using foh::formatConsoleDelayMs;
    REQUIRE(formatConsoleDelayMs(230.4 / 48000 * 1000) == "4.80");
    REQUIRE(formatConsoleDelayMs(230.0 / 48000 * 1000) == "4.79");
    for (auto [ms, expected] : {std::pair{1.005, "1.01"}, {2.675, "2.68"},
                               {1.115, "1.12"}, {8.345, "8.35"},
                               {0.995, "1.00"}, {9.995, "10.00"},
                               {12.995, "13.00"}, {4.805, "4.81"}}) {
        INFO("ms " << ms);
        REQUIRE(formatConsoleDelayMs(ms) == expected);
        REQUIRE(formatConsoleDelayMs(-ms) == std::string("-") + expected);
    }

    // Pin the deliberate double-rounding window from the 0.1 us snap.
    REQUIRE(formatConsoleDelayMs(4.8049) == "4.80");
    REQUIRE(formatConsoleDelayMs(4.804949) == "4.80");
    REQUIRE(formatConsoleDelayMs(4.80495) == "4.81");
    REQUIRE(formatConsoleDelayMs(-4.804949) == "-4.80");
    REQUIRE(formatConsoleDelayMs(-4.80495) == "-4.81");
    REQUIRE(formatConsoleDelayMs(1.0049999999) == "1.01");

    REQUIRE(formatConsoleDelayMs(0.005) == "0.01");
    REQUIRE(formatConsoleDelayMs(-0.005) == "-0.01");
    for (double ms : {0.0, -0.0, 0.001, -0.001, 0.0049, -0.0049, 1e-320, -1e-320})
        REQUIRE(formatConsoleDelayMs(ms) == "0.00");

    REQUIRE(formatConsoleDelayMs(999999999.995) == "1000000000.00");
    REQUIRE(formatConsoleDelayMs(1e9) == "1000000000.00");
    REQUIRE(formatConsoleDelayMs(-1e9) == "-1000000000.00");
    for (double ms : {std::numeric_limits<double>::quiet_NaN(),
                      std::numeric_limits<double>::infinity(),
                      -std::numeric_limits<double>::infinity(),
                      1e9 + 1, -1e9 - 1, std::numeric_limits<double>::max()})
        REQUIRE(formatConsoleDelayMs(ms) == "--");
}
