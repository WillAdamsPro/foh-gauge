#include "AppModel.h"
using namespace juce;

AppModel::AppModel() {
    PropertiesFile::Options o; o.applicationName = "FOH Gauge"; o.filenameSuffix = ".settings"; o.folderName = "FOH Gauge"; o.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters(o);
    auto* prefs = settings.getUserSettings();
    auto savedDevice = prefs ? prefs->getXmlValue("audioDeviceState") : nullptr;
    dm.initialise(4, 4, savedDevice.get(), true);
    if (!savedDevice) { auto s = dm.getAudioDeviceSetup(); s.sampleRate = 48000; dm.setAudioDeviceSetup(s, true); }
    if (prefs) {
        engine.micCh = prefs->getIntValue("micChannel", 0); engine.refCh = prefs->getIntValue("refChannel", 1);
        engine.outA = prefs->getIntValue("generatorChannel", 0); engine.outB = prefs->getIntValue("loopbackChannel", 1);
    }
    dm.addAudioCallback(&engine); analysis.start();
    send(foh::Command::averages(averages)); send(foh::Command::smoothing(smoothingFraction)); send(foh::Command::blankThreshold(blankThreshold)); send(foh::Command::rtaFraction(rtaFraction));
    startTimerHz(15);
}
AppModel::~AppModel() { stopTimer(); analysis.stop(); dm.removeAudioCallback(&engine); if (auto* pf = settings.getUserSettings()) pf->saveIfNeeded(); }

void AppModel::timerCallback() {
    int what = Levels; sampleRate = engine.sampleRate.load();
    { const auto& snap = analysis.latest(); // single reader thread (this timer); copy out what we keep
      if (snap.seq != lastSeq) { lastSeq = snap.seq; what |= Snapshot;
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
        double el = Time::getMillisecondCounterHiRes() - capture.startMs; bool timeout = el >= 10000;
        if ((capture.resetAcknowledged && conv.finished) || timeout) {
            const bool settled = capture.resetAcknowledged && conv.settled;
            capture.active = false; capture.unsettled = !settled;
            auto then = std::move(capture.then);
            const bool ok = storeTrace(capture.name);
            if (then) then(ok && settled);
        }
    }
    if (overlayBuilder && (what & (Snapshot | Delay | Traces))) { overlayBuilder(); what |= Overlay; }
    notify(what);
}
void AppModel::requestDelay(bool sub, std::function<void()> then) {
    pendingDelay.id = ++nextDelayId; pendingDelay.then = std::move(then); delayPending = true; delayWasSub = sub;
    send(foh::Command::findDelay(sub, pendingDelay.id)); notify(Delay);
}
void AppModel::lockDelayFromLast() {
    if (!delayLocked()) return;
    send(foh::Command::referenceDelay(delay.arrivals[0].delaySamples)); lockedDelayMs = delay.arrivals[0].delayMs; notify(Delay);
}
bool AppModel::storeTrace(const String& name) {
    bool ok = traces.store(name.toStdString(), tf, refDelayMs);
    if (!ok) AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Trace store full", "Up to 8 traces. Delete one in the Trace List first.");
    notify(Traces); return ok;
}
const foh::Trace* AppModel::findTrace(const char* n) const { for (int i = traces.count() - 1; i >= 0; --i) if (traces.at(i).name == n) return &traces.at(i); return nullptr; }
void AppModel::beginCapture(const String& name, std::function<void(bool)> then) {
    auto reset = foh::Command::resetAverages(++nextCaptureId);
    if (!analysis.send(reset)) { if (then) then(false); return; }
    capture.resetId = reset.id; capture.resetAcknowledged = false;
    capture.active = true; capture.unsettled = false; capture.name = name; capture.startMs = Time::getMillisecondCounterHiRes(); capture.then = std::move(then); conv = {}; notify(Capture);
}
void AppModel::loadCalFile(const File& f) {
    cal = foh::loadMicCal(f.getFullPathName().toStdString()); calFile = f.getFileName(); calOn = cal.ok();
    analysis.setCal(cal.ok() ? std::make_shared<foh::MicCal>(cal) : nullptr, calFile); notify(Cal);
}
void AppModel::setCalEnabled(bool on) {
    calOn = on && cal.ok(); analysis.setCal(calOn ? std::make_shared<foh::MicCal>(cal) : nullptr, calFile); notify(Cal);
}
