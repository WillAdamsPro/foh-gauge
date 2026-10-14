#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <cmath>
#include "fohcore/json.h"
#include <limits>
using Catch::Approx;
TEST_CASE("SPL calibration: synthetic 1 kHz -20 dBFS reads 94 dB SPL", "[splcal]") {
    for (double rate : {44100., 48000., 96000.}) {
        std::vector<float> x(static_cast<size_t>(rate)); double energy = 0;
        for (size_t i = 0; i < x.size(); ++i) { x[i] = float(0.1 * std::sin(2 * 3.141592653589793 * 1000 * i / rate)); energy += double(x[i]) * x[i]; }
        double measured = 10 * std::log10(2 * energy / x.size());
        foh::SplCal c; REQUIRE(c.level(measured) == Approx(-20).margin(0.01)); REQUIRE(std::string(c.unit()) == "dBFS");
        REQUIRE(c.calibrate(measured, 94)); REQUIRE(std::string(c.unit()) == "dB SPL");
        REQUIRE(c.level(measured) == Approx(94).margin(0.1));
        for (int fraction : {3, 6}) {
            foh::RTA r(16384, rate, fraction); r.process(x.data(), int(x.size())); auto levels = r.bandsDb();
            for (size_t i = 0; i < r.bands().size(); ++i) if (std::abs(r.bands()[i].fCenter - 1000) < 1)
                REQUIRE(c.level(levels[i]) == Approx(94).margin(0.1));
        }
        REQUIRE(c.calibrate(measured, 114)); REQUIRE(c.level(measured) == Approx(114).margin(0.1));
        REQUIRE(c.calibrate(measured, 87.3)); REQUIRE(c.level(measured) == Approx(87.3));
    }
}
TEST_CASE("SPL sensitivity is relative to calibration and excludes TF correction", "[splcal]") {
    auto mic = foh::parseMicCal("Sens Factor =-2.1\n100 1.5\n1000 1.5\n10000 1.5\n");
    foh::SplCal c;
    double corrected = -20 - mic.dbAt(1000);
    REQUIRE(c.calibrate(corrected, 94, mic.sensitivityOffsetDb));
    REQUIRE(c.level(corrected, -2.1) == Approx(94));
    REQUIRE(c.level(corrected - 6, -2.1) == Approx(88));
    REQUIRE(c.level(corrected, -1.1) == Approx(95));
    REQUIRE(c.level(corrected, 0) == Approx(96.1));
    REQUIRE(mic.dbAt(1000) == Approx(1.5));
    foh::SplCal uncal; REQUIRE(uncal.level(-20, -2.1) == -20);
}
TEST_CASE("SPL rejects absent, clipped and invalid calibration levels", "[splcal]") {
    foh::SplCal c; REQUIRE(c.calibrate(-20, 94));
    for (double measured : {-120., 0., 1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE_FALSE(c.calibrate(measured, 94)); REQUIRE(c.level(-20) == Approx(94));
    }
    for (double known : {-1., 0., 161., std::numeric_limits<double>::quiet_NaN()}) REQUIRE_FALSE(c.calibrate(-20, known));
    REQUIRE_FALSE(c.calibrate(-20, 94, std::numeric_limits<double>::infinity()));
}

TEST_CASE("SPL JSON preserves calibration and rejects incomplete data", "[splcal]") {
    foh::SplCal c; REQUIRE(c.calibrate(-20, 94, -2.1));
    auto copy = foh::SplCal::fromJson(foh::Json::parse(c.toJson().dump()));
    REQUIRE(copy.calibrated); REQUIRE(copy.level(-20, -2.1) == Approx(94));
    REQUIRE(copy.level(-20, 0) == Approx(96.1));
    REQUIRE_FALSE(foh::SplCal::fromJson(foh::Json{}).calibrated);
    auto invalid = foh::Json::object(); invalid.set("offsetDb", 114);
    REQUIRE_FALSE(foh::SplCal::fromJson(invalid).calibrated);
    invalid.set("sensitivityDb", "bad"); REQUIRE_FALSE(foh::SplCal::fromJson(invalid).calibrated);
    REQUIRE(foh::SplCal{}.toJson().isNull());
}
