#include "Widgets.h"
#include <cstring>
using foh::RingBuffer;
using namespace juce;

namespace {
// C++17 equivalent of atomic::fetch_max (available in C++26). A CAS prevents
// the UI's exchange(0) from racing with a separate load/max/store sequence.
void fetch_max(std::atomic<float>& peak, float value) noexcept {
    float previous = peak.load(std::memory_order_relaxed);
    if (previous < value && !peak.compare_exchange_strong(previous, value,
            std::memory_order_relaxed, std::memory_order_relaxed)) {
        // Only the UI can contend, and it only writes zero. A second strong CAS
        // therefore succeeds (even if the UI exchanges zero again): bounded RT work.
        if (previous < value) peak.compare_exchange_strong(previous, value,
            std::memory_order_relaxed, std::memory_order_relaxed);
    }
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
    if (deviceFailed.load() || state.load() == Stopped) return foh::ConnectionResult::invalid;
    const auto request = chimeRequest.load(std::memory_order_acquire);
    const auto result = testResult.load(std::memory_order_acquire);
    return (request >> 32) == (result >> 32) ? static_cast<foh::ConnectionResult>(result & 0xff)
        : foh::ConnectionResult::pending;
}

// ===================== Audio engine (real-time thread) =====================
const char* AudioEngine::statusText() const noexcept {
    if (deviceFailed.load(std::memory_order_acquire)) return "Audio device disconnected.";
    switch (state.load(std::memory_order_acquire)) {
        case Stopped: return "Audio device disconnected.";
        case InvalidInputs: return "Choose available microphone and reference inputs.";
        case InvalidOutputs: return "Choose available generator outputs.";
        case Overflow: return "Audio dropped out; measuring restarted.";
        default: return analysisEpoch.load() == captureEpoch.load()
            ? "Measuring" : "Audio changed; measuring restarted.";
    }
}
void AudioEngine::resetPlayback() noexcept {
    gen.store(Off); sweepRequest.store(false); sweepPlaybackDone.store(false);
    sweepRemaining = 0; sweepLevel = 0; sweepRestart = false; sweepOneShot = false; previousGen = Off;
    lastChimeRequest = chimeRequest.load(std::memory_order_acquire);
    chimePosition = 28800; chimeOutput = -1; testSamplesRemaining = 0;
    connectionDetector.reset(-1);
    testResult.store((lastChimeRequest & 0xffffffff00000000ULL)
        | uint64_t(foh::ConnectionResult::invalid), std::memory_order_release);
}
void AudioEngine::audioDeviceError(const juce::String&) {
    // JUCE permits this on any thread: never touch generator/playback state,
    // allocate/copy the error string, or advance the capture handshake here.
    deviceFailed.store(true, std::memory_order_release);
    gen.store(Off);
    micPeak.store(0); refPeak.store(0);
}
void AudioEngine::audioDeviceStopped() {
    // JUCE serializes lifecycle callbacks with I/O; only atomics cross to UI/analysis.
    resetPlayback();
    state.store(Stopped, std::memory_order_release);
    micPeak.store(0); refPeak.store(0);
    captureEpoch.fetch_add(1, std::memory_order_release);
}
void AudioEngine::audioDeviceAboutToStart(AudioIODevice* d) {
    resetPlayback();
    const double rate = d ? d->getCurrentSampleRate() : 0;
    if (!std::isfinite(rate) || rate <= 0) { audioDeviceStopped(); return; }
    sampleRate.store(rate);
    sweep.prepare(rate, 20.0, std::min(20000.0, rate * 0.45), 5.0, 1.0);
    lastMicChannel = micCh.load(); lastRefChannel = refCh.load(); lastInputCount = lastOutputCount = -1;
    const auto inputs = d->getActiveInputChannels(), outputs = d->getActiveOutputChannels();
    lastInputsValid = lastMicChannel >= 0 && inputs[lastMicChannel]
        && lastRefChannel >= 0 && inputs[lastRefChannel];
    // A replacement can have fewer channels even with the generator stopped.
    // None is intentional for an output; a configured but missing output needs Setup.
    const int oa = outA.load(), ob = outB.load();
    lastOutputA = oa; lastOutputB = ob;
    lastOutputsValid = (oa < 0 || outputs[oa]) && (ob < 0 || outputs[ob]);
    captureMicChannel.store(lastMicChannel);
    deviceFailed.store(false, std::memory_order_release);
    state.store(!lastInputsValid ? InvalidInputs : !lastOutputsValid ? InvalidOutputs : Ready, std::memory_order_release);
    micPeak.store(0); refPeak.store(0);
    captureEpoch.fetch_add(1, std::memory_order_release);
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                                   const AudioIODeviceCallbackContext&) {
    foh::ScopedFlushDenormals ftz;
    if (n <= 0) return;
    for (int c = 0; c < nOut; ++c) if (out && out[c]) FloatVectorOperations::clear(out[c], n);
    if (deviceFailed.load(std::memory_order_acquire) || state.load(std::memory_order_acquire) == Stopped) return;
    const int mc = micCh.load(), rc = refCh.load();
    const bool inputsValid = in && mc >= 0 && mc < nIn && in[mc]
        && rc >= 0 && rc < nIn && in[rc];
    const int oa = outA.load(), ob = outB.load();
    const bool routingChanged = mc != lastMicChannel || rc != lastRefChannel
        || oa != lastOutputA || ob != lastOutputB
        || (lastInputCount >= 0 && nIn != lastInputCount)
        || (lastOutputCount >= 0 && nOut != lastOutputCount);
    // Never move an in-flight stimulus to another connection, or keep it playing
    // after a previously available measurement input disappears.
    if (routingChanged || (lastInputCount >= 0 && inputsValid != lastInputsValid)) resetPlayback();
    if (routingChanged || inputsValid != lastInputsValid) {
        captureMicChannel.store(mc);
        micPeak.store(0); refPeak.store(0);
        captureEpoch.fetch_add(1, std::memory_order_release);
    }
    lastMicChannel = mc; lastRefChannel = rc; lastInputCount = nIn; lastInputsValid = inputsValid;
    lastOutputA = oa; lastOutputB = ob;
    const float g = genGainDb.load(); if (g != lastGain) { pink.setGainDb(g + 20.f); sweep.setGainDb(g + 12.f); lastGain = g; }
    const bool silent = monitoringSilent.load(std::memory_order_acquire);
    // Discard playback, including the sweep ramp/tail and requests made while
    // monitoring. Merely skipping playback would let a tail resume on Stop.
    const bool resetRequested = playbackResetRequested.exchange(false, std::memory_order_acquire);
    if (silent || resetRequested) resetPlayback();
    if (sweepRequest.exchange(false, std::memory_order_acquire)) {
        sweepRestart = true; sweepOneShot = true;
    }
    const int mode = silent ? Off : gen.load();
    if (mode == Sweep && previousGen != Sweep) sweepRestart = true;
    previousGen = mode;
    const auto request = chimeRequest.load(std::memory_order_acquire);
    if (request != lastChimeRequest) {
        lastChimeRequest = request;
        chimeOutput = int(request & 0xffff) - 1;
        chimePosition = chimeOutput >= 0 ? 0 : 28800;
        const int expected = int((request >> 16) & 0xffff) - 1;
        connectionDetector.reset(expected);
        testSamplesRemaining = expected >= 0 ? int(sampleRate.load()) : 0;
    }
    if (silent) { chimePosition = 28800; testSamplesRemaining = 0; }
    const double rate = sampleRate.load(std::memory_order_relaxed);
    if (!std::isfinite(rate) || rate <= 0) return;
    const double chimeStep = 48000.0 / rate;
    const auto outputValid = [out, nOut](int c) { return out && c >= 0 && c < nOut && out[c]; };
    const bool outputsValid = (oa < 0 || outputValid(oa)) && (ob < 0 || outputValid(ob))
        && (mode == Off || outputValid(oa))
        && (chimePosition >= 28800 || outputValid(chimeOutput));
    if (outputsValid != lastOutputsValid && !routingChanged) {
        micPeak.store(0); refPeak.store(0);
        captureEpoch.fetch_add(1, std::memory_order_release);
    }
    lastOutputCount = nOut; lastOutputsValid = outputsValid;
    state.store(!inputsValid ? InvalidInputs : !outputsValid ? InvalidOutputs : Ready, std::memory_order_release);
    if (testSamplesRemaining > 0) {
        const int expected = int((request >> 16) & 0xffff) - 1;
        if (!in || expected < 0 || expected >= nIn || !in[expected]) { resetPlayback(); return; }
    }
    if (!outputsValid) {
        resetPlayback();
        return; // never publish a partial measurement or play through broken routing
    }
    const double rampStep = 1.0 / std::max(1.0, rate * 0.01);
    for (int i = 0; !silent && i < n; ++i) {
        const bool wantsSweep = mode == Sweep && gen.load() == Sweep;
        sweepLevel = wantsSweep && !sweepRestart ? std::min(1.0, sweepLevel + rampStep)
            : std::max(0.0, sweepLevel - rampStep);
        if (sweepRestart && sweepLevel == 0) {
            sweep.reset(); sweepRestart = false;
            // Stop at the sweep end, so the tail ramp runs in its silent gap
            // and cannot leak the beginning of the next looping sweep.
            sweepRemaining = wantsSweep && sweepOneShot ? int(rate * 5) : 0;
            sweepOneShot = false;
        }
        float s = mode == Pink ? pink.next() : 0.f;
        if (wantsSweep || sweepLevel > 0) s += sweep.next() * float(sweepLevel);
        if (sweepRemaining > 0) {
            if (mode != Sweep) sweepRemaining = 0;
            else if (--sweepRemaining == 0) {
                sweepPlaybackDone.store(true, std::memory_order_release);
                int expected = Sweep; gen.compare_exchange_strong(expected, Off);
            }
        }
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
            if (outputValid(chimeOutput)) out[chimeOutput][i] = s;
        } else if (!testing) {
            if (outputValid(oa)) out[oa][i] = s;
            if (ob != oa && outputValid(ob)) out[ob][i] = s;
        }
    }
    if (testSamplesRemaining > 0) {
        const int count = std::min(n, testSamplesRemaining);
        connectionDetector.process(in, nIn, count);
        testSamplesRemaining -= count;
        if (testSamplesRemaining == 0) {
            const auto result = outputValid(chimeOutput)
                ? connectionDetector.result() : foh::ConnectionResult::invalid;
            testResult.store((request & 0xffffffff00000000ULL) | uint64_t(result), std::memory_order_release);
        }
    }
    if (deviceFailed.load(std::memory_order_acquire)) return;
    if (analysisEpoch.load(std::memory_order_acquire) != captureEpoch.load(std::memory_order_acquire))
        return; // generator output continues while the worker resets capture
    if (!inputsValid) return;
    // Reserve both queues before publishing either channel. Their single consumer
    // can only free space; a failed reservation resets the entire stereo history.
    if (micRing.freeSpace() < n || refRing.freeSpace() < n) {
        state.store(Overflow, std::memory_order_release);
        captureEpoch.fetch_add(1, std::memory_order_release);
        return;
    }
    for (int off = 0; off < n; off += 4096) {
        int m = std::min(4096, n - off);
        const float* mp = in[mc] + off;
        const float* rp = in[rc] + off;
        bool faulty = false;
        auto peakOf = [m, &faulty](const float* data) {
            float peak = 0;
            for (int i = 0; i < m; ++i) {
                uint32_t bits;
                std::memcpy(&bits, data + i, sizeof(bits));
                const auto magnitude = bits & 0x7fffffffU;
                // Inspect bits because the callback flushes subnormals to zero.
                faulty |= magnitude >= 0x7f800000U || (magnitude > 0 && magnitude < 0x00800000U);
                if (std::isfinite(data[i])) peak = std::max(peak, std::abs(data[i]));
            }
            faulty |= peak <= 1.0e-12f || peak >= 0.99f;
            return peak;
        };
        fetch_max(micPeak, peakOf(mp));
        fetch_max(refPeak, peakOf(rp));
        if (faulty) signalFaults.fetch_add(1, std::memory_order_release);
        // Publish quality before samples can produce a settled analysis snapshot.
        micRing.push(mp, m); refRing.push(rp, m);
    }
    // An asynchronous error can arrive after the entry check or during a push.
    if (deviceFailed.load(std::memory_order_acquire)) { micPeak.store(0); refPeak.store(0); }
}

