#include "MainComponent.h"
using foh::RingBuffer;
using namespace juce;

// ===================== Audio engine (real-time thread) =====================
void AudioEngine::audioDeviceAboutToStart(AudioIODevice* d) {
    sampleRate = d->getCurrentSampleRate();
    sweep.prepare(sampleRate, 20.0, 20000.0, 5.0, 1.0);
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                                   const AudioIODeviceCallbackContext&) {
    const float g = genGainDb.load(); if (g != lastGain) { pink.setGainDb(g + 20.f); sweep.setGainDb(g + 12.f); lastGain = g; }
    const int mode = gen.load(), oa = outA.load(), ob = outB.load();
    for (int c = 0; c < nOut; ++c) if (out[c]) FloatVectorOperations::clear(out[c], n);
    for (int i = 0; i < n; ++i) {
        float s = mode == Pink ? pink.next() : mode == Sweep ? sweep.next() : 0.f;
        if (oa >= 0 && oa < nOut && out[oa]) out[oa][i] = s;
        if (ob >= 0 && ob < nOut && ob != oa && out[ob]) out[ob][i] = s;
    }
    const int mc = micCh.load(), rc = refCh.load();
    for (int off = 0; off < n; off += 4096) {
        int m = std::min(4096, n - off);
        const float* mp = (mc < nIn && in[mc]) ? in[mc] + off : nullptr;
        const float* rp = (rc < nIn && in[rc]) ? in[rc] + off : nullptr;
        if (mp) micRing.push(mp, m); else { std::fill(scratch, scratch + m, 0.f); micRing.push(scratch, m); }
        if (rp) refRing.push(rp, m); else { std::fill(scratch, scratch + m, 0.f); refRing.push(scratch, m); }
        if (mp) micPeak.store(std::max(micPeak.load(), FloatVectorOperations::findMaximum(mp, m) ));
        if (rp) refPeak.store(std::max(refPeak.load(), FloatVectorOperations::findMaximum(rp, m) ));
    }
}

