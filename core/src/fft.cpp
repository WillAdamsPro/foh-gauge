#include "fohcore/fohcore.h"
#include <cmath>
#include <stdexcept>
namespace foh {
bool isPowerOfTwo(int n) { return n > 0 && (n & (n - 1)) == 0; }
FFT::FFT(int size) : n(size) {
    if (!isPowerOfTwo(n) || n > (1 << 24)) throw std::invalid_argument("FFT size must be a power of two in [1, 2^24]");
    tw.resize((size_t) n / 2);
    for (int i = 0; i < n / 2; ++i) tw[(size_t) i] = std::polar(1.0, -2.0 * M_PI * i / n);
    rev.resize((size_t) n); int bits = 0; while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) { int r = 0; for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b); rev[(size_t) i] = r; }
}
void FFT::run(std::vector<cpx>& d, bool inv) const {
    if (d.size() != (size_t) n) throw std::invalid_argument("FFT data size mismatch");
    for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap(d[(size_t) i], d[(size_t) rev[(size_t) i]]);
    for (int len = 2; len <= n; len <<= 1) {
        int step = n / len, half = len / 2;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j) {
                cpx w = tw[(size_t) (j * step)]; if (inv) w = std::conj(w);
                cpx u = d[(size_t) (i + j)], v = d[(size_t) (i + j + half)] * w;
                d[(size_t) (i + j)] = u + v; d[(size_t) (i + j + half)] = u - v;
            }
    }
}
void FFT::forward(std::vector<cpx>& d) const { run(d, false); }
void FFT::inverse(std::vector<cpx>& d) const { run(d, true); for (auto& x : d) x /= (double) n; }
std::vector<double> hannWindow(int n) {
    if (n <= 0) return {};
    std::vector<double> w((size_t) n);
    for (int i = 0; i < n; ++i) w[(size_t) i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / n); // periodic
    return w;
}
}
