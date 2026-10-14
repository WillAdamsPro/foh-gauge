#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
using Catch::Approx;
namespace {
foh::MicPosition position(double gain, double coherence, double delayMs) {
    foh::MicPosition p; p.delayMs = delayMs;
    p.tf.fftSize = 1024; p.tf.freq = {0, 100, 1000, 10000};
    for (double f : p.tf.freq) {
        p.tf.H.push_back(std::polar(gain, -2 * M_PI * f * delayMs / 1000));
        p.tf.coherence.push_back(coherence);
    }
    return p;
}
}
TEST_CASE("Multi-position average favours high coherence per bin", "[multipos]") {
    auto a = position(1, 0.9, 0), b = position(3, 0.1, 0);
    b.tf.coherence[1] = 0.9; a.tf.coherence[1] = 0.1;
    auto avg = foh::multiPositionAverage({a, b});
    REQUIRE(avg.H[0].real() == Approx(1.2));
    REQUIRE(avg.magDb[0] == Approx(20 * std::log10(1.2)));
    REQUIRE(avg.H[1].real() == Approx(2.8));
    REQUIRE(avg.coherence[0] == Approx(0.82));
}
TEST_CASE("Each position delay is removed before complex averaging", "[multipos]") {
    auto a = position(1, 1, 4.8), b = position(1, 1, 12.5), c = position(1, 1, -1.25);
    for (const auto& avg : {foh::multiPositionAverage({a, b}), foh::multiPositionAverage({a, b, c})}) {
        REQUIRE(avg.H.size() == 4);
        for (size_t k = 0; k < avg.H.size(); ++k) {
            REQUIRE(avg.magDb[k] == Approx(0).margin(1e-10));
            REQUIRE(avg.phaseDeg[k] == Approx(0).margin(1e-10));
        }
    }
    b.delayMs = 0;
    REQUIRE(foh::multiPositionAverage({a, b}).magDb[1] < -1);
}
TEST_CASE("Multi-position invalid and unsupported bins are safe", "[multipos]") {
    auto a = position(1, 1, 0), b = a;
    REQUIRE(foh::multiPositionAverage({a}).H.empty());
    REQUIRE(foh::multiPositionAverage({a, b, a, b}).H.empty());
    b.tf.freq[1] += 1;
    REQUIRE(foh::multiPositionAverage({a, b}).H.empty());
    b = a; b.tf.H.clear();
    REQUIRE(foh::multiPositionAverage({a, b}).H.empty());
    b = a; b.delayMs = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(foh::multiPositionAverage({a, b}).H.empty());
    b = a; b.tf.coherence[0] = std::numeric_limits<double>::quiet_NaN();
    b.tf.H[1] = {std::numeric_limits<double>::infinity(), 0};
    b.tf.blanked = {0, 0, 1, 0}; a.tf.coherence[3] = b.tf.coherence[3] = 0;
    auto avg = foh::multiPositionAverage({a, b});
    REQUIRE(avg.H[0].real() == Approx(1));
    REQUIRE(avg.H[1].real() == Approx(1));
    REQUIRE(avg.H[2].real() == Approx(1));
    REQUIRE(avg.isBlanked(3));
    REQUIRE(avg.coherence[3] == 0);
    REQUIRE(std::isfinite(avg.magDb[3]));
}
