// Part A automation (docs/qa/test-checklist-build1.md, 70 items). Tags = Part A IDs.
// New core tests: D-DSP, D-ERR, D-2Sa flag, D-SGN (unbuilt -> shouldfail).
// App/UI-only items are SKIPped placeholders with the reason; see docs/qa/part-a-coverage.md.
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <cmath>
#include <vector>
using namespace foh; using Catch::Approx;

namespace {
std::vector<float> pinkN(int n, uint32_t seed) { PinkNoise p(seed); std::vector<float> v((size_t) n); for (auto& x : v) x = p.next(); return v; }
std::vector<float> fdelay(const std::vector<float>& x, double d) {   // band-limited delay via FFT phase shift
    int N = 1; while (N < (int) x.size() * 2) N <<= 1; FFT f(N); std::vector<cpx> X((size_t) N);
    for (size_t i = 0; i < x.size(); ++i) X[i] = x[i]; f.forward(X);
    for (int k = 0; k < N; ++k) { double kk = k <= N / 2 ? k : k - N; X[(size_t) k] *= std::polar(1.0, -2 * M_PI * kk * d / N); }
    if (N % 2 == 0) X[(size_t) N / 2] = X[(size_t) N / 2].real() * std::cos(M_PI * d);
    f.inverse(X); std::vector<float> y(x.size()); for (size_t i = 0; i < y.size(); ++i) y[i] = (float) X[i].real(); return y;
}
std::vector<float> mix(const std::vector<float>& a, const std::vector<float>& b, double bDb) {
    std::vector<float> y(a.size()); double g = std::pow(10.0, bDb / 20); for (size_t i = 0; i < y.size(); ++i) y[i] = a[i] + (float) (g * b[i]); return y; }
std::vector<float> head(const std::vector<float>& x, size_t n) { return {x.begin(), x.begin() + (long) std::min(n, x.size())}; }
}

TEST_CASE("D-DSP console delay display: 0.01 ms, round-half-up", "[PartA][D-DSP]") {
    REQUIRE(formatConsoleDelayMs(230.4 / 48000 * 1000) == "4.80");
    REQUIRE(formatConsoleDelayMs(230.0 / 48000 * 1000) == "4.79");
    REQUIRE(formatConsoleDelayMs(4.8049) == "4.80");
    REQUIRE(formatConsoleDelayMs(4.8050) == "4.81");
    REQUIRE(formatConsoleDelayMs(0.0) == "0.00");
    REQUIRE(formatConsoleDelayMs(12.995) == "13.00");
    REQUIRE(formatConsoleDelayMs(-4.8050) == "-4.81");
    REQUIRE(formatConsoleDelayMs(-0.001) == "0.00");
    REQUIRE(formatConsoleDelayMs(std::nan("")) == "--");
}

TEST_CASE("D-ERR findDelay returns an error (never throws) on bad sizes and empty input", "[PartA][D-ERR]") {
    auto p = pinkN(48000, 7); std::vector<float> e;
    for (auto [a, b] : std::vector<std::pair<const std::vector<float>*, const std::vector<float>*>>{{&e, &e}, {&p, &e}, {&e, &p}}) {
        DelayResult r; REQUIRE_NOTHROW(r = findDelay(*a, *b, 48000)); REQUIRE(!r.ok()); REQUIRE(!r.error.empty()); REQUIRE(r.arrivals.empty()); }
    std::vector<float> s63(63, 0.5f), s64 = head(p, 64);
    DelayResult r; REQUIRE_NOTHROW(r = findDelay(s63, p, 48000)); REQUIRE(!r.ok()); REQUIRE(!r.error.empty());
    REQUIRE_NOTHROW(r = findDelay(p, s63, 48000)); REQUIRE(!r.ok());
    // mismatched lengths: analysed over the common length, no throw; either ok or a clear error
    for (size_t n : {64u, 1000u, 47999u}) { REQUIRE_NOTHROW(r = findDelay(p, head(p, n), 48000)); REQUIRE((r.ok() || !r.error.empty())); }
    REQUIRE_NOTHROW(r = findDelay(head(p, 30000), fdelay(p, 100), 48000)); REQUIRE(r.ok()); REQUIRE(r.arrivals[0].delaySamples == Approx(100).margin(0.1));
    for (DelayOptions o : {DelayOptions{}, DelayOptions::forSubs()}) { REQUIRE_NOTHROW(r = findDelay(e, e, 48000, o)); REQUIRE(!r.ok()); }
}

TEST_CASE("D-2Sa end-to-end two-spike: 3.00 + 4.50 ms at -0.5 dB sets the flag", "[PartA][D-2Sa]") {
    for (double fs : {44100.0, 48000.0, 96000.0}) { INFO("fs " << fs);
        auto src = pinkN((int) fs * 2, 3); auto ref = head(src, (size_t) fs);
        auto clean = findDelay(ref, head(fdelay(src, 0.003 * fs), (size_t) fs), fs);
        auto two = findDelay(ref, head(mix(fdelay(src, 0.003 * fs), fdelay(src, 0.0045 * fs), -0.5), (size_t) fs), fs);
        REQUIRE(two.ok()); REQUIRE(two.twoSimilarArrivals); REQUIRE(!clean.twoSimilarArrivals);
        REQUIRE(two.arrivals[0].delayMs == Approx(3.0).margin(0.05));
        bool second = false; for (auto& a : two.arrivals) if (std::fabs(a.delayMs - 4.5) < 0.05) second = true; REQUIRE(second);
        REQUIRE(two.confidence < clean.confidence); }
}

// D-SGN: ref delayed relative to meas must read -4.80 ms. findDelay searches positive lags only,
// so this is unbuilt: expected to fail until negative lags are supported.
TEST_CASE("D-SGN sign: ref later than meas reads negative", "[PartA][D-SGN][!shouldfail]") {
    auto src = pinkN(96000, 5); auto ref = head(fdelay(src, 230.4), 48000), meas = head(src, 48000);
    auto r = findDelay(ref, meas, 48000); REQUIRE(r.ok()); REQUIRE(r.arrivals[0].delayMs == Approx(-4.80).margin(0.05));
}

// ---- placeholders: app/UI-only or code-review items (not testable in core) ----
#define PART_A_SKIP(id, title, why) TEST_CASE(id " " title, "[PartA][" id "][placeholder]") { SKIP(why); }
PART_A_SKIP("T4b", "UI blanking default 0.5", "app/UI: TF panel default; Part B H13")
PART_A_SKIP("T6b", "app guide uses core classifier", "code check over app/Source (no colour thresholds); not a core test")
PART_A_SKIP("S17", "ref taken post-DUT help text", "doc/UI check only")
PART_A_SKIP("D-1S", "Find again / Insert are one-shot", "app/controller behaviour; Part B H10")
PART_A_SKIP("P-NA", "no polarity recommendation path", "code/UI review; no core API to test")
PART_A_SKIP("C11", "per-input cal persists, removable", "app-level persistence; Part B H8")
PART_A_SKIP("R-FS", "RTA reads dBFS only, no SPL labels", "code/UI check of app labels")
PART_A_SKIP("WS1", "workspace presets + Cmd+1..4", "app-level; Part B H15 (workspaces not on this base)")
PART_A_SKIP("WS2", "tear-off position restore", "app-level unless serialisation is factored into core; Part B H16")
PART_A_SKIP("WS3", "theme tokens + contrast", "box script over app FohTheme.h; not a core test")
