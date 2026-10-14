#include "MainComponent.h"
using foh::RingBuffer;
using namespace juce;

namespace {
// C++17 equivalent of atomic::fetch_max (available in C++26). A CAS prevents
// the UI's exchange(0) from racing with a separate load/max/store sequence.
void fetch_max(std::atomic<float>& peak, float value) noexcept {
    float previous = peak.load(std::memory_order_relaxed);
    while (previous < value && !peak.compare_exchange_weak(previous, value,
            std::memory_order_relaxed, std::memory_order_relaxed)) {}
}
}

AudioEngine::AudioEngine() {
    float peak = 0;
    for (size_t i = 0; i < chime.size() - 1; ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        const double envelope = std::min(1.0, t / 0.008) * std::min(1.0, (0.6 - t) / 0.04);
        chime[i] = static_cast<float>(envelope * (std::sin(juce::MathConstants<double>::twoPi * 523.25 * t) * std::exp(-7 * t)
            + 0.45 * std::sin(juce::MathConstants<double>::twoPi * 659.25 * t) * std::exp(-9 * t)
            + 0.25 * std::sin(juce::MathConstants<double>::twoPi * 783.99 * t) * std::exp(-11 * t)));
        peak = std::max(peak, std::abs(chime[i]));
    }
    for (auto& sample : chime) sample *= std::pow(10.f, -18.f / 20.f) / peak;
}

// ===================== Audio engine (real-time thread) =====================
void AudioEngine::audioDeviceAboutToStart(AudioIODevice* d) {
    sampleRate = d->getCurrentSampleRate();
    sweep.prepare(sampleRate, 20.0, 20000.0, 5.0, 1.0);
    micPeak.store(0, std::memory_order_relaxed);
    refPeak.store(0, std::memory_order_relaxed);
    captureEpoch.fetch_add(1, std::memory_order_release);
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                                   const AudioIODeviceCallbackContext&) {
    const int selectedMic = micCh.load();
    if (selectedMic != lastMicChannel) { lastMicChannel = selectedMic; captureEpoch.fetch_add(1, std::memory_order_release); }
    const float g = genGainDb.load(); if (g != lastGain) { pink.setGainDb(g + 20.f); sweep.setGainDb(g + 12.f); lastGain = g; }
    const int mode = gen.load(), oa = outA.load(), ob = outB.load();
    const auto request = chimeRequest.load(std::memory_order_relaxed);
    if (request != lastChimeRequest) { lastChimeRequest = request; chimePosition = 0; }
    const double chimeStep = 48000.0 / sampleRate.load(std::memory_order_relaxed);
    for (int c = 0; c < nOut; ++c) if (out[c]) FloatVectorOperations::clear(out[c], n);
    for (int i = 0; i < n; ++i) {
        float s = mode == Pink ? pink.next() : mode == Sweep ? sweep.next() : 0.f;
        if (chimePosition < 28800) {
            const auto index = static_cast<size_t>(chimePosition);
            const float fraction = static_cast<float>(chimePosition - static_cast<double>(index));
            // Temporarily replace generator output so the chime keeps its -18 dBFS peak.
            s = chime[index] + fraction * (chime[index + 1] - chime[index]);
            chimePosition += chimeStep;
        }
        if (oa >= 0 && oa < nOut && out[oa]) out[oa][i] = s;
        if (ob >= 0 && ob < nOut && ob != oa && out[ob]) out[ob][i] = s;
    }
    if (analysisEpoch.load(std::memory_order_acquire) != captureEpoch.load(std::memory_order_acquire))
        return; // generator output continues while the worker resets capture
    const int mc = micCh.load(), rc = refCh.load();
    for (int off = 0; off < n; off += 4096) {
        int m = std::min(4096, n - off);
        const float* mp = (mc >= 0 && mc < nIn && in[mc]) ? in[mc] + off : nullptr;
        const float* rp = (rc >= 0 && rc < nIn && in[rc]) ? in[rc] + off : nullptr;
        if (mp) micRing.push(mp, m); else { std::fill(scratch, scratch + m, 0.f); micRing.push(scratch, m); }
        if (rp) refRing.push(rp, m); else { std::fill(scratch, scratch + m, 0.f); refRing.push(scratch, m); }
        auto peakOf = [m](const float* data) {
            auto range = FloatVectorOperations::findMinAndMax(data, m);
            return std::max(std::abs(range.getStart()), std::abs(range.getEnd()));
        };
        if (mp) fetch_max(micPeak, peakOf(mp));
        if (rp) fetch_max(refPeak, peakOf(rp));
    }
}

