#include "AnalysisEngine.h"
#include "Widgets.h" // AudioEngine
#include <optional>

namespace {
bool deviceCommand(foh::CommandType type) {
    using T = foh::CommandType;
    switch (type) {
        case T::FindDelay: case T::ResetAverages: case T::SetReferenceDelay:
        case T::MeasureSweep: case T::StoreMicPosition: case T::ClearMicPositions: return true;
        default: return false; // presentation/averaging settings survive device rebuilds
    }
}
}

bool AnalysisEngine::send(const foh::Command& c) {
    return send(c, engine.captureEpoch.load(std::memory_order_acquire));
}

void AnalysisEngine::setCal(std::shared_ptr<const foh::MicCal> c, const juce::String& name) {
    std::lock_guard<std::mutex> l(calLock); pendingCal = std::move(c); pendingCalName = name; calDirty = true;
}

void AnalysisEngine::cancelSweep(const char* reason) {
    if (sweepCapturing) { int expected = AudioEngine::Sweep; engine.gen.compare_exchange_strong(expected, AudioEngine::Off); }
    sweepCapturing = false; sweepRef.clear(); sweepMic.clear();
    sweepResult = {}; sweepResult.response.error = reason;
}

uint64_t AnalysisEngine::setLoopback(std::shared_ptr<const foh::LoopbackCorrection> c, uint64_t epoch) {
    std::lock_guard<std::mutex> l(calLock);
    pendingLoopback = std::move(c); pendingLoopbackEpoch = epoch; loopbackDirty = true;
    return ++pendingLoopbackRevision;
}

void AnalysisEngine::rebuild(double newFs) {
    leq = std::make_unique<foh::LeqMeter>(newFs);
    positions = {}; positionMask = 0; positionAverage = {}; positionCount = 0;
    fs = newFs; levelEnergy = levelSum = 0; levelSamples = 0; levelInvalid = false; lastLevelUsable = lastTransferUsable = false;
    tf = std::make_unique<foh::TransferFunction>(fftN, fs); rta = std::make_unique<foh::RTA>(fftN, fs, rtaFrac);
    tf->setConvergenceTimeout(10.0);
    tf->setAverages(averages); tf->setCoherenceBlankThreshold(blankThr);
    histRef.assign((size_t) (3 * fs), 0.f); histMic.assign(histRef.size(), 0.f); histPos = 0;
    if (cal) rta->setCalibrationDb(cal->correctionDb(fftN / 2 + 1, fs / fftN));
}

void AnalysisEngine::handle(const foh::Command& c) {
    using T = foh::CommandType;
    switch (c.type) {
        case T::MeasureSweep: {
            if (paused.load() || engine.deviceFailed.load() || engine.state.load() != AudioEngine::Ready) {
                cancelSweep("Open Audio Setup and resume measurement first"); break;
            }
            if (sweepCapturing) break;
            sweepResult = {}; sweepRef.clear(); sweepMic.clear();
            sweepRef.reserve(size_t(fs * 6.5)); sweepMic.reserve(size_t(fs * 6.5));
            // Discard queued pre-sweep samples on the sole consumer, never on I/O.
            std::vector<float> discard(fftN);
            const int queued = std::min(engine.refRing.available(), engine.micRing.available());
            for (int left = queued; left > 0;) {
                int count = std::min(left, fftN);
                engine.refRing.pop(discard.data(), count); engine.micRing.pop(discard.data(), count); left -= count;
            }
            sweepCapturing = true; sweepStartMs = juce::Time::getMillisecondCounterHiRes();
            engine.startSweepMeasurement(); break;
        }
        case T::StoreMicPosition: {
            if (c.i < 0 || c.i >= 3 || !std::isfinite(c.d) || paused.load()
                || engine.deviceFailed.load() || !lastLevelUsable || !lastTransferUsable || tf->convergence().frames == 0) break;
            auto result = tf->result();
            if (cal) cal->apply(result);
            positions[(size_t)c.i] = {std::move(result), c.d - tf->referenceDelaySamples() / fs * 1000.0};
            positionMask |= uint8_t(1 << c.i);
            std::vector<foh::MicPosition> captured;
            for (size_t i = 0; i < positions.size(); ++i)
                if (positionMask & (1 << i)) captured.push_back(positions[i]);
            positionCount = (int)captured.size();
            positionAverage = foh::multiPositionAverage(captured);
            break;
        }
        case T::ClearMicPositions: positions = {}; positionMask = 0; positionAverage = {}; positionCount = 0; break;
        case T::SetSmoothing: smoothing = c.i; break;
        case T::SetRtaFraction: rtaFrac = c.i; rta->setFraction(c.i); break;
        case T::SetBlankThreshold: blankThr = c.d; tf->setCoherenceBlankThreshold(c.d); break;
        case T::SetAverages: averages = c.i; tf->setAverages(c.i); break;
        case T::ResetAverages: lastTransferUsable = false; tf->reset(); rta->reset(); resetSeq = c.id; break;
        case T::SetReferenceDelay: tf->setReferenceDelaySamples(c.d); tf->reset(); break;
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
    s.loopbackRevision = loopbackRevision;
    s.rawTf = tf->result(0);
    s.tf = tf->result(smoothing, loopbackEpoch == inputEpoch ? loopback.get() : nullptr);
    s.smoothingFraction = smoothing;
    s.positionMask = positionMask; s.positionCount = positionCount;
    s.multiPosition = positionAverage;
    if (cal) cal->apply(s.tf);
    s.rtaDb = rta->bandsDb(); s.rtaBands = rta->bands(); s.rtaFraction = rtaFrac;
    s.sweep = sweepResult; s.sweepCapturing = sweepCapturing;
    s.delay = delay; s.delaySeq = delaySeq; s.delayWasSub = delayWasSub;
    s.refDelaySamples = tf->referenceDelaySamples(); s.refDelayMs = s.refDelaySamples / fs * 1000.0;
    s.calApplied = cal != nullptr; s.setCalName(cal ? calName.c_str() : "");
    s.leq = leq->reading(); s.leqRevision = meterRevision;
    s.micLevelDbfs = levelSamples > 0 ? 10 * std::log10(2 * levelEnergy / levelSamples + 1e-20) : -120;
    const double meanSquare = levelSamples > 0 ? std::pow(levelSum / levelSamples, 2) : 0;
    const double variance = levelSamples > 0 ? levelEnergy / levelSamples - meanSquare : 0;
    if (levelSamples > 0) lastLevelUsable = !levelInvalid && variance > 1e-20 && meanSquare < variance;
    s.micLevelValid = lastLevelUsable && levelSamples > 0 && !paused.load() && !engine.deviceFailed.load()
        && engine.state.load() != AudioEngine::Stopped && engine.state.load() != AudioEngine::InvalidInputs;
    s.inputEpoch = inputEpoch; s.micChannel = micChannel;
    levelEnergy = levelSum = 0; levelSamples = 0; levelInvalid = false;
    s.resetSeq = resetSeq; s.convergence = tf->convergence();
    publisher.publish();
}

void AnalysisEngine::run() {
    foh::ScopedFlushDenormals ftz;
    std::vector<float> m((size_t) fftN), r((size_t) fftN), silence((size_t) fftN, 0.f);
    auto lastPub = juce::Time::getMillisecondCounterHiRes();
    uint64_t epoch = 0;
    std::optional<QueuedCommand> pendingCommand;
    while (!threadShouldExit()) {
        const auto nextEpoch = engine.captureEpoch.load(std::memory_order_acquire);
        if (!tf || nextEpoch != epoch) {
            // Only the consumer advances the ring read positions. The producer
            // waits for analysisEpoch, so these drains cannot discard new-rate audio.
            for (auto* ring : { &engine.micRing, &engine.refRing })
                while (ring->available() > 0) ring->pop(m.data(), std::min(ring->available(), fftN));
            if (tf) cancelSweep("Audio changed; measure the sweep again");
            rebuild(engine.sampleRate.load()); // also clears both delay histories
            delay = {}; delaySeq = 0; delayWasSub = false; resetSeq = 0;
            epoch = nextEpoch; inputEpoch = epoch; micChannel = engine.captureMicChannel.load();
            engine.analysisEpoch.store(epoch, std::memory_order_release);
        }
        { std::lock_guard<std::mutex> l(calLock);
          if (loopbackDirty) {
              loopback = pendingLoopback; loopbackEpoch = pendingLoopbackEpoch;
              loopbackRevision = pendingLoopbackRevision; loopbackDirty = false;
          }
          if (calDirty) { positions = {}; positionMask = 0; positionAverage = {}; positionCount = 0;
                          cal = pendingCal; calName = pendingCalName.toStdString(); calDirty = false;
                          rta->setCalibrationDb(cal ? cal->correctionDb(fftN / 2 + 1, fs / fftN) : std::vector<double>{}); } }
        while (engine.captureEpoch.load(std::memory_order_acquire) == epoch) {
            if (!pendingCommand) {
                QueuedCommand queued;
                if (!commands.pop(queued)) break;
                pendingCommand = queued;
            }
            // A rebuild may begin after the loop condition or the UI's push.
            // Keep the popped command until the worker catches up; never reset
            // the queue from its producer or discard a command for the new epoch.
            if (engine.captureEpoch.load(std::memory_order_acquire) != epoch) break;
            if (!deviceCommand(pendingCommand->command.type) || pendingCommand->epoch == epoch)
                handle(pendingCommand->command);
            pendingCommand.reset();
        }
        if (engine.captureEpoch.load(std::memory_order_acquire) != epoch) continue;
        const auto revision = leqRevision.load();
        if (revision != meterRevision) { leq->reset(); meterRevision = revision; }
        if (sweepCapturing && (paused.load() || engine.deviceFailed.load()
            || engine.state.load() != AudioEngine::Ready || engine.gen.load() == AudioEngine::Pink
            || (engine.gen.load() == AudioEngine::Off && !engine.sweepPlaybackDone.load(std::memory_order_acquire))
            || juce::Time::getMillisecondCounterHiRes() - sweepStartMs > 10000))
            cancelSweep("Sweep interrupted; check routing and measure again");
        bool any = false;
        while (engine.captureEpoch.load(std::memory_order_acquire) == epoch
               && engine.micRing.available() >= 512 && engine.refRing.available() >= 512) {
            int n = std::min({ engine.micRing.available(), engine.refRing.available(), fftN });
            engine.micRing.pop(m.data(), n); engine.refRing.pop(r.data(), n); any = true;
            if (paused.load()) continue;
            if (sweepCapturing) {
                const size_t count = std::min(size_t(n), size_t(fs * 6.5) - sweepRef.size());
                sweepRef.insert(sweepRef.end(), r.begin(), r.begin() + count);
                sweepMic.insert(sweepMic.end(), m.begin(), m.begin() + count);
                if (sweepRef.size() == size_t(fs * 6.5)) {
                    sweepCapturing = false;
                    sweepResult = foh::deconvolveSweep(sweepRef, sweepMic, fs);
                    sweepRef.clear(); sweepMic.clear();
                }
            }
            bool transferInvalid = false;
            double referenceEnergy = 0, measurementEnergy = 0;
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(r[i]) || std::abs(r[i]) >= 1
                    || !std::isfinite(m[i]) || std::abs(m[i]) >= 1) transferInvalid = true;
                if (std::isfinite(r[i])) referenceEnergy += double(r[i]) * r[i];
                if (std::isfinite(m[i])) measurementEnergy += double(m[i]) * m[i];
                if (!std::isfinite(m[i]) || std::abs(m[i]) >= 1) levelInvalid = true;
                else if (std::abs(m[i]) >= std::numeric_limits<float>::min()) {
                    levelEnergy += double(m[i]) * m[i]; levelSum += m[i];
                }
            }
            levelSamples += n;
            leq->process(m.data(), n);
            // Do not retain a coherent live TF across unusable input. W7 and
            // multi-mic capture must wait for fresh, valid analysis frames.
            const bool usable = !transferInvalid && referenceEnergy / n > 1e-20 && measurementEnergy / n > 1e-20;
            if (!usable && lastTransferUsable) tf->reset();
            lastTransferUsable = usable;
            if (usable) tf->process(r.data(), m.data(), n);
            else tf->process(silence.data(), silence.data(), n); // preserve the unsettled timeout

            rta->process(m.data(), n);
            int H = (int) histRef.size();
            for (int i = 0; i < n; ++i) { histRef[(size_t) histPos] = r[(size_t) i]; histMic[(size_t) histPos] = m[(size_t) i]; histPos = (histPos + 1) % H; }
        }
        auto now = juce::Time::getMillisecondCounterHiRes();
        // W7 needs only one snapshot per second; avoid rebuilding smoothed TF/RTA
        // vectors at the normal interactive rate while show monitoring is active.
        const double publishMs = engine.monitoringSilent.load() ? 1000.0 : 50.0;
        if (now - lastPub >= publishMs) { publish(); lastPub = now; }
        if (!any) wait(5);
    }
}
