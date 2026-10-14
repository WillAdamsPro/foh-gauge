#include <catch2/generators/catch_generators.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <algorithm>
using namespace foh;

TEST_CASE("Loopback sweep deconvolution recovers 4.80 ms", "[sweep]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0);
    const bool inverted = GENERATE(false, true);
    LogSweep sweep; sweep.prepare(fs, 20, std::min(20000.0, fs * 0.45), 1, 0.2);
    std::vector<float> ref(size_t(fs * 1.2)), mic(ref.size());
    sweep.process(ref.data(), int(ref.size()));
    // Linear fractional delay. A common interface delay/response is present in
    // the recorded reference and therefore removed by deconvolution.
    auto source = ref;
    for (size_t i = 8; i < ref.size(); ++i) ref[i] = 0.7f*source[i-7] + 0.2f*source[i-8];
    const double delay = fs * 0.0048;
    const size_t whole = size_t(delay); const double fraction = delay - whole;
    for (size_t i = whole + 1; i < mic.size(); ++i)
        mic[i] = float((inverted ? -0.6 : 0.6) * ((1-fraction)*ref[i-whole] + fraction*ref[i-whole-1]));
    auto result = deconvolveSweep(ref, mic, fs);
    REQUIRE(result.response.ok());
    CHECK(result.response.arrivals.front().delayMs == Catch::Approx(4.8).margin(0.02));
    CHECK(result.response.arrivals.front().inverted == inverted);
    CHECK(polarityFromImpulse(result.response.impulse) == (inverted ? ImpulsePolarity::Inverted : ImpulsePolarity::Normal));
    REQUIRE(result.etcDb.size() == result.response.impulse.size());
    CHECK(*std::max_element(result.etcDb.begin(), result.etcDb.end()) == Catch::Approx(0).margin(1e-10));
    for (auto db : result.etcDb) { REQUIRE(std::isfinite(db)); REQUIRE(db <= 0); }
}

TEST_CASE("Sweep deconvolution rejects invalid or silent captures", "[sweep]") {
    std::vector<float> ref(1024, 0), mic(1024, 0);
    CHECK_FALSE(deconvolveSweep(ref, mic, 48000).response.ok());
    ref[0] = 1;
    CHECK_FALSE(deconvolveSweep(ref, mic, 48000).response.ok());
    CHECK_FALSE(deconvolveSweep(ref, mic, 0).response.ok());
    mic[0] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(deconvolveSweep(ref, mic, 48000).response.ok());
    CHECK_FALSE(deconvolveSweep(ref, {}, 48000).response.ok());
}

TEST_CASE("Log sweep has a safety ramp at both ends and restart", "[sweep]") {
    LogSweep sweep; sweep.prepare(48000, 20, 20000, 1, 0.1); sweep.setGainDb(0);
    std::vector<float> samples(52800); sweep.process(samples.data(), int(samples.size()));
    CHECK(samples.front() == 0);
    for (int i = 0; i < 480; ++i) {
        CHECK(std::abs(samples[i]) <= 0.25 * i / 480.0 + 1e-7);
        CHECK(std::abs(samples[47999-i]) <= 0.25 * (i+1) / 480.0 + 1e-7);
    }
    for (size_t i = 48000; i < samples.size(); ++i) CHECK(samples[i] == 0);
    sweep.reset(); CHECK(sweep.next() == 0);
}

TEST_CASE("Sweep ETC preserves synthetic reflection energy", "[sweep]") {
    LogSweep sweep; sweep.prepare(48000, 20, 20000, 1, 0.2);
    std::vector<float> ref(57600), mic(ref.size()); sweep.process(ref.data(), int(ref.size()));
    constexpr size_t direct = 230, reflection = direct + 384; // echo 8 ms later, -6.02 dB
    for (size_t i = direct; i < mic.size(); ++i) {
        mic[i] = ref[i-direct];
        if (i >= reflection) mic[i] += 0.5f * ref[i-reflection];
    }
    auto result = deconvolveSweep(ref, mic, 48000);
    REQUIRE(result.response.ok());
    CHECK(result.etcDb[direct] == Catch::Approx(0).margin(0.1));
    CHECK(result.etcDb[reflection] == Catch::Approx(-6.0206).margin(0.2));
}

TEST_CASE("Near-field impulse direction distinguishes normal and inverted boxes", "[polarity]") {
    const double sign = GENERATE(1.0, -1.0);
    std::vector<double> impulse(512, 0);
    impulse[48] = 0.8 * sign; // direct arrival
    impulse[49] = -0.3 * sign; // opposite ringing lobe
    impulse[240] = -0.2 * sign; // later reflection
    CHECK(polarityFromImpulse(impulse) == (sign > 0 ? ImpulsePolarity::Normal : ImpulsePolarity::Inverted));
}

TEST_CASE("Impulse polarity is inconclusive for unusable measurements", "[polarity]") {
    CHECK(polarityFromImpulse({}) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({0, 0, 0}) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({1e-13, -1e-13}) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({1, std::numeric_limits<double>::quiet_NaN()}) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({std::numeric_limits<double>::infinity(), -1}) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({-0.5, 0.5}) == ImpulsePolarity::Inverted);
}

TEST_CASE("Sweep edge captures cannot report an arrival or polarity", "[sweep][edges]") {
    for (size_t length : {0u, 1u, 63u, 64u, 65u, 1024u}) {
        for (float value : {0.f, 0.25f, 1.f, -1.f, std::numeric_limits<float>::denorm_min(),
                           std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            std::vector<float> samples(length, value);
            auto result = deconvolveSweep(samples, samples, 48000);
            CHECK_FALSE(result.response.ok());
            CHECK_FALSE(result.response.error.empty());
            CHECK(result.response.arrivals.empty());
            CHECK(result.etcDb.empty());
        }
    }
    std::vector<float> ref(1024), mic(1024); ref[10] = 0.5f; mic[20] = 0.25f;
    for (double rate : {0.0, double(NAN), double(INFINITY)}) CHECK_FALSE(deconvolveSweep(ref, mic, rate).response.ok());
    CHECK_FALSE(deconvolveSweep(ref, {}, 48000).response.ok());
    for (float bad : {1.f, -1.f, NAN, INFINITY, -INFINITY}) {
        auto damaged = mic; damaged[30] = bad;
        CHECK_FALSE(deconvolveSweep(ref, damaged, 48000).response.ok());
        CHECK_FALSE(deconvolveSweep(damaged, mic, 48000).response.ok());
    }
}

TEST_CASE("Sweep removes DC without changing synthetic arrival or direction", "[sweep][edges]") {
    std::vector<float> ref(2048), mic(2048); ref[20] = 0.25f; mic[250] = -0.125f;
    const auto clean = deconvolveSweep(ref, mic, 48000);
    for (auto& x : ref) x += 0.2f;
    for (auto& x : mic) x -= 0.1f;
    const auto offset = deconvolveSweep(ref, mic, 48000);
    REQUIRE(clean.response.ok()); REQUIRE(offset.response.ok());
    CHECK(offset.response.arrivals[0].delayMs == Catch::Approx(clean.response.arrivals[0].delayMs).margin(0.001));
    CHECK(offset.response.arrivals[0].inverted);
    for (auto x : offset.etcDb) CHECK(std::isfinite(x));
}

TEST_CASE("Sweep rejects arrivals outside the causal impulse window", "[sweep][edges]") {
    // A late loopback puts the real arrival at negative lag. Searching only
    // positive lags would incorrectly publish a regularization sidelobe.
    std::vector<float> ref(4096), mic(4096);
    ref[300] = 0.5f; mic[100] = 0.25f;
    auto result = deconvolveSweep(ref, mic, 48000);
    CHECK_FALSE(result.response.ok());
    CHECK_FALSE(result.response.error.empty());
    CHECK(result.response.impulse.empty());
    CHECK(result.response.arrivals.empty());
    CHECK(result.etcDb.empty());

    // Positive lag beyond the displayed half-second window is also unusable.
    ref.assign(32768, 0); mic.assign(32768, 0);
    ref[100] = 0.5f; mic[25000] = 0.25f;
    result = deconvolveSweep(ref, mic, 48000);
    CHECK_FALSE(result.response.ok());
    CHECK(result.response.impulse.empty());
    CHECK(result.response.arrivals.empty());
    CHECK(result.etcDb.empty());
}

TEST_CASE("Sweep accepts zero lag and subnormal samples in an otherwise usable capture", "[sweep][edges]") {
    std::vector<float> ref(4096, std::numeric_limits<float>::denorm_min()), mic(ref);
    ref[100] = 0.5f; mic[100] = 0.25f;
    const auto result = deconvolveSweep(ref, mic, 48000);
    REQUIRE(result.response.ok());
    CHECK(result.response.arrivals[0].delaySamples == Catch::Approx(0));
    for (auto sample : result.response.impulse) CHECK(std::isfinite(sample));
    for (auto db : result.etcDb) CHECK(std::isfinite(db));
}

TEST_CASE("Impulse view rejects incomplete or unusable stored spectra", "[sweep][edges]") {
    TFResult tf; tf.fftSize = 64; tf.H.assign(33, 0.5);
    REQUIRE(impulseFromTF(tf).size() == 64);
    for (double rate : {0.0, double(NAN), double(INFINITY)}) {
        auto bad = tf; bad.sampleRate = rate; CHECK(impulseFromTF(bad).empty());
    }
    for (size_t size : {0u, 1u, 32u}) {
        auto bad = tf; bad.H.resize(size); CHECK(impulseFromTF(bad).empty());
    }
    for (double value : {0.0, std::numeric_limits<double>::denorm_min(), double(NAN), double(INFINITY), 1e300}) {
        auto bad = tf; bad.H.assign(33, value); CHECK(impulseFromTF(bad).empty());
    }
    auto bad = tf; bad.blanked.assign(33, 1); CHECK(impulseFromTF(bad).empty());
    bad = tf; bad.H.assign(33, 0); bad.H[0] = 0.5; CHECK(impulseFromTF(bad).empty());
}
