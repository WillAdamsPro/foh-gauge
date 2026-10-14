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

void AudioEngine::playChime(int output) noexcept {
    const auto channel = output >= 0 && output < 65534 ? uint64_t(output + 1) : 0;
    chimeRequest.store((uint64_t(++requestSequence) << 32) | channel, std::memory_order_release);
}
void AudioEngine::testConnection(int output, int expectedInput) noexcept {
    if (output < 0 || output >= 65534 || expectedInput < 0 || expectedInput >= foh::ConnectionDetector::maxInputs) {
        cancelConnectionTest();
        testResult.store((uint64_t(requestSequence) << 32) | uint64_t(foh::ConnectionResult::invalid), std::memory_order_release);
        return;
    }
    chimeRequest.store((uint64_t(++requestSequence) << 32) | (uint64_t(expectedInput + 1) << 16)
        | uint64_t(output + 1), std::memory_order_release);
}
void AudioEngine::cancelConnectionTest() noexcept {
    chimeRequest.store(uint64_t(++requestSequence) << 32, std::memory_order_release);
}
foh::ConnectionResult AudioEngine::connectionResult() const noexcept {
    const auto request = chimeRequest.load(std::memory_order_acquire);
    const auto result = testResult.load(std::memory_order_acquire);
    return (request >> 32) == (result >> 32) ? static_cast<foh::ConnectionResult>(result & 0xff)
        : foh::ConnectionResult::pending;
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
    const auto request = chimeRequest.load(std::memory_order_acquire);
    if (request != lastChimeRequest) {
        lastChimeRequest = request;
        chimeOutput = int(request & 0xffff) - 1;
        chimePosition = chimeOutput >= 0 ? 0 : 28800;
        const int expected = int((request >> 16) & 0xffff) - 1;
        connectionDetector.reset(expected);
        testSamplesRemaining = expected >= 0 ? int(sampleRate.load()) : 0;
    }
    const double chimeStep = 48000.0 / sampleRate.load(std::memory_order_relaxed);
    for (int c = 0; c < nOut; ++c) if (out[c]) FloatVectorOperations::clear(out[c], n);
    for (int i = 0; i < n; ++i) {
        float s = mode == Pink ? pink.next() : mode == Sweep ? sweep.next() : 0.f;
        const bool testing = testSamplesRemaining > 0;
        const bool playingChime = chimePosition < 28800;
        if (playingChime) {
            const auto index = static_cast<size_t>(chimePosition);
            const float fraction = static_cast<float>(chimePosition - static_cast<double>(index));
            // Temporarily replace generator output so the chime keeps its -18 dBFS peak.
            s = chime[index] + fraction * (chime[index + 1] - chime[index]);
            chimePosition += chimeStep;
        }
        if (playingChime) {
            if (chimeOutput >= 0 && chimeOutput < nOut && out[chimeOutput]) out[chimeOutput][i] = s;
        } else if (!testing) {
            if (oa >= 0 && oa < nOut && out[oa]) out[oa][i] = s;
            if (ob >= 0 && ob < nOut && ob != oa && out[ob]) out[ob][i] = s;
        }
    }
    if (testSamplesRemaining > 0) {
        const int count = std::min(n, testSamplesRemaining);
        connectionDetector.process(in, nIn, count);
        testSamplesRemaining -= count;
        if (testSamplesRemaining == 0) {
            const auto result = chimeOutput < nOut && out && out[chimeOutput]
                ? connectionDetector.result() : foh::ConnectionResult::invalid;
            testResult.store((request & 0xffffffff00000000ULL) | uint64_t(result), std::memory_order_release);
        }
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

