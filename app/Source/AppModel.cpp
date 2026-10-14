#include "AppModel.h"
#include <fohcore/input.h>
#include "FileValidation.h"
#include "PreferencesRecovery.h"
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
    if (prefs && fohapp::recoverAudioPreferences(*prefs))
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Audio preferences load failed", "The saved audio setup is incomplete or invalid. Choose devices and channels in Setup.");
    auto deviceText = prefs ? prefs->getValue("audioDeviceState").toStdString() : std::string{};
    auto savedDevice = fohapp::boundedXml(deviceText) ? juce::parseXML(String(deviceText)) : nullptr;
    auto deviceError = dm.initialise(4, 4, savedDevice.get(), savedDevice == nullptr);
    if (savedDevice && (!deviceError.isEmpty() || !dm.getCurrentAudioDevice()
        || dm.getCurrentAudioDeviceType() != savedDevice->getStringAttribute("deviceType"))) {
        dm.closeAudioDevice();
        if (prefs) {
            prefs->setValue("setupComplete", juce::var(false));
            for (const auto* key : {"micChannel", "refChannel", "generatorChannel", "loopbackChannel"}) prefs->setValue(key, -1);
        }
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Audio setup needed", "The saved device is unavailable. Choose devices and channels in Setup.");
    }
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
    if (prefs && prefs->containsKey("interfaceLoopback")) {
        if (!fohapp::loadLoopbackPreferences(prefs->getValue("interfaceLoopback").toStdString(), loopbackCals))
            AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Preferences load failed", "Invalid interface calibration preferences. Using no interface correction.");
    }
    dm.addAudioCallback(&engine);
    if (!dm.getCurrentAudioDevice()) engine.audioDeviceStopped();
    analysis.start();
    send(foh::Command::averages(averages)); send(foh::Command::smoothing(smoothingFraction)); send(foh::Command::blankThreshold(blankThreshold)); send(foh::Command::rtaFraction(rtaFraction));
    startTimerHz(15);
}
AppModel::~AppModel() { stopTimer(); if (loopbackMeasuring) finishLoopback(); dm.removeAudioCallback(&engine); analysis.stop(); if (auto* pf = settings.getUserSettings()) pf->saveIfNeeded(); }

void AppModel::requestDelay(bool sub, std::function<void()> then) {
    pendingDelay.id = ++nextDelayId; pendingDelay.then = std::move(then); delayPending = true; delayWasSub = sub;
    send(foh::Command::findDelay(sub, pendingDelay.id)); notify(Delay);
}
void AppModel::lockDelayFromLast() {
    if (!delayLocked()) return;
    analysis.send(foh::Command::referenceDelay(delay.arrivals[0].delaySamples), lastInputEpoch); lockedDelayMs = delay.arrivals[0].delayMs; notify(Delay);
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
bool AppModel::beginLoopback() {
    if (loopbackMeasuring || capture.active || !dm.getCurrentAudioDevice()
        || engine.micCh.load() < 0 || engine.refCh.load() < 0 || engine.micCh.load() == engine.refCh.load()
        || engine.outA.load() < 0 || engine.outB.load() < 0) return false;
    syncLoopback();
    if (!analysis.send(foh::Command::referenceDelay(0))) return false;
    savedReferenceDelay = refDelaySamples;
    loopbackResetId = ++nextCaptureId;
    if (!analysis.send(foh::Command::resetAverages(loopbackResetId))) {
        analysis.send(foh::Command::referenceDelay(savedReferenceDelay), activeLoopbackEpoch); return false;
    }
    savedGenerator = engine.gen.load(); savedGeneratorGain = engine.genGainDb.load(); savedPaused = paused;
    setPaused(false); engine.genGainDb = -20; engine.gen = AudioEngine::Pink;
    measuringLoopbackKey = loopbackKey(); loopbackStart = Time::getMillisecondCounterHiRes();
    loopbackMeasuring = true; loopbackStatus = "Measuring interface...";
    notify(Settings); return true;
}
void AppModel::clearLoopback() {
    if (loopbackMeasuring) finishLoopback();
    loopbackCals.erase(loopbackKey()); analysis.setLoopback(nullptr, engine.captureEpoch.load());
    auto j = foh::Json::object(); for (const auto& entry : loopbackCals) j.set(entry.first, entry.second.toJson());
    if (auto* p = settings.getUserSettings()) { p->setValue("interfaceLoopback", juce::String(j.dump())); p->saveIfNeeded(); }
    loopbackStatus = "Interface correction cleared."; notify(Settings);
}
