#pragma once
#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace foh::detail {
inline float cleanSample(float x) noexcept {
    return std::isfinite(x) && std::abs(x) >= std::numeric_limits<float>::min() ? x : 0.f;
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
