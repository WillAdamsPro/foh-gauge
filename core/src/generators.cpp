#include "dsp_safety.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
namespace foh {
ScopedFlushDenormals::ScopedFlushDenormals() noexcept {
#if defined(__aarch64__)
    uint64_t f; __asm__ __volatile__("mrs %0, fpcr" : "=r"(f)); saved = f;
    f |= (1ull << 24); __asm__ __volatile__("msr fpcr, %0" :: "r"(f)); // FZ
#elif defined(__x86_64__) || defined(_M_X64)
    unsigned m = _mm_getcsr(); saved = m; _mm_setcsr(m | 0x8040u);     // FTZ | DAZ
#endif
}
ScopedFlushDenormals::~ScopedFlushDenormals() noexcept {
#if defined(__aarch64__)
    uint64_t f = saved; __asm__ __volatile__("msr fpcr, %0" :: "r"(f));
#elif defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr((unsigned) saved);
#endif
}
float PinkNoise::white() noexcept { // xorshift32 -> [-1,1)
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (float) ((double) state / 2147483648.0 - 1.0);
}
static float safeGain(float db, float scale) noexcept {
    return std::isfinite(db) ? scale * std::pow(10.0f, std::clamp(db, -240.f, 60.f) / 20.f) : 0.f;
}
void PinkNoise::setGainDb(float db) noexcept { gain.store(safeGain(db, 0.1f), std::memory_order_relaxed); }
float PinkNoise::next() noexcept {
    float w = white();
    b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
    b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
    b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
    float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
    return p * gain.load(std::memory_order_relaxed);
}
void LogSweep::prepare(double sr, double a, double b, double s, double gap) {
    sweepLen = gapLen = pos = 0;
    resetReq.store(false, std::memory_order_relaxed);
    // Invalid preparation disables playback; validate before floating-to-integer casts.
    if (!std::isfinite(sr) || sr < 1 || !std::isfinite(a) || !std::isfinite(b)
        || !(a > 0) || !(b > a) || b > sr / 2 || !std::isfinite(s) || !(s > 0)
        || !std::isfinite(gap) || gap < 0) return;
    double len = s * sr, silence = gap * sr;
    if (!std::isfinite(len + silence) || len < 1
        || len + silence >= (double) std::numeric_limits<long>::max()) return;
    double logRatio = std::log(b) - std::log(a);
    double newK = s / logRatio, newPhK = 2.0 * M_PI * a * newK;
    if (!std::isfinite(newK) || !(newK > 0) || !std::isfinite(newPhK)) return;
    fs = sr; f1 = a; f2 = b; T = s; sweepLen = (long) len; gapLen = (long) silence;
    K = newK; phK = newPhK; fadeN = std::max(1.0, std::min(0.01 * fs, 0.5 * sweepLen));
}
void LogSweep::setGainDb(float db) noexcept { gain.store(safeGain(db, 0.25f), std::memory_order_relaxed); }
float LogSweep::next() noexcept {
    if (resetReq.load(std::memory_order_relaxed) && resetReq.exchange(false, std::memory_order_acquire)) pos = 0;
    if (sweepLen == 0) return 0;
    float out = 0;
    if (pos < sweepLen) {
        double t = pos / fs, ph = phK * (std::exp(t / K) - 1.0), env = 1.0;
        if (pos < fadeN) env = pos / fadeN; else if (pos > sweepLen - fadeN) env = (sweepLen - pos) / fadeN;
        out = std::isfinite(ph) ? (float) (std::sin(ph) * env) * gain.load(std::memory_order_relaxed) : 0.f;
    }
    if (++pos >= sweepLen + gapLen) pos = 0;
    return out;
}
static size_t roundPow2(int n) { size_t p = 1; while ((int) p < n) p <<= 1; return p; }
RingBuffer::RingBuffer(int cap) : buf(roundPow2(cap < 2 ? 2 : cap)), mask(buf.size() - 1) {}
bool RingBuffer::push(const float* d, int n) noexcept {
    if (n <= 0) return true;
    uint64_t wi = w.load(std::memory_order_relaxed), ri = r.load(std::memory_order_acquire);
    if ((wi - ri) + (uint64_t) n > buf.size()) { drops.fetch_add(1, std::memory_order_relaxed); return false; }
    size_t start = (size_t) (wi & mask), first = std::min((size_t) n, buf.size() - start);
    std::memcpy(buf.data() + start, d, first * sizeof(float));
    if (first < (size_t) n) std::memcpy(buf.data(), d + first, ((size_t) n - first) * sizeof(float));
    w.store(wi + (uint64_t) n, std::memory_order_release);
    return true;
}
int RingBuffer::available() const noexcept { return (int) (w.load(std::memory_order_acquire) - r.load(std::memory_order_relaxed)); }
int RingBuffer::pop(float* d, int n) noexcept {
    if (n <= 0) return 0;
    uint64_t ri = r.load(std::memory_order_relaxed); int a = available(); if (n > a) n = a;
    size_t start = (size_t) (ri & mask), first = std::min((size_t) n, buf.size() - start);
    std::memcpy(d, buf.data() + start, first * sizeof(float));
    if (first < (size_t) n) std::memcpy(d + first, buf.data(), ((size_t) n - first) * sizeof(float));
    r.store(ri + (uint64_t) n, std::memory_order_release); return n;
}
}
