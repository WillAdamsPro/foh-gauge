#pragma once
#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace foh::detail {
inline float cleanSample(float x) noexcept {
    return std::isfinite(x) && std::abs(x) >= std::numeric_limits<float>::min() ? x : 0.f;
}
// Analysis frames reject clipping/nonfinite input and remove DC before windowing.
inline bool frameMean(const std::vector<float>& samples, double& mean) {
    mean = 0;
    int rails = 0, plateau = 0;
    float previous = 0;
    for (float x : samples) {
        if (!std::isfinite(x) || std::abs(x) > 1e6f) return false;
        // Rail hits alone include a valid full-scale sine's isolated peaks.
        // Reject sustained digital clipping, including alternating rail samples.
        if (std::abs(x) == 1.f) {
            ++rails; plateau = x == previous ? plateau + 1 : 1;
            if (plateau >= 3) return false;
        } else plateau = 0;
        previous = x;
        mean += cleanSample(x);
    }
    if (rails > (int) samples.size() / 4) return false;
    mean /= samples.size();
    return true;
}
// Keep public spectrum operations away from overflow, including prefix sums and norms.
inline double cleanSpectrum(double x) {
    return std::isfinite(x) ? std::clamp(x, -1e100, 1e100) : 0.0;
}
inline cpx cleanSpectrum(cpx x) {
    return {cleanSpectrum(x.real()), cleanSpectrum(x.imag())};
}
inline double sampleRate(double sr) {
    if (!std::isfinite(sr) || sr < 1) throw std::invalid_argument("sample rate must be finite and >= 1 Hz");
    return sr;
}
inline int analysisSize(int n) {
    if (n < 2) throw std::invalid_argument("analysis FFT size must be >= 2");
    return n;
}
inline double correction(double db) {
    return std::isfinite(db) ? std::clamp(db, -240.0, 240.0) : 0.0;
}
}
