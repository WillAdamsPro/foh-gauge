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
    CHECK(polarityFromImpulse({-0.5, 0.5}) == ImpulsePolarity::Unknown);
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

TEST_CASE("Delay rejects silence, DC and short or missing reference windows", "[delay][edges]") {
    for (bool phat : {false, true}) {
        for (size_t length : {0u, 1u, 2u, 63u, 64u, 65u, 2048u}) {
            for (float dc : {0.f, 0.25f, -0.25f}) {
                std::vector<float> ref(length, dc), mic(length, dc);
                const auto result = findDelay(ref, mic, 48000, phat);
                CHECK_FALSE(result.ok()); CHECK_FALSE(result.error.empty());
                CHECK(result.arrivals.empty()); CHECK(result.confidence == 0);
            }
        }
        std::vector<float> mic(2048); mic[200] = -0.5f;
        CHECK_FALSE(findDelay({}, mic, 48000, phat).ok());
        CHECK_FALSE(findDelay(std::vector<float>(mic.size()), mic, 48000, phat).ok());
    }
}

TEST_CASE("DC offset preserves synthetic delay and inverted near-field direction", "[delay][polarity][edges]") {
    for (bool phat : {false, true}) {
        std::vector<float> ref(2048), mic(2048); ref[100] = 0.5f; mic[244] = -0.25f;
        for (auto& x : ref) x += 0.2f;
        for (auto& x : mic) x += 0.4f;
        const auto result = findDelay(ref, mic, 48000, phat);
        REQUIRE(result.ok()); CHECK(result.arrivals[0].delaySamples == Catch::Approx(144).margin(0.01));
        CHECK(result.arrivals[0].inverted); CHECK(std::isfinite(result.confidence));
    }
    for (double sign : {-1.0, 1.0}) {
        std::vector<double> impulse(512, 2.0);
        impulse[48] += sign * 0.8; impulse[49] -= sign * 0.3;
        CHECK(polarityFromImpulse(impulse) == (sign < 0 ? ImpulsePolarity::Inverted : ImpulsePolarity::Normal));
    }
    CHECK(polarityFromImpulse(std::vector<double>(64, 0.25)) == ImpulsePolarity::Unknown);
    CHECK(polarityFromImpulse({1}) == ImpulsePolarity::Unknown);
    std::vector<double> ambiguous(512); ambiguous[48] = 0.8; ambiguous[240] = -0.7;
    CHECK(polarityFromImpulse(ambiguous) == ImpulsePolarity::Unknown);
}

TEST_CASE("Two-spike flag checks every secondary and rejects non-finite values", "[delay][edges]") {
    std::vector<Arrival> arrivals{{48, 1, 0, 0}, {60, 1.25, 0, -0.1}, {144, 3, 0, -2.9}};
    CHECK(twoSpikeFlag(arrivals));
    arrivals[2].relLevelDb = -3.1; CHECK_FALSE(twoSpikeFlag(arrivals));
    CHECK_FALSE(twoSpikeFlag(-6, 0, 2)); // absolute level difference
    for (double bad : {double(NAN), double(INFINITY), double(-INFINITY)}) {
        CHECK_FALSE(twoSpikeFlag(bad, 0, 2)); CHECK_FALSE(twoSpikeFlag(0, bad, 2));
        CHECK_FALSE(twoSpikeFlag(0, 0, bad));
    }
    // Three physical peaks: a close lobe cannot mask the later comparable arrival.
    std::vector<float> ref(2048), mic(2048); ref[100] = 0.5f;
    mic[148] = 0.5f; mic[160] = 0.49f; mic[244] = -0.4f;
    DelayOptions options; options.phat = false; options.minSeparationMs = 0.1;
    const auto result = findDelay(ref, mic, 48000, options);
    REQUIRE(result.ok()); REQUIRE(result.arrivals.size() == 3);
    CHECK(result.twoSimilarArrivals); CHECK(result.confidence <= 0.75);
}

TEST_CASE("Delay cannot claim confidence when the window has no noise baseline", "[delay][edges]") {
    std::vector<float> ref(64), mic(64); ref[10] = 0.5f; mic[20] = 0.25f;
    DelayOptions options; options.minSeparationMs = 100;
    const auto result = findDelay(ref, mic, 48000, options);
    REQUIRE(result.ok()); CHECK(result.confidence == 0);
}

TEST_CASE("One corrupt complex TF bin invalidates the impulse including blanked bins", "[sweep][edges]") {
    TFResult tf; tf.fftSize = 64; tf.H.assign(33, 0.5);
    for (size_t bin : {0u, 1u, 32u}) {
        for (double bad : {double(NAN), double(INFINITY), double(-INFINITY)}) {
            for (bool imaginary : {false, true}) {
                auto damaged = tf; damaged.H[bin] = imaginary ? cpx(0.5, bad) : cpx(bad, 0);
                CHECK(impulseFromTF(damaged).empty());
                damaged.blanked.assign(33, 0); damaged.blanked[bin] = 1;
                CHECK(impulseFromTF(damaged).empty());
            }
        }
    }
}
