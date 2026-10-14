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
