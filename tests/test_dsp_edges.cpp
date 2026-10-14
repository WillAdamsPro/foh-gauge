#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace foh;
using Catch::Approx;
namespace {
constexpr double qnan = std::numeric_limits<double>::quiet_NaN();
constexpr double inf = std::numeric_limits<double>::infinity();
void finite(const std::vector<double>& v) { for (double x : v) REQUIRE(std::isfinite(x)); }
void finite(const TFResult& r) {
    finite(r.freq); finite(r.magDb); finite(r.phaseDeg); finite(r.coherence);
    for (auto h : r.H) { REQUIRE(std::isfinite(h.real())); REQUIRE(std::isfinite(h.imag())); }
}
}
TEST_CASE("TF and RTA survive silence clipping and invalid samples without poisoning later frames", "[edges]") {
    for (int n : {2, 64, 16384}) {
        TransferFunction tf(n); RTA rta(n);
        tf.process(nullptr, nullptr, 0); rta.process(nullptr, 0);
        std::vector<float> samples(2 * n, 0);
        for (float value : {0.f, 1.f, -1.f, std::numeric_limits<float>::denorm_min(),
                            (float) qnan, (float) inf, (float) -inf, std::numeric_limits<float>::max()}) {
            std::fill(samples.begin(), samples.end(), value);
            // Single-sample blocks exercise overlap and very short buffers.
            for (auto& x : samples) { tf.process(&x, &x, 1); rta.process(&x, 1); }
            finite(tf.result(3)); finite(rta.bandsDb());
        }
        tf.reset(); rta.reset();
        PinkNoise noise; noise.process(samples.data(), (int) samples.size());
        tf.process(samples.data(), samples.data(), (int) samples.size());
        rta.process(samples.data(), (int) samples.size());
        finite(tf.result()); finite(rta.bandsDb());
        REQUIRE(tf.result().coherence.back() == Approx(1));
    }
}
TEST_CASE("FFT validates sizes and storage before touching memory", "[edges]") {
    REQUIRE_THROWS_AS(FFT(0), std::invalid_argument);
    REQUIRE_THROWS_AS(FFT(3), std::invalid_argument);
    REQUIRE_THROWS_AS(FFT(1 << 25), std::invalid_argument);
    FFT tiny(1); std::vector<cpx> one{2.0}; tiny.forward(one); tiny.inverse(one); REQUIRE(one[0].real() == 2);
    std::vector<cpx> empty; REQUIRE_THROWS_AS(tiny.forward(empty), std::invalid_argument);
    REQUIRE_THROWS_AS(tiny.inverse(empty), std::invalid_argument);
    REQUIRE(hannWindow(-1).empty());
    REQUIRE_THROWS_AS(TransferFunction(1), std::invalid_argument);
    REQUIRE_THROWS_AS(RTA(1), std::invalid_argument);
    REQUIRE_THROWS_AS(TransferFunction(64, qnan), std::invalid_argument);
    REQUIRE_THROWS_AS(RTA(64, 0), std::invalid_argument);
    // Largest supported radix-2 transform (also the delay finder's ceiling).
    FFT large(1 << 24); std::vector<cpx> data(1 << 24); data[0] = 1;
    large.forward(data); REQUIRE(data.back().real() == Approx(1));
    large.inverse(data); REQUIRE(data[0].real() == Approx(1));
    REQUIRE(std::abs(data.back()) < 1e-12);
}
TEST_CASE("Smoothing and coherence reject invalid values locally", "[edges]") {
    for (double bin : {0.0, qnan, inf, 48000.0 / 64}) {
        finite(smoothPower({1, qnan, 2, inf, 3}, bin, 3));
        for (auto h : smoothComplex({1, {qnan, inf}, 2}, bin, 3)) {
            REQUIRE(std::isfinite(h.real())); REQUIRE(std::isfinite(h.imag()));
        }
    }
    REQUIRE(classifyCoherence(inf) == TrafficLight::Red);
    TFResult r; r.freq = {0, 100, 200}; r.coherence = {0, qnan, inf};
    REQUIRE(coherenceSummary(r).meanCoherence == 0);
    SettleDetector detector;
    for (int i = 0; i < 4; ++i) detector.update(-inf);
    REQUIRE_FALSE(detector.settled());
    detector.update(-1); REQUIRE(detector.streakCount() == 0);
    ConvergenceSettings s; s.consecutive = 0; SettleDetector invalid(s);
    REQUIRE_FALSE(invalid.update(qnan));
    REQUIRE(std::isnan(settleDeltaDb({0}, {0}, {inf}, {1000})));
    REQUIRE(std::isnan(settleDeltaDb({0}, {1e300}, {1}, {1000})));
}
TEST_CASE("RTA calibration and averaging cannot inject nonfinite band levels", "[edges]") {
    RTA rta(64); std::vector<float> x(128, 1);
    rta.setCalibrationDb(std::vector<double>(33, inf)); rta.avgAlpha = qnan;
    rta.process(x.data(), 128); rta.process(x.data(), 128); finite(rta.bandsDb());
    rta.setCalibrationDb(std::vector<double>(33, 1e300)); finite(rta.bandsDb());
    REQUIRE(octaveBands(0).empty()); REQUIRE(octaveBands(-3).empty());
}
TEST_CASE("Mic calibration handles invalid queries and extreme corrections", "[edges]") {
    auto cal = parseMicCal("100 -1e300 0\n1000 -1e300 0\n");
    REQUIRE(cal.ok()); REQUIRE(std::isfinite(cal.dbAt(qnan)));
    REQUIRE(cal.correctionDb(-1, 1).empty());
    TransferFunction tf(64); std::vector<float> x(128, 1); tf.process(x.data(), x.data(), 128);
    auto r = tf.result(); cal.apply(r); finite(r);
    r.freq.clear(); cal.apply(r); // Malformed public result must not index past its arrays.
    MicCal malformed; malformed.hz = {100, 1000}; REQUIRE(malformed.dbAt(500) == 0);
    auto sens = parseMicCal("Sens Factor =nan\n100 0\n1000 0\n");
    REQUIRE_FALSE(sens.hasSensFactor); REQUIRE(std::isfinite(sens.sensitivityOffsetDb));
}
TEST_CASE("Generators reject invalid preparation and bound extreme gain requests", "[edges]") {
    PinkNoise pink; LogSweep sweep;
    sweep.prepare(48000, 20, 20000, 1);
    for (float gain : {(float) qnan, (float) inf, (float) -inf, 1e30f, -1e30f}) {
        pink.setGainDb(gain); sweep.setGainDb(gain);
        for (int i = 0; i < 128; ++i) { REQUIRE(std::isfinite(pink.next())); REQUIRE(std::isfinite(sweep.next())); }
    }
    for (double rate : {0.0, qnan, inf, 48000.0}) {
        sweep.prepare(rate, 0, 20000, 1); REQUIRE(sweep.lengthSamples() == 0);
        for (int i = 0; i < 128; ++i) REQUIRE(sweep.next() == 0);
    }
    sweep.prepare(48000, 100, 100, 1); REQUIRE(sweep.lengthSamples() == 0);
    sweep.prepare(48000, 20, 20000, inf); REQUIRE(sweep.lengthSamples() == 0);
    sweep.prepare(48000, 20, 20000, 0.0001, 0);
    for (int i = 0; i < 128; ++i) REQUIRE(std::isfinite(sweep.next()));
}
TEST_CASE("Delay oversized searches stay within captured history", "[edges]") {
    for (int length : {0, 1, 63, 64, 65, 128}) {
        std::vector<float> x(length), y(length);
        if (length) { x[0] = 1; y.back() = 1; }
        DelayOptions o; o.maxDelayMs = 1e300; o.minDelayMs = -inf;
        auto r = findDelay(x, y, 48000, o);
        finite(r.impulse); REQUIRE(std::isfinite(r.confidence));
        if (length < 64) REQUIRE_FALSE(r.ok());
        else { REQUIRE(r.ok()); for (auto a : r.arrivals) {
            REQUIRE(std::abs(a.delaySamples) <= length - 1);
            REQUIRE(std::isfinite(a.delayMs)); REQUIRE(std::isfinite(a.relLevelDb));
        } }
    }
    REQUIRE(recommendedDelayWindowSamples(qnan, true) == 0);
    REQUIRE(recommendedDelayWindowSamples(inf, false) == 0);
    REQUIRE(recommendedDelayWindowSamples(48000, false, 1e300) == (1 << 23));
    TFResult invalid; invalid.fftSize = 1; invalid.H = {{qnan, inf}};
    finite(impulseFromTF(invalid));
}
TEST_CASE("Targets explicitly reject invalid numeric input", "[edges]") {
    for (const auto& row : {"100 nan", "100 inf", "100 1e999", "100 0 nan 1"}) {
        auto target = parseTarget(std::string("20 0\n20000 0\n") + row + "\n");
        REQUIRE_FALSE(target.ok()); finite(target.hz); finite(target.db);
    }
}

TEST_CASE("Reset discards reference delay history and invalid convergence settings are safe", "[edges]") {
    TransferFunction tf(64); tf.setReferenceDelaySamples(128);
    std::vector<float> signal(256, 1), silence(64, 0);
    tf.process(signal.data(), signal.data(), 256); tf.reset();
    tf.process(silence.data(), signal.data(), 64);
    auto r = tf.result(); finite(r);
    for (auto h : r.H) REQUIRE(std::abs(h) == 0);
    tf.setCoherenceBlankThreshold(qnan); REQUIRE(tf.coherenceBlankThreshold() == 0);
    ConvergenceSettings settings; settings.fLo = -1e300; settings.fHi = 1e300;
    tf.setConvergence(settings); tf.process(signal.data(), signal.data(), 256); finite(tf.result());
    settings.fLo = qnan; settings.fHi = inf; tf.setConvergence(settings);
    tf.process(signal.data(), signal.data(), 256); REQUIRE_FALSE(tf.convergence().settled);
    REQUIRE(std::isfinite(tf.convergence().progress));
}
TEST_CASE("Different source sample clocks remain finite but cannot give a coherent transfer", "[edges]") {
    // Raw paired APIs carry one sample rate, so callers must resample upstream.
    // A 2 kHz tone captured at 44.1k is a different bin from one captured at 48k.
    constexpr int n = 1024; TransferFunction tf(n, 48000);
    std::vector<float> ref(n), meas(n);
    for (int frame = 0; frame < 100; ++frame) {
        for (int i = 0; i < n; ++i) {
            ref[i] = (float) std::sin(2 * 3.141592653589793 * 2000 * (frame * n + i) / 44100);
            meas[i] = (float) std::sin(2 * 3.141592653589793 * 2000 * (frame * n + i) / 48000);
        }
        tf.process(ref.data(), meas.data(), n);
    }
    const auto r = tf.result(); finite(r);
    REQUIRE(coherenceSummary(r, 1900, 2300).meanCoherence < 0.6);
    std::vector<float> tiny(128, std::numeric_limits<float>::denorm_min());
    REQUIRE_FALSE(findDelay(tiny, tiny, 48000).ok());
}
