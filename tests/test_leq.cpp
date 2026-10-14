#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
using Catch::Approx;
static void tone(foh::LeqMeter& meter, double fs, double seconds, double amplitude=0.1, double frequency=1000) {
    std::vector<float> block(1000); int total=int(fs*seconds);
    for (int at=0; at<total;) {
        int n=std::min(1000,total-at);
        for (int i=0; i<n; ++i) block[i]=float(amplitude*std::sin(2*3.141592653589793*frequency*(at+i)/fs));
        meter.process(block.data(),n); at+=n;
    }
}
TEST_CASE("Calibrated SPL and constant-signal Leq read 94 dB", "[leq]") {
    for (double fs : {44100.,48000.,96000.}) {
        foh::LeqMeter m(fs); foh::SplCal c; REQUIRE(c.calibrate(-20,94));
        tone(m,fs,2); auto r=m.reading();
        for (int w : {0,1,2}) {
            REQUIRE(c.level(r.level[w])==Approx(94).margin(0.1));
            REQUIRE(c.level(r.minute[w])==Approx(94).margin(0.1));
            REQUIRE(c.level(r.fifteenMinutes[w])==Approx(94).margin(0.1));
            REQUIRE(c.level(r.session[w])==Approx(94).margin(0.1));
        }
        REQUIRE(r.seconds==Approx(2));
    }
}
TEST_CASE("Leq averages energy and expires rolling windows", "[leq]") {
    foh::LeqMeter m(8000); tone(m,8000,900,0.1); tone(m,8000,900,0.01);
    auto r=m.reading(); REQUIRE(r.minute[2]==Approx(-40).margin(0.01));
    REQUIRE(r.fifteenMinutes[2]==Approx(-40).margin(0.01));
    REQUIRE(r.session[2]==Approx(10*std::log10((0.01+0.0001)/2)).margin(0.01));
    m.reset(); REQUIRE(m.reading().seconds==0); REQUIRE(m.reading().session[2]==-120);
    tone(m,8000,60.05); REQUIRE(m.reading().minute[2]==Approx(-20).margin(0.01));
}
TEST_CASE("A and C weighting attenuate low frequencies relative to Z", "[leq]") {
    foh::LeqMeter m(48000); tone(m,48000,3,0.1,100); auto r=m.reading();
    REQUIRE(r.session[0]-r.session[2]==Approx(-19.1).margin(0.3));
    REQUIRE(r.session[1]-r.session[2]==Approx(-0.3).margin(0.2));
    float bad[] = {NAN, INFINITY, -INFINITY}; m.process(bad,3);
    for (auto value:m.reading().session) REQUIRE(std::isfinite(value));
    REQUIRE_THROWS_AS(foh::LeqMeter(0),std::invalid_argument);
}
TEST_CASE("Exposure presets use A weighted limits and NIOSH duration", "[leq]") {
    using P=foh::ExposurePreset;
    REQUIRE_FALSE(foh::exposureWarning(P::WHO,99.9,110,60).exceeded);
    REQUIRE(foh::exposureWarning(P::WHO,100,70,60).exceeded);
    REQUIRE_FALSE(foh::exposureWarning(P::NIOSH,110,85,28799).exceeded);
    REQUIRE(foh::exposureWarning(P::NIOSH,70,85,28800).exceeded);
    REQUIRE(foh::exposureWarning(P::NIOSH,70,94,3600).dosePercent==Approx(100));
    REQUIRE_FALSE(foh::exposureWarning(P::NIOSH,70,94,0).exceeded);
}

// Skip only the repeated weighting-filter work. finishBucket is the production
// path for rolling history, session energy and the 64-bit sample clock.
namespace foh {
struct LeqMeterSoakAccess {
    static void bucket(LeqMeter& meter, const std::array<double, 3>& meanEnergy) {
        for (int w = 0; w < 3; ++w) meter.partial[w] = meanEnergy[w] * meter.bucketSamples;
        meter.fill = meter.bucketSamples;
        meter.finishBucket();
    }
    static auto storage(const LeqMeter& meter) { return meter.history.data(); }
    static int historyCount(const LeqMeter& meter) { return meter.count; }
    static uint64_t sampleCount(const LeqMeter& meter) { return meter.samples; }
    static double energy(const LeqMeter& meter, int weighting) { return meter.total[weighting]; }
};
}

TEST_CASE("Twelve-hour Leq buckets retain quiet energy and bounded rolling history", "[leq][stress]") {
    using Access = foh::LeqMeterSoakAccess;
    for (double fs : {8000., 384000.}) {
        INFO("sample rate " << fs);
        foh::LeqMeter meter(fs);
        const auto* storage = Access::storage(meter);
        constexpr int loudBuckets = 3 * 36000, quietBuckets = 9 * 36000;
        for (int i = 0; i < loudBuckets; ++i) Access::bucket(meter, {1, 1, 1});
        for (int i = 0; i < quietBuckets; ++i) {
            Access::bucket(meter, {1e-12, 1e-10, 1e-8});
            if (i % 36000 == 0) {
                CHECK(Access::storage(meter) == storage);
                CHECK(Access::historyCount(meter) == 9000);
                CHECK(std::isfinite(meter.reading().session[0]));
            }
        }
        const auto r = meter.reading();
        CHECK(r.seconds == Approx(12 * 3600).epsilon(1e-14));
        CHECK(Access::sampleCount(meter) == uint64_t(fs * 12 * 3600));
        for (int w = 0; w < 3; ++w) {
            const double quiet = std::pow(10., -12 + 2 * w);
            const long double expected = (long double)(fs / 10) * (loudBuckets + (long double)quietBuckets * quiet);
            // Check energy directly: dB rounding could hide quiet contributions.
            CHECK(std::abs(Access::energy(meter, w) - double(expected))
                <= 2 * std::numeric_limits<double>::epsilon() * double(expected));
            CHECK(r.session[w] == Approx(10 * std::log10((1 + 3 * quiet) / 4)).margin(1e-10));
            CHECK(r.minute[w] == Approx(10 * std::log10(quiet)).margin(1e-8));
            CHECK(r.fifteenMinutes[w] == Approx(10 * std::log10(quiet)).margin(1e-8));
        }
        meter.reset();
        CHECK(Access::sampleCount(meter) == 0);
        CHECK(Access::historyCount(meter) == 0);
        CHECK(meter.reading().session[0] == -120);
        Access::bucket(meter, {0.01, 0.01, 0.01});
        CHECK(meter.reading().session[0] == Approx(-20));
    }
}

TEST_CASE("Leq session includes partial buckets after a long accelerated session", "[leq][stress]") {
    foh::LeqMeter meter(8000);
    for (int i = 0; i < 3 * 36000; ++i) foh::LeqMeterSoakAccess::bucket(meter, {0.02, 0.02, 0.02});
    const float sample = 0.5f;
    meter.process(&sample, 1);
    auto r = meter.reading();
    CHECK(r.seconds == Approx(10800 + 1. / 8000).epsilon(1e-14));
    CHECK(r.session[2] == Approx(10 * std::log10((0.02 * 86400000 + 0.5) / 86400001)).margin(1e-12));
    CHECK(r.minute[2] == Approx(10 * std::log10((0.02 * 479999 + 0.5) / 480000)).margin(1e-10));
    CHECK(r.fifteenMinutes[2] == Approx(10 * std::log10((0.02 * 7199999 + 0.5) / 7200000)).margin(1e-9));
}
