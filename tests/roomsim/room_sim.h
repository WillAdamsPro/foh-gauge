// Simulated room for FOH Gauge core tests (core-only, no JUCE). Impulse responses are built at
// runtime from fixed seeds; nothing is checked in. Everything is done in the frequency domain on
// one FFT grid, so fractional delays are exact (band-limited) and the LR4 split is the analytic
// Linkwitz-Riley response.
//
//   mic = IFFT( X * H ) + noise,   H = direct + reflections + tail   (optionally split at xo:
//         H = HP4(f) * H_mains * e^{-jw dMains} + LP4(f) * gSub * e^{-jw dSub})
//   ref = source (pre-DUT loopback). Both are steady-state windows (no leading silence).
#pragma once
#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <random>
#include <vector>

namespace roomsim {
using cpx = std::complex<double>;

struct Reflection { double delaySmp; double gainDb; };
struct Room {
    double fs = 48000;
    double directDelaySmp = 0, directGainDb = 0;
    std::vector<Reflection> reflections;           // delays absolute (samples), gain vs. direct
    double rt60Low = 0, rt60High = 0;              // s; 0 = no tail. Split at tailSplitHz (one-pole bands)
    double tailSplitHz = 500, tailLevelDb = -6;     // tail energy relative to the direct sound
    double tailStartSmp = -1;                      // default: direct + 1 ms
    bool sub = false; double xoHz = 80, subDelaySmp = 0, subGainDb = 0; // LR4 split, sub path delay
    bool mainsOnly = false, subOnly = false;        // play just one side of the split (W2 style)
    double snrDb = 1e9;                            // additive white noise at the mic (vs. mic RMS)
    uint32_t seed = 1;
};

struct Capture { std::vector<float> ref, mic; double fs; };

inline int nextPow2(size_t n) { int p = 1; while ((size_t) p < n) p <<= 1; return p; }

inline std::vector<double> whiteNoise(size_t n, uint32_t seed, double rms = 0.25) {
    std::mt19937 g(seed); std::normal_distribution<double> nd(0, rms); std::vector<double> v(n); for (auto& x : v) x = nd(g); return v; }

// Exponential-decay noise tail, two bands (low/high split by one-pole filters), unit-energy normalised then scaled.
inline std::vector<double> tailIR(const Room& r, size_t len) {
    std::vector<double> lo(len), hi(len); auto n1 = whiteNoise(len, r.seed * 7919u + 11, 1.0), n2 = whiteNoise(len, r.seed * 7919u + 13, 1.0);
    double a = std::exp(-2 * M_PI * r.tailSplitHz / r.fs), yl = 0, yh = 0;
    for (size_t i = 0; i < len; ++i) { double t = (double) i / r.fs;
        yl = (1 - a) * n1[i] + a * yl; double l = n1[i]; yh = (1 - a) * n2[i] + a * yh; double h = n2[i] - yh; (void) l;
        lo[i] = yl * (r.rt60Low > 0 ? std::exp(-6.9078 * t / r.rt60Low) : 0); hi[i] = h * (r.rt60High > 0 ? std::exp(-6.9078 * t / r.rt60High) : 0); }
    std::vector<double> out(len); double e = 0; for (size_t i = 0; i < len; ++i) { out[i] = lo[i] + hi[i]; e += out[i] * out[i]; }
    double g = e > 0 ? std::pow(10.0, r.tailLevelDb / 20) * std::pow(10.0, r.directGainDb / 20) / std::sqrt(e) : 0; for (auto& v : out) v *= g; return out;
}

// LR4 = Butterworth-2 squared (analytic, s = j f / fx). HP4 + LP4 is allpass.
inline cpx lr4lp(double f, double fx) { cpx s(0, f / fx); cpx b = 1.0 / (s * s + std::sqrt(2.0) * s + 1.0); return b * b; }
inline cpx lr4hp(double f, double fx) { cpx s(0, f / fx); cpx b = (s * s) / (s * s + std::sqrt(2.0) * s + 1.0); return b * b; }

// Simulate `seconds` of capture. The source is white noise (seed) unless `source` is given.
inline Capture simulate(const Room& r, double seconds) {
    const size_t L = (size_t) std::lround(seconds * r.fs);
    double maxD = std::max({r.directDelaySmp, r.subDelaySmp, 0.0}); for (auto& x : r.reflections) maxD = std::max(maxD, x.delaySmp);
    const bool tail = r.rt60Low > 0 || r.rt60High > 0;
    const size_t tailLen = tail ? (size_t) (std::max(r.rt60Low, r.rt60High) * r.fs) : 0;
    // warm-up so the window is steady state; fixed (not delay-dependent) so two rooms with the
    // same seed see the same source window. Delays must stay below 0.5 s.
    const size_t pre = (size_t) (0.5 * r.fs) + tailLen + 1024;
    const size_t M = pre + L; const int N = nextPow2(M + (size_t) maxD + tailLen + 1024);
    auto src = whiteNoise(M, r.seed);
    foh::FFT fft(N); std::vector<cpx> X((size_t) N), T((size_t) N);
    for (size_t i = 0; i < M; ++i) X[i] = src[i]; fft.forward(X);
    if (tail) { auto t = tailIR(r, tailLen); double t0 = r.tailStartSmp >= 0 ? r.tailStartSmp : r.directDelaySmp + 0.001 * r.fs;
        size_t o = (size_t) std::lround(t0); for (size_t i = 0; i < tailLen && o + i < (size_t) N; ++i) T[o + i] = t[i]; fft.forward(T); }
    const double gD = std::pow(10.0, r.directGainDb / 20), gS = std::pow(10.0, r.subGainDb / 20);
    for (int k = 0; k < N; ++k) {
        const double kk = k <= N / 2 ? k : k - N, w = -2 * M_PI * kk / N, f = std::fabs(kk) * r.fs / N;
        cpx Hm = gD * std::polar(1.0, w * r.directDelaySmp);
        for (auto& x : r.reflections) Hm += gD * std::pow(10.0, x.gainDb / 20) * std::polar(1.0, w * x.delaySmp);
        if (tail) Hm += T[(size_t) k];
        cpx H = Hm;
        if (r.sub) { cpx hp = lr4hp(f, r.xoHz), lp = lr4lp(f, r.xoHz); if (kk < 0) { hp = std::conj(hp); lp = std::conj(lp); }
            cpx Hs = gS * lp * std::polar(1.0, w * r.subDelaySmp);
            H = r.subOnly ? Hs : r.mainsOnly ? hp * Hm : hp * Hm + Hs; }
        if (k == N / 2) H = H.real();
        X[(size_t) k] *= H; }
    fft.inverse(X);
    Capture c; c.fs = r.fs; c.ref.resize(L); c.mic.resize(L);
    double e = 0; for (size_t i = 0; i < L; ++i) { c.ref[i] = (float) src[pre + i]; double y = X[pre + i].real(); e += y * y; c.mic[i] = (float) y; }
    if (r.snrDb < 1e8) { auto n = whiteNoise(L, r.seed * 31u + 5, std::sqrt(e / (double) L) * std::pow(10.0, -r.snrDb / 20));
        for (size_t i = 0; i < L; ++i) c.mic[i] += (float) n[i]; }
    return c;
}

// Run a capture through the core TF (16384, infinite average) with a given reference delay.
inline foh::TFResult transfer(const Capture& c, double refDelaySmp, int fftSize = 16384) {
    foh::TransferFunction tf(fftSize, c.fs); tf.setAverages(0); tf.setReferenceDelaySamples(refDelaySmp);
    tf.process(c.ref.data(), c.mic.data(), (int) c.ref.size()); return tf.result();
}
inline size_t bin(const foh::TFResult& r, double f) { return (size_t) std::lround(f / (r.sampleRate / r.fftSize)); }
inline double meanCoh(const foh::TFResult& r, double lo, double hi) { double s = 0; int n = 0; for (size_t k = bin(r, lo); k <= bin(r, hi); ++k) { s += r.coherence[k]; ++n; } return n ? s / n : 0; }
} // namespace roomsim
