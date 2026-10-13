#include "fohcore/fohcore.h"
#include <cmath>
namespace foh {
template <typename T>
static std::vector<T> smoothImpl(const std::vector<T>& v, double binHz, int fraction) {
    if (fraction <= 0 || v.size() < 3) return v;
    size_t n = v.size(); std::vector<T> pre(n + 1, T{}); // prefix sums
    for (size_t i = 0; i < n; ++i) pre[i + 1] = pre[i] + v[i];
    std::vector<T> out(n); double k = std::pow(2.0, 0.5 / fraction);
    out[0] = v[0];
    for (size_t i = 1; i < n; ++i) {
        double f = i * binHz;
        long lo = std::max(1L, (long) std::floor(f / k / binHz)), hi = std::min((long) n - 1, (long) std::ceil(f * k / binHz));
        out[i] = (pre[(size_t) hi + 1] - pre[(size_t) lo]) / (double) (hi - lo + 1);
    }
    return out;
}
std::vector<double> smoothPower(const std::vector<double>& p, double b, int f) { return smoothImpl(p, b, f); }
std::vector<cpx> smoothComplex(const std::vector<cpx>& h, double b, int f) { return smoothImpl(h, b, f); }
}
