#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <fstream>
#include <sstream>
using Catch::Approx;

TEST_CASE("Target parser reads all shipped Smaart files", "[target]") {
    struct Fixture { const char* name; size_t count; double firstDb, lastHz, lastDb; };
    for (const auto& f : {Fixture{"target-plus6.txt", 460, 6.26, 23250, -36.18},
                         Fixture{"target-plus9.txt", 460, 9.26, 23250, -34.66},
                         Fixture{"target-plus12.txt", 459, 12.26, 23062.5, -32.42},
                         Fixture{"target-flat-tolerance.txt", 463, 0, 23812.5, -31.07}}) {
        INFO(f.name);
        std::ifstream input(std::string(FOH_TARGET_DATA) + "/" + f.name, std::ios::binary);
        REQUIRE(input.good()); std::stringstream text; text << input.rdbuf();
        auto t = foh::parseTarget(text.str());
        REQUIRE(t.ok()); REQUIRE(t.hz.size() == f.count); REQUIRE(t.db.size() == f.count);
        REQUIRE(t.hz.front() == Approx(1.464844)); REQUIRE(t.db.front() == Approx(f.firstDb));
        REQUIRE(t.hz.back() == Approx(f.lastHz)); REQUIRE(t.db.back() == Approx(f.lastDb));
        const bool band = std::string(f.name) == "target-flat-tolerance.txt";
        REQUIRE(t.useTolerance == band); REQUIRE(t.toleranceDb == Approx(band ? 3.0 : 0.0));
    }
}
TEST_CASE("Target parser validates numeric data and metadata", "[target]") {
    auto t = foh::parseTarget("\xEF\xBB\xBF" "Tolerance:\t3\r\nUseTolerance:\t1\r\n1000 2\r\n20 -1\r\n20 7\r\n2000 * * *\r\n");
    REQUIRE(t.ok()); REQUIRE(t.hz == std::vector<double>{20, 1000});
    REQUIRE(t.db == std::vector<double>{-1, 2}); REQUIRE(t.toleranceDb == 3); REQUIRE(t.useTolerance);
    for (const auto* bad : {"", "20 1", "20 1\n0 2", "20 1\n100 nan", "20 1\n100 2 junk",
                            "20 1\n100 2 3", "Tolerance: -3\n20 1\n100 2", "UseTolerance: 2\n20 1\n100 2"})
        REQUIRE_FALSE(foh::parseTarget(bad).ok());
}

TEST_CASE("Target tolerance preserves explicit zero and distinguishes missing metadata", "[target][regression]") {
    auto absent = foh::parseTarget("20 0\n1000 1"); REQUIRE(absent.ok());
    CHECK_FALSE(absent.hasTolerance);
    for (auto value : {"0", "0.0", "3"}) {
        auto target = foh::parseTarget(std::string("Tolerance: ") + value + "\nUseTolerance: 1\n20 0\n1000 1");
        REQUIRE(target.ok()); CHECK(target.hasTolerance); CHECK(target.useTolerance);
        CHECK(target.toleranceDb == (std::string(value) == "3" ? 3 : 0));
    }
}
