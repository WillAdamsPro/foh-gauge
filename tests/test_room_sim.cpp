// Simulated-room harness: synthetic rooms (tests/roomsim/room_sim.h) fed through core findDelay,
// TransferFunction and coherence; asserts against known truth at Part A tolerances
// (delay ±0.05 ms or ±1 smp, sub-sample ±0.1 smp; TF mag ±0.5 dB; phase ±5° where coh > 0.9).
// Each scenario prints one "ROOMSIM | ..." row (scenario, fs, truth, result).
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "roomsim/room_sim.h"
#include <cstdio>
using namespace roomsim; using Catch::Approx;

namespace {
double tolSmp(double fs) { return std::max(0.05e-3 * fs, 1.0); }
void row(const char* name, double fs, double truth, const foh::DelayResult& d, const char* extra = "") {
    std::printf("ROOMSIM | %-22s | %6.0f | truth %9.3f smp | #1 %9.3f smp", name, fs, truth, d.arrivals.empty() ? NAN : d.arrivals[0].delaySamples);
    for (size_t i = 1; i < d.arrivals.size(); ++i) std::printf(" | #%zu %8.3f smp %6.2f dB", i + 1, d.arrivals[i].delaySamples, d.arrivals[i].relLevelDb);
    std::printf(" | conf %.2f flag %d %s\n", d.confidence, (int) d.twoSimilarArrivals, extra); }
const foh::Arrival* near(const foh::DelayResult& d, double smp, double tol) { for (auto& a : d.arrivals) if (std::fabs(a.delaySamples - smp) <= tol) return &a; return nullptr; }
}

TEST_CASE("RoomSim clean + fractional delay at 44.1/48/96k: delay, TF flat, coherence 1", "[RoomSim][S4][S5][D-INS][S1][S12]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0); INFO("fs " << fs);
    Room r; r.fs = fs; r.directDelaySmp = 4.80e-3 * fs; r.directGainDb = -6; r.seed = 101;
    auto c = simulate(r, 1.0); auto d = foh::findDelay(c.ref, c.mic, fs); row("clean 4.80 ms", fs, r.directDelaySmp, d);
    REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delaySamples == Approx(r.directDelaySmp).margin(0.1)); REQUIRE(!d.twoSimilarArrivals); REQUIRE(d.confidence > 0.8);
    auto tf = transfer(c, d.arrivals[0].delaySamples);
    for (double f : {100.0, 1000.0, 10000.0}) { INFO(f << " Hz"); auto k = bin(tf, f);
        REQUIRE(tf.magDb[k] == Approx(-6.0).margin(0.5)); REQUIRE(tf.coherence[k] > 0.99); REQUIRE(std::fabs(tf.phaseDeg[k]) < 5.0); }
    const double i0 = GENERATE(230.0, 230.4); Room ri = r; ri.directDelaySmp = i0 * fs / 48000.0; // S5 integer / S4 fractional, scaled
    auto ci = simulate(ri, 1.0); auto di = foh::findDelay(ci.ref, ci.mic, fs); REQUIRE(di.ok());
    REQUIRE(di.arrivals[0].delaySamples == Approx(ri.directDelaySmp).margin(0.1));
}

TEST_CASE("RoomSim single reflection -6 dB at +3 ms: direct first, reflection found at its level", "[RoomSim][D-T3][S16]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0); INFO("fs " << fs);
    Room r; r.fs = fs; r.directDelaySmp = 2.0e-3 * fs; r.reflections = {{5.0e-3 * fs, -6.0}}; r.seed = 202;
    auto c = simulate(r, 1.0); auto d = foh::findDelay(c.ref, c.mic, fs); row("reflection -6 dB +3 ms", fs, r.directDelaySmp, d);
    REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delaySamples == Approx(r.directDelaySmp).margin(0.1));
    auto a = near(d, 5.0e-3 * fs, tolSmp(fs)); REQUIRE(a); REQUIRE(a->relLevelDb == Approx(-6.0).margin(1.0)); REQUIRE(!d.twoSimilarArrivals);
    REQUIRE(a->distanceM - d.arrivals[0].distanceM == Approx(0.003 * 343).margin(0.02));
}

TEST_CASE("RoomSim S16 reflection -3 dB at +8 ms at 44.1/48/96k: level and flag boundaries", "[RoomSim][S16][S16a][S16b]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0); INFO("fs " << fs);
    for (double lvl : {-3.0, -2.5, -3.5}) { INFO("level " << lvl);
        Room r; r.fs = fs; r.directDelaySmp = 2.0e-3 * fs; r.reflections = {{10.0e-3 * fs, lvl}}; r.seed = 203;
        auto c = simulate(r, 1.0); auto d = foh::findDelay(c.ref, c.mic, fs); if (lvl == -3.0) row("S16 refl -3 dB +8 ms", fs, r.directDelaySmp, d);
        REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delayMs == Approx(2.0).margin(0.05));
        auto a = near(d, 10.0e-3 * fs, tolSmp(fs)); REQUIRE(a); REQUIRE(a->relLevelDb == Approx(lvl).margin(0.25));
        REQUIRE(d.twoSimilarArrivals == (lvl > -3.0)); }
}

TEST_CASE("RoomSim two-spike 3.00 + 4.50 ms at -0.5 dB: flag, both arrivals, lower confidence", "[RoomSim][D-2Sa][D-CF]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0); INFO("fs " << fs);
    Room r; r.fs = fs; r.directDelaySmp = 3.0e-3 * fs; r.reflections = {{4.5e-3 * fs, -0.5}}; r.seed = 303;
    auto c = simulate(r, 1.0); auto d = foh::findDelay(c.ref, c.mic, fs); row("two-spike", fs, r.directDelaySmp, d);
    Room cl = r; cl.reflections.clear(); auto cc = simulate(cl, 1.0); auto dc = foh::findDelay(cc.ref, cc.mic, fs);
    REQUIRE(d.ok()); REQUIRE(d.twoSimilarArrivals); REQUIRE(d.arrivals[0].delayMs == Approx(3.0).margin(0.05));
    auto a = near(d, 4.5e-3 * fs, tolSmp(fs)); REQUIRE(a); REQUIRE(a->relLevelDb == Approx(-0.5).margin(0.5));
    REQUIRE(d.confidence < dc.confidence);
}

TEST_CASE("RoomSim reverberant RT60 0.5 / 1.5 s: direct still found, coherence degrades with RT60", "[RoomSim][S4][D-CF]") {
    const double fs = 48000; double cohPrev = 1.1;
    for (double rt : {0.5, 1.5}) { INFO("RT60 " << rt);
        Room r; r.fs = fs; r.directDelaySmp = 10.0e-3 * fs + 0.37; r.rt60Low = rt * 1.3; r.rt60High = rt; r.tailLevelDb = -3; r.seed = 404;
        r.reflections = {{13.1e-3 * fs, -8.0}, {17.9e-3 * fs, -10.0}};
        auto c = simulate(r, 2.0); auto d = foh::findDelay(c.ref, c.mic, fs);
        char ex[64]; auto tf = transfer(c, d.ok() ? d.arrivals[0].delaySamples : 0); double coh = meanCoh(tf, 200, 5000);
        std::snprintf(ex, sizeof ex, "RT60 %.1f s, coh(200-5k) %.2f", rt, coh); row(rt < 1 ? "reverb RT60 0.5 s" : "reverb RT60 1.5 s", fs, r.directDelaySmp, d, ex);
        REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delaySamples == Approx(r.directDelaySmp).margin(tolSmp(fs)));
        REQUIRE(coh < cohPrev); REQUIRE(d.confidence > 0.5); cohPrev = coh; }
}

TEST_CASE("RoomSim SNR +10 / -10 dB: delay held, coherence ~ SNR/(1+SNR)", "[RoomSim][S8][D-CF]") {
    const double fs = 48000; double confPrev = 1.1;
    for (double snr : {10.0, -10.0}) { INFO("SNR " << snr);
        Room r; r.fs = fs; r.directDelaySmp = 230.4; r.snrDb = snr; r.seed = 505;
        auto c = simulate(r, 2.0); auto d = foh::findDelay(c.ref, c.mic, fs);
        // 4096-pt TF here: ~46 Welch frames in 2 s keeps the coherence estimator bias (~1/frames) small
        auto tf = transfer(c, d.ok() ? d.arrivals[0].delaySamples : 0, 4096); double coh = meanCoh(tf, 200, 10000), s = std::pow(10.0, snr / 10);
        char ex[64]; std::snprintf(ex, sizeof ex, "SNR %+.0f dB, coh %.3f (theory %.3f)", snr, coh, s / (1 + s)); row(snr > 0 ? "SNR +10 dB" : "SNR -10 dB", fs, 230.4, d, ex);
        REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delaySamples == Approx(230.4).margin(snr > 0 ? 0.1 : tolSmp(fs)));
        REQUIRE(coh == Approx(s / (1 + s)).margin(0.05));
        double m = 0; int n = 0; for (size_t k = bin(tf, 500); k <= bin(tf, 2000); ++k) { m += tf.magDb[k]; ++n; } // band mean: H1 is unbiased by mic noise
        REQUIRE(m / n == Approx(0.0).margin(snr > 0 ? 0.5 : 1.0));
        REQUIRE(d.confidence < confPrev); confPrev = d.confidence; }
}

TEST_CASE("RoomSim sub trap: 80 Hz LR4 split, sub 12.5 ms behind mains; phase helper vs raw peak", "[RoomSim][D-SUBGD][S15_D-SUB]") {
    const double fs = 48000;
    Room m; m.fs = fs; m.sub = true; m.xoHz = 80; m.directDelaySmp = 10.0e-3 * fs; m.subDelaySmp = 22.5e-3 * fs; m.seed = 606;
    Room mo = m; mo.mainsOnly = true; Room so = m; so.subOnly = true;
    auto cm = simulate(mo, 4.0), cs = simulate(so, 4.0);
    auto A = transfer(cm, 0), B = transfer(cs, 0);
    auto raw = foh::findDelay(cs.ref, cs.mic, fs, false);
    auto e = foh::subDelayFromPhase(A, B, 60, 100, 80, &raw);
    char ex[96]; std::snprintf(ex, sizeof ex, "helper %.2f ms alt %.2f/%.2f, raw peak %.2f ms", e.primaryMs, e.alternativesMs[0], e.alternativesMs[1], raw.ok() ? raw.arrivals[0].delayMs : NAN);
    row("sub trap 80 Hz", fs, 12.5e-3 * fs, raw, ex);
    REQUIRE(e.valid); REQUIRE(e.primaryMs == Approx(12.5).margin(0.25));
    REQUIRE(e.alternativesMs[0] == Approx(0.0).margin(0.25)); REQUIRE(e.alternativesMs[1] == Approx(25.0).margin(0.25));
    REQUIRE(raw.ok()); REQUIRE(std::fabs(raw.arrivals[0].delayMs - 22.5) > 1.0); // raw peak is not the sub's true arrival
    REQUIRE(e.hasHint);
    // S15: band-limited sub alone vs its own band-limited reference: 3 s window, sub options -> path delay +-0.1 ms
    Room bl = so; bl.directDelaySmp = 0; auto cb = simulate(bl, 3.0);
    auto ref0 = simulate([&] { Room z = so; z.subDelaySmp = 0; return z; }(), 3.0);
    auto s15 = foh::findDelay(ref0.mic, cb.mic, fs, foh::DelayOptions::forSubs());
    REQUIRE(s15.ok()); REQUIRE(s15.arrivals[0].delayMs == Approx(22.5).margin(0.1));
}

// Negative lags are opt-in; swapped inputs must explicitly enable the signed search.
TEST_CASE("RoomSim negative lag: mic earlier than reference reads negative when enabled", "[RoomSim][D-SGN]") {
    const double fs = GENERATE(44100.0, 48000.0, 96000.0); INFO("fs " << fs);
    Room r; r.fs = fs; r.directDelaySmp = 4.8e-3 * fs; r.seed = 707;
    auto c = simulate(r, 1.0); foh::DelayOptions options; options.minDelayMs = -10;
    auto d = foh::findDelay(c.mic, c.ref, fs, options); row("negative lag", fs, -r.directDelaySmp, d);
    REQUIRE(d.ok()); REQUIRE(d.arrivals[0].delaySamples == Approx(-r.directDelaySmp).margin(0.1));
}
