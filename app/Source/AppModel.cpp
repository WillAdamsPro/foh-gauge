#include "AppModel.h"
#include <fohcore/input.h>
#include "FileValidation.h"
using namespace juce;

AppModel::AppModel() {
    PropertiesFile::Options o; o.applicationName = "FOH Gauge"; o.filenameSuffix = ".settings"; o.folderName = "FOH Gauge"; o.osxLibrarySubFolder = "Application Support";
    // Preserve rejected preferences for recovery; never let JUCE recursively
    // parse an oversized/deep XML tree or compressed arbitrary input.
    for (bool common : {false, true}) {
        auto options = o; options.commonToAllUsers = common;
        auto file = options.getDefaultFile();
        if (!file.exists()) continue;
        try {
            auto text = foh::input::read(file.getFullPathName().toStdString());
            if (!fohapp::boundedXml(text)) throw std::runtime_error("Invalid or oversized preferences XML");
            auto xml = juce::parseXML(String::fromUTF8(text.data(), (int) text.size()));
            if (!xml || !xml->hasTagName("PROPERTIES")) throw std::runtime_error("Invalid preferences XML");
        } catch (const std::exception& e) {
            auto backup = file.getParentDirectory().getNonexistentChildFile(file.getFileName() + ".invalid", "", false);
            if (!file.moveFileTo(backup)) {
                // A file we cannot quarantine must never reach the XML parser.
                o.applicationName = "FOH Gauge Recovery"; o.doNotSave = true;
            }
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Preferences load failed", String(e.what()) + ". Using defaults; the original file has been preserved.");
        }
    }
    settings.setStorageParameters(o);
    auto* prefs = settings.getUserSettings();
    auto deviceText = prefs ? prefs->getValue("audioDeviceState").toStdString() : std::string{};
    auto savedDevice = fohapp::boundedXml(deviceText) ? juce::parseXML(String(deviceText)) : nullptr;
    dm.initialise(4, 4, savedDevice.get(), true);
    if (!savedDevice) { auto s = dm.getAudioDeviceSetup(); s.sampleRate = 48000; dm.setAudioDeviceSetup(s, true); }
    if (prefs) {
        auto channel = [&](const char* key, int fallback) {
            double value;
            if (!prefs->containsKey(key)) return fallback;
            if (!foh::input::number(prefs->getValue(key).toStdString(), value) || value < -1 || value > 1023 || std::floor(value) != value) {
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Preferences load failed", String("Invalid channel preference: ") + key);
                return fallback;
            }
            return (int) value;
        };
        engine.micCh = channel("micChannel", 0); engine.refCh = channel("refChannel", 1);
        engine.outA = channel("generatorChannel", 0); engine.outB = channel("loopbackChannel", 1);
    }
    if (prefs && prefs->containsKey("splCalibration")) {
        std::string error; auto j = foh::Json::parse(prefs->getValue("splCalibration").toStdString(), &error);
        if (!error.empty() || !j.isObject()) AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Preferences load failed", "Invalid SPL calibration preferences");
        else loadSplCalJson(j);
    }
    if (prefs) loadSplCalJson(foh::Json::parse(prefs->getValue("splCalibration").toStdString()));
    if (prefs) {
        auto j = foh::Json::parse(prefs->getValue("interfaceLoopback").toStdString());
        for (const auto& entry : j.members()) {
            auto c = foh::LoopbackCorrection::fromJson(entry.second);
            if (c.valid()) loopbackCals[entry.first] = std::move(c);
        }
    }
    dm.addAudioCallback(&engine);
    if (!dm.getCurrentAudioDevice()) engine.audioDeviceStopped();
    analysis.start();
    send(foh::Command::averages(averages)); send(foh::Command::smoothing(smoothingFraction)); send(foh::Command::blankThreshold(blankThreshold)); send(foh::Command::rtaFraction(rtaFraction));
    startTimerHz(15);
}
AppModel::~AppModel() { stopTimer(); if (loopbackMeasuring) finishLoopback(); dm.removeAudioCallback(&engine); analysis.stop(); if (auto* pf = settings.getUserSettings()) pf->saveIfNeeded(); }

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
        tf = {}; rtaDb.clear(); rtaBands.clear(); conv = {}; micLevelValid = false;
        lockedDelayMs = 0; refDelaySamples = 0; refDelayMs = 0;
        what |= Snapshot | Delay | Capture | Settings;
        if (then) then(false); // never store a trace interrupted by a device/routing change
    }
    { const auto& snap = analysis.latest(); // single reader thread (this timer); copy out what we keep
      if (!deviceFailed && snap.seq != lastSeq && snap.inputEpoch == epoch) { lastSeq = snap.seq; what |= Snapshot;
          leq = snap.leq; leqRevision = snap.leqRevision;
          micLevelDbfs = snap.micLevelDbfs;
          levelInputKey = splInputKey(); levelInputEpoch = snap.inputEpoch;
          levelTimeMs = Time::getMillisecondCounterHiRes();
          micLevelValid = snap.micLevelValid && snap.inputEpoch == engine.captureEpoch.load()
              && snap.micChannel == engine.micCh.load() && engine.micCh.load() >= 0;
          sweep = snap.sweep; sweepCapturing = snap.sweepCapturing;
          if (loopbackMeasuring && snap.resetSeq == loopbackResetId && snap.refDelaySamples == 0 && snap.convergence.settled) finishLoopback(&snap.rawTf);
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
    try {
        auto text = foh::input::read(f.getFullPathName().toStdString());
        auto parsed = foh::parseMicCal(text);
        if (!parsed.ok()) throw std::runtime_error(parsed.error);
        auto published = std::make_shared<foh::MicCal>(parsed);
        calText = std::move(text); cal = std::move(parsed); calFile = f.getFileName(); calOn = true;
        analysis.setCal(std::move(published), calFile); ++analysis.leqRevision;
        notify(Cal);
    } catch (const std::exception& e) {
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Mic calibration import failed", String(e.what()));
    }
}
void AppModel::setCalEnabled(bool on) {
    calOn = on && cal.ok(); analysis.setCal(calOn ? std::make_shared<foh::MicCal>(cal) : nullptr, calFile); ++analysis.leqRevision;
    notify(Cal);
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
    ++analysis.leqRevision;
    notify(Cal | Settings); return true;
}
void AppModel::clearSpl() {
    splCals.erase(splInputKey());
    if (auto* p = settings.getUserSettings()) { p->setValue("splCalibration", juce::String(splCalJson().dump())); p->saveIfNeeded(); }
    ++analysis.leqRevision;
    notify(Cal | Settings);
}
foh::Json AppModel::splCalJson() const {
    auto j = foh::Json::object();
    for (const auto& entry : splCals) j.set(entry.first, entry.second.toJson());
    return j;
}
void AppModel::loadSplCalJson(const foh::Json& j) {
    if (!j.isObject()) return;
    decltype(splCals) candidate;
    for (const auto& entry : j.members()) {
        auto c = foh::SplCal::fromJson(entry.second);
        if (!c.calibrated) { AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "SPL calibration load failed", "Invalid saved calibration"); return; }
        candidate[entry.first] = c;
    }
    splCals = std::move(candidate);
    ++analysis.leqRevision;
    notify(Cal | Settings);
}

bool AppModel::micLevelReady() const {
    return micLevelValid && !paused && dm.getCurrentAudioDevice() && levelInputKey == splInputKey()
        && levelInputEpoch == engine.captureEpoch.load() && Time::getMillisecondCounterHiRes() - levelTimeMs < 250;
}

std::string AppModel::loopbackKey() const {
    auto setup = dm.getAudioDeviceSetup();
    auto j = foh::Json::array();
    j.push(dm.getCurrentAudioDeviceType().toStdString());
    j.push(setup.inputDeviceName.toStdString()); j.push(setup.outputDeviceName.toStdString());
    j.push(engine.sampleRate.load()); j.push(engine.micCh.load()); j.push(engine.refCh.load());
    j.push(engine.outA.load()); j.push(engine.outB.load());
    return j.dump(0);
}
void AppModel::syncLoopback() {
    auto key = loopbackKey(); auto epoch = engine.captureEpoch.load();
    if (key == activeLoopbackKey && epoch == activeLoopbackEpoch) return;
    if (loopbackMeasuring) finishLoopback();
    activeLoopbackKey = key; activeLoopbackEpoch = epoch; loopbackStatus.clear();
    auto it = loopbackCals.find(key);
    analysis.setLoopback(it == loopbackCals.end() ? nullptr : std::make_shared<foh::LoopbackCorrection>(it->second), epoch);
}
bool AppModel::beginLoopback() {
    if (loopbackMeasuring || capture.active || !dm.getCurrentAudioDevice()
        || engine.micCh.load() < 0 || engine.refCh.load() < 0 || engine.micCh.load() == engine.refCh.load()
        || engine.outA.load() < 0 || engine.outB.load() < 0) return false;
    syncLoopback();
    if (!analysis.send(foh::Command::referenceDelay(0))) return false;
    savedReferenceDelay = refDelaySamples;
    loopbackResetId = ++nextCaptureId;
    if (!analysis.send(foh::Command::resetAverages(loopbackResetId))) {
        send(foh::Command::referenceDelay(savedReferenceDelay)); return false;
    }
    savedGenerator = engine.gen.load(); savedGeneratorGain = engine.genGainDb.load(); savedPaused = paused;
    setPaused(false); engine.genGainDb = -20; engine.gen = AudioEngine::Pink;
    measuringLoopbackKey = loopbackKey(); loopbackStart = Time::getMillisecondCounterHiRes();
    loopbackMeasuring = true; loopbackStatus = "Measuring interface...";
    notify(Settings); return true;
}
void AppModel::finishLoopback(const foh::TFResult* raw) {
    if (!loopbackMeasuring) return;
    if (measuringLoopbackKey == loopbackKey() && activeLoopbackEpoch == engine.captureEpoch.load())
        send(foh::Command::referenceDelay(savedReferenceDelay));
    loopbackMeasuring = false; engine.gen = savedGenerator; engine.genGainDb = savedGeneratorGain; setPaused(savedPaused);
    auto c = raw ? foh::LoopbackCorrection::measure(*raw) : foh::LoopbackCorrection{};
    if (c.valid() && measuringLoopbackKey == loopbackKey()) {
        loopbackCals[measuringLoopbackKey] = c;
        analysis.setLoopback(std::make_shared<foh::LoopbackCorrection>(c), engine.captureEpoch.load());
        auto j = foh::Json::object(); for (const auto& entry : loopbackCals) j.set(entry.first, entry.second.toJson());
        if (auto* p = settings.getUserSettings()) { p->setValue("interfaceLoopback", juce::String(j.dump())); p->saveIfNeeded(); }
        loopbackStatus = "Interface correction saved (response + latency).";
    } else loopbackStatus = "Loopback failed: check cables, gain and broadband signal.";
    notify(Settings);
}
void AppModel::clearLoopback() {
    if (loopbackMeasuring) finishLoopback();
    loopbackCals.erase(loopbackKey()); analysis.setLoopback(nullptr, engine.captureEpoch.load());
    auto j = foh::Json::object(); for (const auto& entry : loopbackCals) j.set(entry.first, entry.second.toJson());
    if (auto* p = settings.getUserSettings()) { p->setValue("interfaceLoopback", juce::String(j.dump())); p->saveIfNeeded(); }
    loopbackStatus = "Interface correction cleared."; notify(Settings);
}
