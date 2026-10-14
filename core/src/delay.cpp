#include "dsp_safety.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace foh {
static int nextPow2(int n) { int p = 1; while (p < n && p < (1 << 30)) p <<= 1; return p; } // callers keep n <= 2^24
// float -> int samples, saturating (no UB for huge/NaN values)
static int toSamples(double v, int lo, int hi) {
    if (!(v == v)) return lo; if (v <= lo) return lo; if (v >= hi) return hi; return (int) v; }
static constexpr int kMaxWindow = 1 << 23;
static constexpr double kMinLpHz = 10.0; // ~175 s @ 48k; keeps FFT size within int

bool twoSpikeFlag(const std::vector<Arrival>& a) {
    return a.size() >= 2 && twoSpikeFlag(0.0, a[1].relLevelDb - a[0].relLevelDb, a[1].delayMs - a[0].delayMs);
}
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double fs,
                      bool phat, int maxArr, double minSepMs, double c) {
    DelayOptions o; o.phat = phat; o.maxArrivals = maxArr; o.minSeparationMs = minSepMs; o.speedOfSound = c;
    return findDelay(ref, meas, fs, o);
}
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double fs, const DelayOptions& opt) {
    ScopedFlushDenormals ftz;
    DelayResult res;
    if (fs < 1 || !std::isfinite(fs)) { res.error = "invalid sample rate"; return res; }
    size_t Ls = std::min(ref.size(), meas.size());
    if (Ls < 64) { res.error = "window too short (< 64 samples)"; return res; }
    int L = (int) std::min(Ls, (size_t) kMaxWindow); res.samplesUsed = L; res.truncated = Ls > (size_t) kMaxWindow;
    const int maxArr = std::clamp(opt.maxArrivals, 0, 16); const double c = std::isfinite(opt.speedOfSound) && opt.speedOfSound > 0 ? opt.speedOfSound : 343.0;
    // band weighting sanity: lpHz floored at kMinLpHz (a 1e-12 Hz low-pass would zero the
    // whole spectrum and leave only numerical noise), both edges clamped below Nyquist,
    // non-finite edges ignored, and an empty band (hp >= lp) is an error, not a guess
    double lpHz = std::isfinite(opt.lpHz) && opt.lpHz > 0 ? std::min(std::max(opt.lpHz, kMinLpHz), 0.5 * fs) : 0.0;
    double hpHz = std::isfinite(opt.hpHz) && opt.hpHz > 0 ? std::min(opt.hpHz, 0.5 * fs) : 0.0;
    if (lpHz > 0 && hpHz >= lpHz) { res.error = "empty band (hpHz >= lpHz)"; return res; }
    int N = nextPow2(2 * L); FFT fft(N);
    std::vector<cpx> X((size_t) N), Y((size_t) N), Tspec((size_t) N), Espec((size_t) N / 2 + 1);
    bool anyX = false, anyY = false;
    for (int i = 0; i < L; ++i) {
        float x = ref[(size_t) i], y = meas[(size_t) i];
        if (!std::isfinite(x)) { x = 0; ++res.nonFiniteSamples; } if (!std::isfinite(y)) { y = 0; ++res.nonFiniteSamples; }
        x = detail::cleanSample(x); y = detail::cleanSample(y);
        anyX |= x != 0; anyY |= y != 0; X[(size_t) i] = x; Y[(size_t) i] = y; }
    if (!anyX || !anyY) { res.error = !anyX ? "reference is silent" : "measurement is silent"; return res; }
    fft.forward(X); fft.forward(Y);
    // Y <- unweighted (band-weighted only) cross spectrum, X <- timing (PHAT) cross spectrum
    double meanPx = 0; if (opt.levelMode == DelayOptions::LevelMode::RefWhitened) { for (auto& v : X) meanPx += std::norm(v); meanPx /= N; }
    for (int k = 0; k < N; ++k) { cpx g = Y[(size_t) k] * std::conj(X[(size_t) k]);
        cpx gl = opt.levelMode == DelayOptions::LevelMode::RefWhitened ? g / (std::norm(X[(size_t) k]) + 1e-3 * meanPx + 1e-30) : g;
        double w = 1.0;
        if (lpHz > 0 || hpHz > 0) { double f = (k <= N / 2 ? k : N - k) * fs / N;
            if (lpHz > 0) w *= 1.0 / std::sqrt(1.0 + std::pow(f / lpHz, 8.0));
            if (hpHz > 0) w *= f > 0 ? 1.0 / std::sqrt(1.0 + std::pow(hpHz / f, 8.0)) : 0.0; }
        cpx t = g;
        if (opt.phat) { double m = std::abs(g); t = m > 1e-20 ? g / m : cpx{}; }
        X[(size_t) k] = t * w; Tspec[(size_t) k] = t * w;
        // analytic signal of the unweighted correlation: keep DC/Nyquist, double positive, zero negative
        double h = (k == 0 || k == N / 2) ? 1.0 : (k < N / 2 ? 2.0 : 0.0);
        Y[(size_t) k] = gl * (w * h); if (k <= N / 2) Espec[(size_t) k] = Y[(size_t) k]; }
    fft.inverse(X);
    // lag range [-nNeg, L): positive lags by default (meas later than ref), bounded by maxDelayMs;
    // negative lags only when minDelayMs < 0 (N >= 2L so negative lags never alias positive ones)
    const int Lw = L;
    if (std::isfinite(opt.maxDelayMs) && opt.maxDelayMs > 0) L = std::min(L, std::max(2, toSamples(opt.maxDelayMs * 1e-3 * fs, 0, kMaxWindow) + 2));
    int nNeg = 0;
    if (std::isnan(opt.minDelayMs) || opt.minDelayMs < 0)
        nNeg = std::isfinite(opt.minDelayMs) ? std::min(Lw - 1, toSamples(-opt.minDelayMs * 1e-3 * fs, 0, kMaxWindow) + 2) : Lw - 1;
    const int M = nNeg + L;
    std::vector<double> r((size_t) M);
    for (int j = 0; j < M; ++j) { int lag = j - nNeg; size_t q = (size_t) (lag < 0 ? lag + N : lag);
        r[(size_t) j] = X[q].real(); }
    res.impulse.assign(r.begin() + nNeg, r.end());
    std::vector<double> a(r.size()); for (size_t i = 0; i < r.size(); ++i) a[i] = std::fabs(r[i]);
    int sep = std::max(1, toSamples(opt.minSeparationMs * 1e-3 * fs, 1, M));
    std::vector<char> used(a.size(), 0); double peak0 = 0;
    for (int m = 0; m < maxArr; ++m) {
        int best = -1; double bv = 0;
        for (size_t i = 0; i < a.size(); ++i) if (!used[i] && a[i] > bv) { bv = a[i]; best = (int) i; }
        if (best < 0 || !(bv > 0)) break;
        if (m == 0) peak0 = bv; else if (bv < peak0 * 0.05) break; // < -26 dB: ignore
        double off = 0;
        if (best > 0 && best + 1 < (int) a.size()) { double y0 = a[(size_t) best-1], y1 = a[(size_t) best], y2 = a[(size_t) best+1];
            double d = y0 - 2*y1 + y2; if (std::fabs(d) > 1e-30) off = std::clamp(0.5 * (y0 - y2) / d, -0.5, 0.5); }
        double ds = best - nNeg + off;
        Arrival ar{ds, ds / fs * 1000.0, ds / fs * c, 0.0, r[(size_t) best] < 0, 20.0 * std::log10(bv / peak0)};
        res.arrivals.push_back(ar);
        for (int i = std::max(0, best - sep); i <= std::min((int) a.size() - 1, best + sep); ++i) used[(size_t) i] = 1;
    }
    if (res.arrivals.empty()) { res.error = "no correlation peak"; return res; }
    // sub-sample refinement: maximise |r(tau)| of the band-limited timing correlation,
    // evaluated exactly as (1/N) sum_k T_k e^{+j 2 pi k' tau / N} (k' signed), golden section in +-1 sample
    auto rAt = [&](double tau) {
        cpx acc{}; const double w0 = 2.0 * M_PI * tau / N;
        cpx rot = std::polar(1.0, w0), ph{1.0, 0.0};
        for (int k = 0; k <= N / 2; ++k) { acc += Tspec[(size_t) k] * ph; ph *= rot; }
        ph = std::polar(1.0, -w0); rot = std::conj(rot); cpx p2 = ph;
        for (int k = N - 1; k > N / 2; --k) { acc += Tspec[(size_t) k] * p2; p2 *= rot; }
        return acc.real() / N; };
    for (auto& ar : res.arrivals) {
        double lo = std::floor(ar.delaySamples + 0.5) - 1.0, hi = lo + 2.0; const double gr = 0.6180339887498949;
        double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo), f1 = std::fabs(rAt(x1)), f2 = std::fabs(rAt(x2));
        for (int it = 0; it < 30; ++it) {
            if (f1 > f2) { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo); f1 = std::fabs(rAt(x1)); }
            else { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo); f2 = std::fabs(rAt(x2)); } }
        double ds = std::clamp(0.5 * (lo + hi), (double) -nNeg, (double) L - 1);
        ar.delaySamples = ds; ar.delayMs = ds / fs * 1000.0; ar.distanceM = ds / fs * c; ar.inverted = rAt(ds) < 0;
    }
    // level: |analytic correlation| evaluated exactly at the fractional delay (band-limited DFT sum),
    // not linear interpolation between samples; the latter under-reads a peak that falls between
    // samples (e.g. 2 ms = 88.2 smp at 44.1k) and made the S16 -3 dB reflection read -2.5 dB at 44.1k only
    auto envAt = [&](double tau) { cpx acc{}; cpx rot = std::polar(1.0, 2.0 * M_PI * tau / N), ph{1.0, 0.0};
        for (int k = 0; k <= N / 2; ++k) { acc += Espec[(size_t) k] * ph; ph *= rot; } return std::abs(acc) / N; };
    double e0 = envAt(res.arrivals[0].delaySamples);
    for (auto& ar : res.arrivals) { double e = envAt(ar.delaySamples); ar.relLevelDb = (e0 > 0 && e > 0) ? 20.0 * std::log10(e / e0) : -120.0; }
    res.arrivals.erase(std::remove_if(res.arrivals.begin() + 1, res.arrivals.end(), [](const Arrival& a) { return a.relLevelDb < -26.0; }), res.arrivals.end());
    // confidence: peak vs RMS of the timing correlation outside the main lobe, mapped 0..1
    int excl = std::max(sep, lpHz > 0 ? toSamples(fs / lpHz, 1, L) : sep);
    int b0 = (int) std::lround(res.arrivals[0].delaySamples) + nNeg;
    double s = 0; size_t cnt = 0;
    for (size_t i = 0; i < a.size(); ++i) if (std::abs((int) i - b0) > excl) { s += a[i] * a[i]; ++cnt; }
    double rms = cnt ? std::sqrt(s / cnt) : 0;
    double pnr = rms > 0 ? peak0 / rms : 1e9;
    // log map: 14 dB peak-to-noise -> 0, 54 dB -> 1 (does not saturate for moderate SNR,
    // so a -10 dB SNR capture ranks below a clean one even with GCC-PHAT)
    res.confidence = std::clamp((20.0 * std::log10(std::max(pnr, 1e-9)) - 14.0) / 40.0, 0.0, 1.0);
    res.twoSimilarArrivals = twoSpikeFlag(res.arrivals);
    if (res.twoSimilarArrivals) res.confidence *= 0.75; // ambiguous: penalise, but a clean two-spike still beats a -10 dB SNR capture
    return res;
}
std::vector<double> impulseFromTF(const TFResult& tf) {
    int N = tf.fftSize; if (!isPowerOfTwo(N) || N > (1 << 24)) return {};
    if (N < 2 || !std::isfinite(tf.sampleRate) || tf.sampleRate < 1
        || tf.H.size() != size_t(N / 2 + 1)) return {};
    bool signal = false;
    for (size_t k = 0; k < tf.H.size(); ++k) {
        auto h = tf.H[k];
        if (!std::isfinite(h.real()) || !std::isfinite(h.imag()) || std::abs(h) > 1e100) return {};
        if (k > 0 && !tf.isBlanked(k) && std::abs(h) > 1e-20) signal = true;
    }
    if (!signal) return {};
    std::vector<cpx> d((size_t) N);
    for (int k = 0; k <= N / 2; ++k) { d[(size_t) k] = tf.isBlanked(size_t(k)) ? cpx{} : tf.H[(size_t) k]; if (k > 0 && k < N / 2) d[(size_t) (N - k)] = std::conj(d[(size_t) k]); }
    FFT(N).inverse(d); std::vector<double> out((size_t) N); for (int i = 0; i < N; ++i) out[(size_t) i] = d[(size_t) i].real();
    return out;
}
}
