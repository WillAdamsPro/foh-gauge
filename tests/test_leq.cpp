#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <memory>
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

TEST_CASE("Leq invalid signal cannot be presented as a safe exposure", "[leq][edges]") {
    foh::LeqMeter meter(48000);
    CHECK_FALSE(meter.reading().valid);
    meter.process(nullptr, 0); CHECK(meter.reading().seconds == 0);
    float shortTone[] = {0.1f, -0.1f}; meter.process(shortTone, 2);
    CHECK_FALSE(meter.reading().valid);
    for (float value : {0.f, 0.25f, 1.f, -1.f, std::numeric_limits<float>::denorm_min(), NAN, INFINITY, -INFINITY}) {
        meter.reset(); std::vector<float> block(4800, value); meter.process(block.data(), int(block.size()));
        auto reading = meter.reading(); CHECK_FALSE(reading.valid);
        for (auto db : reading.session) CHECK(std::isfinite(db));
        for (auto db : reading.level) CHECK(std::isfinite(db));
        for (auto db : reading.minute) CHECK(std::isfinite(db));
    }
    meter.reset();
    std::vector<float> dc(4800, 0.25f); meter.process(dc.data(), int(dc.size()));
    tone(meter, 48000, 0.2); CHECK_FALSE(meter.reading().valid);
    meter.reset(); tone(meter, 48000, 0.2); REQUIRE(meter.reading().valid);
    float bad = NAN; meter.process(&bad, 1); tone(meter, 48000, 0.2);
    CHECK_FALSE(meter.reading().valid); // exposure history was damaged
    meter.reset(); tone(meter, 48000, 0.2); CHECK(meter.reading().valid);
    meter.process(nullptr, 1); CHECK_FALSE(meter.reading().valid);
    CHECK(std::isfinite(foh::exposureWarning(foh::ExposurePreset::NIOSH, 100, 1e300, 60).dosePercent));
}

TEST_CASE("Leq retains energy and sample counts over a 25 hour session", "[leq][long]") {
    // More than 2^32 samples at a normal device rate.
    constexpr int fs = 48000, halfSeconds = 25 * 3600 / 2;
    foh::LeqMeter meter(fs);
    std::vector<float> block(fs);
    double expectedEnergy = 0;
    for (double amplitude : {0.1, 0.0001}) {
        double energy = 0;
        for (int i = 0; i < fs; ++i) {
            block[i] = float(amplitude * std::sin(2 * 3.141592653589793 * 1000 * i / fs));
            energy += 2 * double(block[i]) * block[i];
        }
        expectedEnergy += energy * halfSeconds;
        for (int second = 0; second < halfSeconds; ++second) meter.process(block.data(), fs);
    }
    auto r = meter.reading();
    REQUIRE(r.valid);
    CHECK(r.seconds == 90000);
    CHECK(r.session[2] == Approx(10 * std::log10(expectedEnergy / (uint64_t(fs) * 90000))).margin(1e-9));
    CHECK(r.minute[2] == Approx(-80).margin(1e-6));
    CHECK(r.fifteenMinutes[2] == Approx(-80).margin(1e-6));
    for (int w : {0, 1, 2}) {
        CHECK(std::isfinite(r.session[w])); CHECK(std::isfinite(r.level[w]));
        CHECK(std::isfinite(r.minute[w])); CHECK(std::isfinite(r.fifteenMinutes[w]));
    }
    foh::SplCal cal; REQUIRE(cal.calibrate(-20, 94));
    CHECK(foh::exposureWarning(foh::ExposurePreset::NIOSH, cal.level(r.fifteenMinutes[0]),
        cal.level(r.session[0]), r.seconds).exceeded);
    CHECK_FALSE(foh::exposureWarning(foh::ExposurePreset::WHO, cal.level(r.fifteenMinutes[0]),
        cal.level(r.session[0]), r.seconds).exceeded);
    meter.reset();
    CHECK(meter.reading().seconds == 0);
    tone(meter, fs, 1);
    CHECK(meter.reading().session[2] == Approx(-20).margin(1e-6));
}

TEST_CASE("Leq silence and isolated corrupt samples have finite guarded readings", "[leq][edges]") {
    for (float bad : {1.f, -1.f, 2.f, -2.f, NAN, INFINITY, -INFINITY}) {
        auto storage = std::make_unique<foh::LeqMeter>(8000);
        auto& meter = *storage; tone(meter, 8000, 0.2);
        REQUIRE(meter.reading().valid);
        meter.process(&bad, 1); tone(meter, 8000, 0.2);
        CHECK_FALSE(meter.reading().valid);
        const auto r = meter.reading();
        for (const auto& values : {r.level, r.minute, r.fifteenMinutes, r.session})
            for (double value : values) CHECK(std::isfinite(value));
        meter.reset(); tone(meter, 8000, 0.2);
        CHECK(meter.reading().valid);
    }
    auto storage = std::make_unique<foh::LeqMeter>(8000);
    auto& meter = *storage; tone(meter, 8000, 0.2);
    std::vector<float> silence(8000, 0); meter.process(silence.data(), int(silence.size()));
    auto r = meter.reading(); CHECK_FALSE(r.valid);
    CHECK(r.level[2] == -120); CHECK(std::isfinite(r.session[2]));
    foh::SplCal uncalibrated;
    CHECK_FALSE(uncalibrated.calibrated);
    CHECK(uncalibrated.level(r.session[2]) == r.session[2]);
    // Subnormals mixed into otherwise usable AC input behave exactly like zeros.
    auto cleanStorage = std::make_unique<foh::LeqMeter>(8000);
    auto denormalStorage = std::make_unique<foh::LeqMeter>(8000);
    auto& clean = *cleanStorage;
    auto& denormal = *denormalStorage;
    std::vector<float> signal(8000), dirty(8000);
    for (int i = 0; i < 8000; ++i) {
        signal[i] = i % 2 ? (i % 4 == 1 ? 0.1f : -0.1f) : 0.f;
        dirty[i] = i % 2 ? signal[i] : std::numeric_limits<float>::denorm_min();
    }
    clean.process(signal.data(), 8000); denormal.process(dirty.data(), 8000);
    REQUIRE(clean.reading().valid); CHECK(denormal.reading().valid);
    CHECK(clean.reading().session == denormal.reading().session);
    CHECK(clean.reading().level == denormal.reading().level);
}

TEST_CASE("Sustained denormals match silence through Leq filters and rolling windows", "[leq][edges][reaudit]") {
    for (float tiny : {std::numeric_limits<float>::denorm_min(),
                       std::nextafter(std::numeric_limits<float>::min(), 0.f)}) {
        auto clean = std::make_unique<foh::LeqMeter>(8000);
        auto dirty = std::make_unique<foh::LeqMeter>(8000);
        tone(*clean, 8000, 0.2); tone(*dirty, 8000, 0.2);
        std::vector<float> silence(997), subnormals(997);
        for (int i = 0; i < 997; ++i) subnormals[i] = i % 2 ? tiny : -tiny;
        // Expire the minute window and exercise filter tails and partial buckets.
        for (int repeat = 0; repeat < 490; ++repeat) {
            clean->process(silence.data(), int(silence.size()));
            dirty->process(subnormals.data(), int(subnormals.size()));
        }
        const auto a = clean->reading(), b = dirty->reading();
        CHECK_FALSE(a.valid); CHECK_FALSE(b.valid);
        CHECK(a.seconds == b.seconds); CHECK(a.level == b.level); CHECK(a.minute == b.minute);
        CHECK(a.fifteenMinutes == b.fifteenMinutes); CHECK(a.session == b.session);
        for (const auto& values : {b.level, b.minute, b.fifteenMinutes, b.session})
            for (double value : values) CHECK(std::isfinite(value));
        for (double value : b.minute) CHECK(value == -120);
        tone(*clean, 8000, 0.2); tone(*dirty, 8000, 0.2);
        CHECK(clean->reading().valid); CHECK(dirty->reading().valid);
        CHECK(clean->reading().level == dirty->reading().level);
    }
}
