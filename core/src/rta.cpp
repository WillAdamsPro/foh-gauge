#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
namespace foh {
std::vector<Band> octaveBands(int fraction, double fmin, double fmax) {
    std::vector<Band> b; double G = std::pow(10.0, 0.3); // base-10 octave ratio
    double half = std::pow(G, 0.5 / fraction);
    for (int x = -60; x <= 60; ++x) {
        double fc = 1000.0 * std::pow(G, (double) x / fraction);
        if (fc < fmin / 1.01 || fc > fmax * 1.01) continue;
        b.push_back({fc, fc / half, fc * half});
    }
    return b;
}
RTA::RTA(int n, double sr, int frac) : fft(n), N(n), fs(sr), fraction(frac), win(hannWindow(n)),
    power((size_t) n / 2 + 1, 0.0), buf((size_t) n), X((size_t) n) { bandList = octaveBands(frac); for (double w : win) winPowerSum += w; }
void RTA::setFraction(int f) { fraction = f; bandList = octaveBands(f); }
void RTA::reset() { std::fill(power.begin(), power.end(), 0.0); fill = 0; any = false; }
void RTA::process(const float* x, int n) {
    for (int i = 0; i < n; ++i) { buf[(size_t) fill] = x[i];
        if (++fill == N) { frame(); std::copy(buf.begin() + N / 2, buf.end(), buf.begin()); fill = N / 2; } }
}
void RTA::frame() {
    for (int i = 0; i < N; ++i) X[(size_t) i] = buf[(size_t) i] * win[(size_t) i];
    fft.forward(X);
    double norm = 2.0 / (winPowerSum * winPowerSum); // full-scale sine peak bin -> ~ amplitude^2
    for (size_t k = 0; k < power.size(); ++k) { double p = std::norm(X[k]) * norm; 
        power[k] = any ? power[k] + avgAlpha * (p - power[k]) : p; }
    any = true;
}
std::vector<double> RTA::bandsDb() const {
    std::vector<double> out; double bin = fs / N;
    // ENBW of Hann = 1.5 bins: divide summed power accordingly
    // each bin covers [(k-0.5), (k+0.5)] * bin; bins straddling a band edge contribute by overlap
    // fraction, so narrow LF bands are not quantised to whole bins (S9: +-0.5 dB on pink)
    for (auto& b : bandList) { double s = 0;
        long k0 = std::max(1L, (long) std::floor(b.fLo / bin - 0.5)), k1 = std::min((long) power.size() - 1, (long) std::ceil(b.fHi / bin + 0.5));
        for (long k = k0; k <= k1; ++k) {
            double lo = (k - 0.5) * bin, hi = (k + 0.5) * bin, ov = std::min(hi, b.fHi) - std::max(lo, b.fLo);
            if (ov <= 0) continue; double p = power[(size_t) k] * (ov / bin);
            if ((size_t) k < calDb.size()) p *= std::pow(10.0, calDb[(size_t) k] / 10.0); s += p; }
        out.push_back(10.0 * std::log10(s / 1.5 + 1e-20) + 3.0103); } // +3 dB: sine RMS -> peak-referenced dBFS
    return out;
}
}
