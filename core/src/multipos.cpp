#include "fohcore/fohcore.h"
#include <algorithm>

namespace foh {
TFResult multiPositionAverage(const std::vector<MicPosition>& positions) {
    if (positions.size() < 2 || positions.size() > 3) return {};
    const auto& first = positions.front().tf;
    const auto n = first.freq.size();
    if (n < 2 || !std::isfinite(first.sampleRate) || first.sampleRate < 1
        || !isPowerOfTwo(first.fftSize) || first.fftSize < 2
        || !std::isfinite(first.blankThreshold) || first.blankThreshold < 0 || first.blankThreshold > 1) return {};
    for (const auto& p : positions) {
        const auto& t = p.tf;
        if (!std::isfinite(p.delayMs) || t.freq != first.freq || t.sampleRate != first.sampleRate
            || t.fftSize != first.fftSize || t.H.size() != n || t.coherence.size() != n) return {};
    }
    TFResult out;
    out.sampleRate = first.sampleRate; out.fftSize = first.fftSize; out.freq = first.freq;
    out.blankThreshold = first.blankThreshold;
    out.H.resize(n); out.magDb.resize(n); out.phaseDeg.resize(n);
    out.coherence.resize(n); out.blanked.assign(n, 1);
    for (size_t k = 0; k < n; ++k) {
        if (!std::isfinite(out.freq[k]) || out.freq[k] < 0 || out.freq[k] > out.sampleRate / 2
            || (k > 0 && out.freq[k] <= out.freq[k-1])) return {};
        cpx sum{}; double weight = 0, coh = 0;
        for (const auto& p : positions) {
            const auto& t = p.tf; double w = t.coherence[k];
            if (t.isBlanked(k) || !std::isfinite(w) || w <= 0 || w > 1
                || !std::isfinite(t.H[k].real()) || !std::isfinite(t.H[k].imag())
                || !(std::abs(t.H[k]) > 1e-20) || std::abs(t.H[k]) > 1e100) continue;
            // A delayed response has negative phase slope; advance it before summing.
            double phase = 2 * M_PI * out.freq[k] * p.delayMs / 1000.0;
            if (!std::isfinite(phase)) continue;
            sum += w * t.H[k] * std::polar(1.0, std::remainder(phase, 2 * M_PI));
            weight += w; coh += w * w;
        }
        if (weight > 0 && std::abs(sum / weight) > 1e-20) {
            out.H[k] = sum / weight; out.coherence[k] = coh / weight;
            out.blanked[k] = out.coherence[k] < out.blankThreshold;
        }
        out.magDb[k] = 20 * std::log10(std::max(1e-20, std::abs(out.H[k])));
        out.phaseDeg[k] = std::arg(out.H[k]) * 180 / M_PI;
    }
    return out;
}
}
