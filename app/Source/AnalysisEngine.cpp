#include "AnalysisEngine.h"
#include "Widgets.h" // AudioEngine

void AnalysisEngine::setCal(std::shared_ptr<const foh::MicCal> c, const juce::String& name) {
    std::lock_guard<std::mutex> l(calLock); pendingCal = std::move(c); pendingCalName = name; calDirty = true;
}

void AnalysisEngine::rebuild(double newFs) {
    fs = newFs;
    tf = std::make_unique<foh::TransferFunction>(fftN, fs); rta = std::make_unique<foh::RTA>(fftN, fs, rtaFrac);
    tf->setAverages(averages); tf->setCoherenceBlankThreshold(blankThr);
    histRef.assign((size_t) (3 * fs), 0.f); histMic.assign(histRef.size(), 0.f); histPos = 0;
    if (cal) rta->setCalibrationDb(cal->correctionDb(fftN / 2 + 1, fs / fftN));
}

void AnalysisEngine::handle(const foh::Command& c) {
    using T = foh::CommandType;
    switch (c.type) {
        case T::SetSmoothing: smoothing = c.i; break;
        case T::SetRtaFraction: rtaFrac = c.i; rta->setFraction(c.i); break;
        case T::SetBlankThreshold: blankThr = c.d; tf->setCoherenceBlankThreshold(c.d); break;
        case T::SetAverages: averages = c.i; tf->setAverages(c.i); break;
        case T::ResetAverages: tf->reset(); rta->reset(); break;
        case T::SetReferenceDelay: tf->setReferenceDelaySamples((int) std::lround(c.d)); tf->reset(); break;
        case T::FindDelay: { // CommandQueue consumer: allocations / FFT stay on this worker.
            int L = std::min((int) histRef.size(), foh::recommendedDelayWindowSamples(fs, c.sub));
            std::vector<float> r((size_t) L), m((size_t) L); int H = (int) histRef.size();
            for (int i = 0; i < L; ++i) { int idx = (histPos - L + i + H) % H; r[(size_t) i] = histRef[(size_t) idx]; m[(size_t) i] = histMic[(size_t) idx]; }
            delay = foh::findDelay(r, m, fs, c.sub ? foh::DelayOptions::forSubs() : foh::DelayOptions{});
            delaySeq = c.id; delayWasSub = c.sub; break; }
    }
}

void AnalysisEngine::publish() {
    auto& s = publisher.beginWrite();
    s.tf = tf->result(smoothing); s.smoothingFraction = smoothing;
    if (cal) cal->apply(s.tf);
    s.rtaDb = rta->bandsDb(); s.rtaBands = rta->bands(); s.rtaFraction = rtaFrac;
    s.delay = delay; s.delaySeq = delaySeq; s.delayWasSub = delayWasSub;
    s.refDelaySamples = tf->referenceDelaySamples(); s.refDelayMs = s.refDelaySamples / fs * 1000.0;
    s.calApplied = cal != nullptr; s.setCalName(cal ? calName.c_str() : "");
    s.convergence = tf->convergence();
    publisher.publish();
}

void AnalysisEngine::run() {
    foh::ScopedFlushDenormals ftz;
    std::vector<float> m((size_t) fftN), r((size_t) fftN);
    auto lastPub = juce::Time::getMillisecondCounterHiRes();
    uint64_t epoch = 0;
    while (!threadShouldExit()) {
        const auto nextEpoch = engine.captureEpoch.load(std::memory_order_acquire);
        if (!tf || nextEpoch != epoch) {
            // Only the consumer advances the ring read positions. The producer
            // waits for analysisEpoch, so these drains cannot discard new-rate audio.
            for (auto* ring : { &engine.micRing, &engine.refRing })
                while (ring->available() > 0) ring->pop(m.data(), std::min(ring->available(), fftN));
            rebuild(engine.sampleRate.load()); // also clears both delay histories
            delay = {};
            epoch = nextEpoch;
            engine.analysisEpoch.store(epoch, std::memory_order_release);
        }
        { std::lock_guard<std::mutex> l(calLock);
          if (calDirty) { cal = pendingCal; calName = pendingCalName.toStdString(); calDirty = false;
                          rta->setCalibrationDb(cal ? cal->correctionDb(fftN / 2 + 1, fs / fftN) : std::vector<double>{}); } }
        foh::Command c; while (commands.pop(c)) handle(c);
        bool any = false;
        while (engine.captureEpoch.load(std::memory_order_acquire) == epoch
               && engine.micRing.available() >= 512 && engine.refRing.available() >= 512) {
            int n = std::min({ engine.micRing.available(), engine.refRing.available(), fftN });
            engine.micRing.pop(m.data(), n); engine.refRing.pop(r.data(), n); any = true;
            if (paused.load()) continue;
            tf->process(r.data(), m.data(), n); rta->process(m.data(), n);
            int H = (int) histRef.size();
            for (int i = 0; i < n; ++i) { histRef[(size_t) histPos] = r[(size_t) i]; histMic[(size_t) histPos] = m[(size_t) i]; histPos = (histPos + 1) % H; }
        }
        auto now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastPub >= 50) { publish(); lastPub = now; }
        if (!any) wait(5);
    }
}
