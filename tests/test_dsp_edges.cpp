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
    REQUIRE_FALSE(parseMicCal("100 -1e300 0\n1000 -1e300 0\n").ok()); // #87: absurd values are rejected
    auto cal = parseMicCal("100 -60 0\n1000 -60 0\n");
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
        for (int i = 0; i < 128; ++i) {
            float p = pink.next(), s = sweep.next();
            REQUIRE(std::isfinite(p)); REQUIRE(std::isfinite(s));
            if (!std::isfinite(gain)) { REQUIRE(p == 0); REQUIRE(s == 0); }
        }
        pink.setGainDb(-12); sweep.setGainDb(-12);
        double pinkEnergy = 0, sweepEnergy = 0;
        for (int i = 0; i < 128; ++i) {
            pinkEnergy += std::pow(pink.next(), 2); sweepEnergy += std::pow(sweep.next(), 2);
        }
        REQUIRE(pinkEnergy > 0); REQUIRE(sweepEnergy > 0);
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

TEST_CASE("TF rejects silent DC clipped and nonfinite frames even with blanking disabled", "[edges][tf]") {
    constexpr int n = 1024;
    std::vector<float> good(4 * n);
    PinkNoise noise; noise.process(good.data(), (int) good.size());
    for (float value : {0.f, 0.5f, 1.f, -1.f, (float) qnan, (float) inf,
                        std::numeric_limits<float>::denorm_min()}) {
        TransferFunction tf(n); tf.setCoherenceBlankThreshold(0);
        tf.process(good.data(), good.data(), (int) good.size());
        REQUIRE(coherenceSummary(tf.result()).meanCoherence > 0.99);
        auto bad = good;
        if (std::isfinite(value) && std::abs(value) < 1) std::fill(bad.begin(), bad.end(), value);
        else for (int i = 0; i < (int) bad.size(); i += n / 4) { bad[i] = value; bad[i + 1] = value; bad[i + 2] = value; }
        tf.process(bad.data(), bad.data(), (int) bad.size());
        for (int smoothing : {0, 3, 6, 48}) {
            auto r = tf.result(smoothing); finite(r);
            REQUIRE(coherenceSummary(r).meanCoherence == 0);
            for (size_t k = 0; k < r.H.size(); ++k) { REQUIRE(r.isBlanked(k)); REQUIRE(r.H[k] == cpx{}); }
        }
        REQUIRE_FALSE(tf.convergence().settled);
        tf.process(good.data(), good.data(), (int) good.size());
        REQUIRE(coherenceSummary(tf.result()).meanCoherence > 0.99);
    }
}

TEST_CASE("RTA discards stale levels for DC silence and invalid frames and recovers", "[edges][rta]") {
    constexpr int n = 1024;
    std::vector<float> good(4 * n);
    for (size_t i = 0; i < good.size(); ++i) good[i] = 0.5f * std::sin(2 * M_PI * 1000 * i / 48000);
    for (float value : {0.f, 0.5f, 1.f, -1.f, (float) qnan, (float) inf,
                        std::numeric_limits<float>::denorm_min()}) {
        RTA rta(n); rta.process(good.data(), (int) good.size());
        auto initial = rta.bandsDb(); REQUIRE(*std::max_element(initial.begin(), initial.end()) > -10);
        std::vector<float> bad(4 * n, value); rta.process(bad.data(), (int) bad.size());
        auto levels = rta.bandsDb(); finite(levels);
        if (!std::isfinite(value) || std::abs(value) >= 1) REQUIRE(levels.empty());
        else { REQUIRE_FALSE(levels.empty()); for (auto db : levels) REQUIRE(db < -190); }
        rta.process(good.data(), (int) good.size());
        levels = rta.bandsDb(); REQUIRE_FALSE(levels.empty());
        REQUIRE(*std::max_element(levels.begin(), levels.end()) > -10);
    }
}

TEST_CASE("Input block sizes do not change fixed Welch hop TF or RTA results", "[edges][hop]") {
    constexpr int n = 1024;
    std::vector<float> signal(5 * n + 31); PinkNoise noise; noise.process(signal.data(), (int) signal.size());
    TransferFunction baseline(n); RTA baselineRta(n);
    baseline.process(signal.data(), signal.data(), (int) signal.size()); baselineRta.process(signal.data(), (int) signal.size());
    auto expected = baseline.result(); auto expectedRta = baselineRta.bandsDb();
    for (int block : {1, 17, n / 2 - 1, n / 2 + 1, n + 3}) {
        TransferFunction tf(n); RTA rta(n);
        for (int i = 0; i < (int) signal.size(); i += block) {
            int count = std::min(block, (int) signal.size() - i);
            tf.process(signal.data() + i, signal.data() + i, count); rta.process(signal.data() + i, count);
        }
        REQUIRE(tf.frames() == baseline.frames()); REQUIRE(tf.result().H == expected.H);
        REQUIRE(rta.bandsDb() == expectedRta);
    }
}

TEST_CASE("Target validity rejects missing empty mismatched and corrupt public traces", "[edges][target]") {
    for (auto text : {"", "# header only\n", "20 * * *\n20000 * * *\n", "100 0\n"}) REQUIRE_FALSE(parseTarget(text).ok());
    auto valid = parseTarget("20 0\n20000 -6\n"); REQUIRE(valid.ok());
    auto t = valid; t.db.clear(); REQUIRE_FALSE(t.ok());
    t = valid; t.hz[1] = t.hz[0]; REQUIRE_FALSE(t.ok());
    for (double bad : {qnan, inf, -inf}) {
        t = valid; t.db[0] = bad; REQUIRE_FALSE(t.ok());
        t = valid; t.hz[0] = bad; REQUIRE_FALSE(t.ok());
        t = valid; t.toleranceDb = bad; REQUIRE_FALSE(t.ok());
    }
}

TEST_CASE("Sustained full-scale sine clipping is rejected independently on either TF input", "[edges][tf][rta]") {
    constexpr int n = 1024;
    std::vector<float> good(4 * n), clipped(4 * n);
    for (size_t i = 0; i < good.size(); ++i) {
        double sine = std::sin(2 * M_PI * 1000 * i / 48000);
        good[i] = (float) (0.5 * sine); clipped[i] = (float) std::clamp(2 * sine, -1.0, 1.0);
    }
    RTA rta(n); rta.process(clipped.data(), (int) clipped.size()); REQUIRE(rta.bandsDb().empty());
    for (bool reference : {false, true}) {
        TransferFunction tf(n); tf.setCoherenceBlankThreshold(0);
        tf.process(reference ? clipped.data() : good.data(), reference ? good.data() : clipped.data(), (int) good.size());
        auto result = tf.result(); finite(result);
        REQUIRE(coherenceSummary(result).meanCoherence == 0);
        REQUIRE_FALSE(tf.convergence().settled);
        for (size_t k = 0; k < result.H.size(); ++k) REQUIRE(result.isBlanked(k));
    }
}

TEST_CASE("Generator signal paths reject damage and recover without resetting analysis", "[edges][generator][tf][rta]") {
    constexpr int n = 1024;
    for (int source : {0, 1, 2}) {
        INFO("source (pink, sine, sweep): " << source);
        std::vector<float> good(4 * n);
        PinkNoise pink(102); LogSweep sweep;
        sweep.prepare(48000, 200, 10000, 0.1, 0);
        for (size_t i = 0; i < good.size(); ++i)
            good[i] = source == 0 ? pink.next() : source == 1
                ? float(0.5 * std::sin(2 * M_PI * 1000 * i / 48000)) : sweep.next();
        for (int damage = 0; damage < 7; ++damage) {
            INFO("damage: " << damage);
            auto bad = good;
            for (size_t i = 0; i < bad.size(); ++i) {
                if (damage == 0) bad[i] = 0;
                if (damage == 1) bad[i] = i % 2 ? 1.f : -1.f;
                // Sparse injection crosses both frame and irregular block boundaries.
                if (damage >= 2 && damage <= 4 && i % 127 == 0)
                    bad[i] = damage == 2 ? float(qnan) : damage == 3 ? float(inf) : float(-inf);
                if (damage >= 5) bad[i] = (i % 2 ? 1.f : -1.f) *
                    (damage == 5 ? std::numeric_limits<float>::denorm_min()
                                 : std::nextafter(std::numeric_limits<float>::min(), 0.f));
            }
            for (bool damageReference : {false, true}) {
                TransferFunction tf(n); tf.setCoherenceBlankThreshold(0);
                RTA rta(n);
                auto feed = [&](const std::vector<float>& ref, const std::vector<float>& meas) {
                    for (int i = 0; i < int(ref.size()); i += 137) {
                        int count = std::min(137, int(ref.size()) - i);
                        tf.process(ref.data() + i, meas.data() + i, count);
                        rta.process(meas.data() + i, count);
                    }
                };
                feed(good, good);
                REQUIRE(coherenceSummary(tf.result()).meanCoherence > 0.99);
                feed(damageReference ? bad : good, damageReference ? good : bad);
                auto rejected = tf.result(3); finite(rejected);
                REQUIRE(coherenceSummary(rejected).meanCoherence == 0);
                for (size_t k = 0; k < rejected.H.size(); ++k) REQUIRE(rejected.isBlanked(k));
                auto levels = rta.bandsDb(); finite(levels);
                if (!damageReference && damage >= 1 && damage <= 4) REQUIRE(levels.empty());
                if (!damageReference && (damage == 0 || damage >= 5)) {
                    REQUIRE_FALSE(levels.empty());
                    for (double db : levels) REQUIRE(db < -190);
                }
                // The first returning frame overlaps the damaged half-frame;
                // allow normal Welch averaging to converge rather than resetting it.
                for (int frame = 0; frame < 8; ++frame) feed(good, good);
                auto recovered = tf.result(); finite(recovered);
                REQUIRE(coherenceSummary(recovered).meanCoherence > 0.99);
                levels = rta.bandsDb(); finite(levels); REQUIRE_FALSE(levels.empty());
                REQUIRE(*std::max_element(levels.begin(), levels.end()) > -50);
            }
        }
    }
}
