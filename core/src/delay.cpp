#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
namespace foh {
static int nextPow2(int n) { int p = 1; while (p < n) p <<= 1; return p; }
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double fs,
                      bool phat, int maxArr, double minSepMs, double c) {
    DelayOptions o; o.phat = phat; o.maxArrivals = maxArr; o.minSeparationMs = minSepMs; o.speedOfSound = c;
    return findDelay(ref, meas, fs, o);
}
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double fs, const DelayOptions& opt) {
    const bool phat = opt.phat; const int maxArr = opt.maxArrivals; const double minSepMs = opt.minSeparationMs, c = opt.speedOfSound;
    DelayResult res; int L = (int) std::min(ref.size(), meas.size()); if (L < 64) return res;
    int N = nextPow2(2 * L); FFT fft(N);
    std::vector<cpx> X((size_t) N), Y((size_t) N);
    for (int i = 0; i < L; ++i) { X[(size_t) i] = ref[(size_t) i]; Y[(size_t) i] = meas[(size_t) i]; }
    fft.forward(X); fft.forward(Y);
    for (int k = 0; k < N; ++k) { cpx g = Y[(size_t) k] * std::conj(X[(size_t) k]);
        if (phat) { double m = std::abs(g); g = m > 1e-20 ? g / m : cpx{}; }
        if (opt.lpHz > 0 || opt.hpHz > 0) { double f = (k <= N / 2 ? k : N - k) * fs / N, w = 1.0;
            if (opt.lpHz > 0) w *= 1.0 / std::sqrt(1.0 + std::pow(f / opt.lpHz, 8.0));
            if (opt.hpHz > 0) w *= f > 0 ? 1.0 / std::sqrt(1.0 + std::pow(opt.hpHz / f, 8.0)) : 0.0;
            g *= w; }
        X[(size_t) k] = g; }
    fft.inverse(X);
    // positive lags only (meas lags ref); limit to L-1
    if (opt.maxDelayMs > 0) L = std::min(L, std::max(2, (int) (opt.maxDelayMs * 1e-3 * fs) + 2));
    std::vector<double> r((size_t) L); for (int i = 0; i < L; ++i) r[(size_t) i] = X[(size_t) i].real();
    res.impulse = r;
    std::vector<double> a(r.size()); for (size_t i = 0; i < r.size(); ++i) a[i] = std::fabs(r[i]);
    int sep = std::max(1, (int) (minSepMs * 1e-3 * fs));
    std::vector<char> used(a.size(), 0); double peak0 = 0;
    for (int m = 0; m < maxArr; ++m) {
        int best = -1; double bv = 0;
        for (size_t i = 0; i < a.size(); ++i) if (!used[i] && a[i] > bv) { bv = a[i]; best = (int) i; }
        if (best < 0 || bv <= 0) break;
        if (m == 0) peak0 = bv; else if (bv < peak0 * 0.05) break; // < -26 dB: ignore
        double off = 0;
        if (best > 0 && best + 1 < (int) a.size()) { double y0 = a[(size_t) best-1], y1 = a[(size_t) best], y2 = a[(size_t) best+1];
            double d = y0 - 2*y1 + y2; if (std::fabs(d) > 1e-30) off = 0.5 * (y0 - y2) / d; }
        double ds = best + off;
        res.arrivals.push_back({ds, ds / fs * 1000.0, ds / fs * c, 20.0 * std::log10(bv / peak0), r[(size_t) best] < 0});
        for (int i = std::max(0, best - sep); i <= std::min((int) a.size() - 1, best + sep); ++i) used[(size_t) i] = 1;
    }
    // confidence: peak vs RMS of correlation (peak-to-noise), mapped 0..1
    // noise floor excludes the main lobe of the first arrival (wide when band-limited)
    int excl = std::max(sep, opt.lpHz > 0 ? (int) (fs / opt.lpHz) : sep);
    int b0 = res.arrivals.empty() ? -1 : (int) std::lround(res.arrivals[0].delaySamples);
    double s = 0; size_t cnt = 0;
    for (size_t i = 0; i < a.size(); ++i) if (b0 < 0 || std::abs((int) i - b0) > excl) { s += a[i] * a[i]; ++cnt; }
    double rms = cnt ? std::sqrt(s / cnt) : 0;
    double pnr = rms > 0 ? peak0 / rms : (peak0 > 0 ? 1e9 : 0);
    res.confidence = std::clamp((pnr - 5.0) / 25.0, 0.0, 1.0);
    if (res.arrivals.size() > 1 && res.arrivals[1].relLevelDb > -2.0) res.confidence *= 0.5; // ambiguous
    return res;
}
std::vector<double> impulseFromTF(const TFResult& tf) {
    int N = tf.fftSize; std::vector<cpx> d((size_t) N);
    for (int k = 0; k <= N / 2 && k < (int) tf.H.size(); ++k) { d[(size_t) k] = tf.H[(size_t) k]; if (k > 0 && k < N / 2) d[(size_t) (N - k)] = std::conj(tf.H[(size_t) k]); }
    FFT(N).inverse(d); std::vector<double> out((size_t) N); for (int i = 0; i < N; ++i) out[(size_t) i] = d[(size_t) i].real();
    return out;
}
}
