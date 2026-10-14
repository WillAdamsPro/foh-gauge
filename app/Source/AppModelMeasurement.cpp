#include "AppModel.h"
using namespace juce;

void AppModel::timerCallback() {
    int what = Levels;
    const int generatorMode = engine.gen.load();
    if (generatorMode != lastGeneratorMode) { lastGeneratorMode = generatorMode; what |= Settings; }
    sampleRate = engine.sampleRate.load();
    const auto epoch = engine.captureEpoch.load(std::memory_order_acquire);
    const bool deviceFailed = engine.deviceFailed.load(std::memory_order_acquire);
    syncLoopback();
    if (loopbackMeasuring && (deviceFailed || measuringLoopbackKey != loopbackKey()
        || epoch != activeLoopbackEpoch || Time::getMillisecondCounterHiRes() - loopbackStart > 10000)) finishLoopback();
    if (epoch != lastInputEpoch || deviceFailed != lastDeviceFailed) {
        lastDeviceFailed = deviceFailed;
        lastInputEpoch = epoch;
        auto then = std::move(capture.then);
        cancelSessionOperations();
        sweep = {}; sweepCapturing = false;
        multiPosition = {}; positionCount = 0; positionMask = 0;
        leq = {}; micLevelDbfs = -120;
        engine.micPeak.store(0); engine.refPeak.store(0);
        tf = {}; rtaDb.clear(); rtaBands.clear(); conv = {}; micLevelValid = false;
        lockedDelayMs = 0; refDelaySamples = 0; refDelayMs = 0;
        what |= Snapshot | Delay | Capture | Settings;
        if (then) then(false); // never store a trace interrupted by a device/routing change
    }
    { const auto& snap = analysis.latest(); // single reader thread (this timer); copy out what we keep
      if (!deviceFailed && engine.state.load() == AudioEngine::Ready && snap.seq != lastSeq && snap.inputEpoch == epoch) { lastSeq = snap.seq; what |= Snapshot;
          leq = snap.leq; leqRevision = snap.leqRevision;
          micLevelDbfs = snap.micLevelDbfs;
          levelInputKey = splInputKey(); levelInputEpoch = snap.inputEpoch;
          levelTimeMs = Time::getMillisecondCounterHiRes();
          micLevelValid = snap.micLevelValid && snap.inputEpoch == engine.captureEpoch.load()
              && snap.micChannel == engine.micCh.load() && engine.micCh.load() >= 0;
          sweep = snap.sweep; sweepCapturing = snap.sweepCapturing;
          if (loopbackMeasuring && snap.resetSeq == loopbackResetId && snap.refDelaySamples == 0 && snap.convergence.settled) finishLoopback(&snap.rawTf);
          multiPosition = snap.multiPosition; positionCount = snap.positionCount; positionMask = snap.positionMask;
          tf = snap.tf; rtaDb = snap.rtaDb; rtaBands = snap.rtaBands; conv = snap.convergence;
          if (capture.active) {
              capture.resetAcknowledged = snap.resetSeq == capture.resetId;
              if (!capture.resetAcknowledged) conv = {}; // ignore the previous capture's settle state
          }
          refDelaySamples = snap.refDelaySamples; refDelayMs = snap.refDelayMs; calApplied = snap.calApplied; calName = String(CharPointer_UTF8(snap.calName));
          if (snap.delaySeq != lastDelaySeq) { lastDelaySeq = snap.delaySeq; delay = snap.delay; delayWasSub = snap.delayWasSub; delayPending = false; what |= Delay;
              if (lastDelaySeq == pendingDelay.id && pendingDelay.then) { auto t = std::move(pendingDelay.then); pendingDelay.then = {}; MessageManager::callAsync(t); } } } }
    micPeak = engine.micPeak.exchange(0); refPeak = engine.refPeak.exchange(0);
    if (capture.active) { what |= Capture;
        // Recheck at completion: a notification may arrive while copying the snapshot.
        if (engine.deviceFailed.load(std::memory_order_acquire) || engine.state.load() != AudioEngine::Ready
            || engine.captureEpoch.load(std::memory_order_acquire) != epoch) {
            auto then = std::move(capture.then);
            cancelSessionOperations();
            if (then) then(false);
        } else {
            double el = Time::getMillisecondCounterHiRes() - capture.startMs; bool timeout = el >= 10000;
            if ((capture.resetAcknowledged && conv.finished) || timeout) {
                const bool settled = capture.resetAcknowledged && conv.settled;
                capture.active = false; capture.unsettled = !settled;
                auto then = std::move(capture.then);
                const bool ok = storeTrace(capture.name);
                if (then) then(ok && settled);
            }
        }
    }
    if (overlayBuilder && (what & (Snapshot | Delay | Traces))) { overlayBuilder(); what |= Overlay; }
    notify(what);
}

std::string AppModel::splInputKey() const {
    auto setup = dm.getAudioDeviceSetup();
    return (dm.getCurrentAudioDeviceType() + "\n" + setup.inputDeviceName + "\n" + juce::String(engine.micCh.load())).toStdString();
}
