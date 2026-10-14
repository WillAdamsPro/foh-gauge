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
    if (prefs) loadSplCalJson(foh::Json::parse(prefs->getValue("splCalibration").toStdString()));
    dm.addAudioCallback(&engine);
    if (!dm.getCurrentAudioDevice()) engine.audioDeviceStopped();
    analysis.start();
    send(foh::Command::averages(averages)); send(foh::Command::smoothing(smoothingFraction)); send(foh::Command::blankThreshold(blankThreshold)); send(foh::Command::rtaFraction(rtaFraction));
    startTimerHz(15);
}
AppModel::~AppModel() { stopTimer(); dm.removeAudioCallback(&engine); analysis.stop(); if (auto* pf = settings.getUserSettings()) pf->saveIfNeeded(); }

void AppModel::timerCallback() {
    int what = Levels; sampleRate = engine.sampleRate.load();
    const auto epoch = engine.captureEpoch.load(std::memory_order_acquire);
    const bool deviceFailed = engine.deviceFailed.load(std::memory_order_acquire);
    if (epoch != lastInputEpoch || deviceFailed != lastDeviceFailed) {
        lastDeviceFailed = deviceFailed;
        lastInputEpoch = epoch;
        auto then = std::move(capture.then);
        cancelSessionOperations();
        tf = {}; rtaDb.clear(); rtaBands.clear(); conv = {}; micLevelValid = false;
        lockedDelayMs = 0; refDelaySamples = 0; refDelayMs = 0;
        what |= Snapshot | Delay | Capture | Settings;
        if (then) then(false); // never store a trace interrupted by a device/routing change
    }
    { const auto& snap = analysis.latest(); // single reader thread (this timer); copy out what we keep
      if (!deviceFailed && snap.seq != lastSeq && snap.inputEpoch == epoch) { lastSeq = snap.seq; what |= Snapshot;
          micLevelDbfs = snap.micLevelDbfs;
          levelInputKey = splInputKey(); levelInputEpoch = snap.inputEpoch;
          levelTimeMs = Time::getMillisecondCounterHiRes();
          micLevelValid = snap.micLevelValid && snap.inputEpoch == engine.captureEpoch.load()
              && snap.micChannel == engine.micCh.load() && engine.micCh.load() >= 0;
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
    calText = f.loadFileAsString().toStdString();
    cal = foh::parseMicCal(calText); calFile = f.getFileName(); calOn = cal.ok();
    analysis.setCal(cal.ok() ? std::make_shared<foh::MicCal>(cal) : nullptr, calFile); notify(Cal);
}
void AppModel::setCalEnabled(bool on) {
    calOn = on && cal.ok(); analysis.setCal(calOn ? std::make_shared<foh::MicCal>(cal) : nullptr, calFile); notify(Cal);
}

std::string AppModel::splInputKey() const {
    auto setup = dm.getAudioDeviceSetup();
    return (dm.getCurrentAudioDeviceType() + "\n" + setup.inputDeviceName + "\n" + juce::String(engine.micCh.load())).toStdString();
}
foh::SplCal AppModel::splCal() const {
    auto it = splCals.find(splInputKey()); return it == splCals.end() ? foh::SplCal{} : it->second;
}
bool AppModel::calibrateSpl(double known) {
    if (!micLevelReady() || micPeak >= 0.99f || micLevelDbfs <= -100 || micLevelDbfs >= 0) return false;
    foh::SplCal c;
    if (!c.calibrate(micLevelDbfs - (calOn ? cal.dbAt(1000) : 0), known, splSensitivity())) return false;
    splCals[splInputKey()] = c;
    if (auto* p = settings.getUserSettings()) { p->setValue("splCalibration", juce::String(splCalJson().dump())); p->saveIfNeeded(); }
    notify(Cal | Settings); return true;
}
void AppModel::clearSpl() {
    splCals.erase(splInputKey());
    if (auto* p = settings.getUserSettings()) { p->setValue("splCalibration", juce::String(splCalJson().dump())); p->saveIfNeeded(); }
    notify(Cal | Settings);
}
foh::Json AppModel::splCalJson() const {
    auto j = foh::Json::object();
    for (const auto& entry : splCals) j.set(entry.first, entry.second.toJson());
    return j;
}
void AppModel::loadSplCalJson(const foh::Json& j) {
    splCals.clear();
    for (const auto& entry : j.members()) {
        auto c = foh::SplCal::fromJson(entry.second);
        if (c.calibrated) splCals[entry.first] = c;
    }
    notify(Cal | Settings);
}

bool AppModel::micLevelReady() const {
    return micLevelValid && !paused && dm.getCurrentAudioDevice() && levelInputKey == splInputKey()
        && levelInputEpoch == engine.captureEpoch.load() && Time::getMillisecondCounterHiRes() - levelTimeMs < 250;
}
