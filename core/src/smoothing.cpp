#include "dsp_safety.h"
#include <cmath>
namespace foh {
template <typename T>
static std::vector<T> smoothImpl(const std::vector<T>& v, double binHz, int fraction) {
    std::vector<T> clean; clean.reserve(v.size());
    for (auto x : v) clean.push_back(detail::cleanSpectrum(x));
    if (fraction <= 0 || v.size() < 3 || !std::isfinite(binHz) || binHz <= 0) return clean;
    size_t n = v.size(); std::vector<T> pre(n + 1, T{}); // prefix sums
    for (size_t i = 0; i < n; ++i) pre[i + 1] = pre[i] + clean[i];
    std::vector<T> out(n); double k = std::pow(2.0, 0.5 / fraction);
    out[0] = clean[0];
    for (size_t i = 1; i < n; ++i) {
        // binHz cancels: use bin indices directly, avoiding overflow/invalid casts.
        long lo = std::max(1L, (long) std::floor(i / k)), hi = std::min((long) n - 1, (long) std::ceil(i * k));
        out[i] = (pre[(size_t) hi + 1] - pre[(size_t) lo]) / (double) (hi - lo + 1);
    }
    return out;
}
std::vector<double> smoothPower(const std::vector<double>& p, double b, int f) { return smoothImpl(p, b, f); }
std::vector<cpx> smoothComplex(const std::vector<cpx>& h, double b, int f) { return smoothImpl(h, b, f); }
}
