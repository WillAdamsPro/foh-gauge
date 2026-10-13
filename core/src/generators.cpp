#include "fohcore/fohcore.h"
#include <cmath>
namespace foh {
float PinkNoise::white() { // xorshift32 -> [-1,1)
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    return (float) ((double) state / 2147483648.0 - 1.0);
}
void PinkNoise::setGainDb(float db) { gain = 0.1f * std::pow(10.0f, db / 20.0f); }
float PinkNoise::next() {
    float w = white();
    b0 = 0.99886f*b0 + w*0.0555179f; b1 = 0.99332f*b1 + w*0.0750759f;
    b2 = 0.96900f*b2 + w*0.1538520f; b3 = 0.86650f*b3 + w*0.3104856f;
    b4 = 0.55000f*b4 + w*0.5329522f; b5 = -0.7616f*b5 - w*0.0168980f;
    float p = b0+b1+b2+b3+b4+b5+b6 + w*0.5362f; b6 = w*0.115926f;
    return p * gain;
}
void LogSweep::prepare(double sr, double a, double b, double s, double gap) {
    fs = sr; f1 = a; f2 = b; T = s; sweepLen = (long) (s * sr); gapLen = (long) (gap * sr); pos = 0;
}
void LogSweep::setGainDb(float db) { gain = 0.25f * std::pow(10.0f, db / 20.0f); }
float LogSweep::next() {
    float out = 0;
    if (pos < sweepLen) {
        double t = pos / fs, K = T / std::log(f2 / f1);
        double ph = 2.0 * M_PI * f1 * K * (std::exp(t / K) - 1.0);
        double fadeN = 0.01 * fs, env = 1.0;
        if (pos < fadeN) env = pos / fadeN; else if (pos > sweepLen - fadeN) env = (sweepLen - pos) / fadeN;
        out = (float) (std::sin(ph) * env) * gain;
    }
    if (++pos >= sweepLen + gapLen) pos = 0;
    return out;
}
RingBuffer::RingBuffer(int cap) : buf((size_t) cap), mask(cap - 1) {}
void RingBuffer::push(const float* d, int n) {
    uint64_t wi = w.load(std::memory_order_relaxed), ri = r.load(std::memory_order_acquire);
    if ((int) (wi - ri) + n > (int) buf.size()) return; // drop
    for (int i = 0; i < n; ++i) buf[(size_t) ((wi + (uint64_t) i) & (uint64_t) mask)] = d[i];
    w.store(wi + (uint64_t) n, std::memory_order_release);
}
int RingBuffer::available() const { return (int) (w.load(std::memory_order_acquire) - r.load(std::memory_order_relaxed)); }
int RingBuffer::pop(float* d, int n) {
    uint64_t ri = r.load(std::memory_order_relaxed); int a = available(); if (n > a) n = a;
    for (int i = 0; i < n; ++i) d[i] = buf[(size_t) ((ri + (uint64_t) i) & (uint64_t) mask)];
    r.store(ri + (uint64_t) n, std::memory_order_release); return n;
}
}
