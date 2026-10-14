#include "MainComponent.h"
using namespace juce;

PopupMenu MainComponent::getFileMenu(ApplicationCommandManager& commands) {
    PopupMenu menu;
    for (auto id : { openSessionCommand, saveSessionCommand, saveSessionAsCommand })
        menu.addCommandItem(&commands, id);
    return menu;
}
void MainComponent::getAllCommands(Array<CommandID>& commands) {
    for (auto id : { openSessionCommand, saveSessionCommand, saveSessionAsCommand })
        commands.add(id);
}
void MainComponent::getCommandInfo(CommandID id, ApplicationCommandInfo& info) {
    switch (id) {
        case openSessionCommand:
            info.setInfo("Open Session...", "Open a session folder", "File", 0);
            info.addDefaultKeypress('O', ModifierKeys::commandModifier);
            break;
        case saveSessionCommand:
            info.setInfo("Save Session", "Save the current session", "File", 0);
            info.addDefaultKeypress('S', ModifierKeys::commandModifier);
            break;
        case saveSessionAsCommand:
            info.setInfo("Save Session As...", "Save the session to a new folder", "File", 0);
            info.addDefaultKeypress('S', ModifierKeys::commandModifier | ModifierKeys::shiftModifier);
            break;
        default: break;
    }
}
bool MainComponent::perform(const InvocationInfo& info) {
    switch (info.commandID) {
        case openSessionCommand: openSession(); return true;
        case saveSessionCommand: saveSession(false); return true;
        case saveSessionAsCommand: saveSession(true); return true;
        default: return false;
    }
}
void MainComponent::saveSession(bool saveAs) {
    if (!saveAs && sessionFolder != File()) { writeSession(sessionFolder); return; }
    if (sessionChooser) return;
    sessionChooser = std::make_unique<FileChooser>("Save Session As", sessionFolder == File() ? File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Untitled.fohsession") : sessionFolder, "*.fohsession", true);
    sessionChooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
        [sp = SafePointer<MainComponent>(this)](const FileChooser& chooser) {
            if (!sp) return;
            auto file = chooser.getResult();
            if (file != File()) { if (!file.hasFileExtension("fohsession")) file = file.withFileExtension("fohsession"); sp->writeSession(file); }
            MessageManager::callAsync([sp] { if (sp) sp->sessionChooser.reset(); });
        });
}
void MainComponent::openSession() {
    if (sessionChooser) return;
    sessionChooser = std::make_unique<FileChooser>("Open Session Folder", sessionFolder, "*.fohsession", true);
    sessionChooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
        [sp = SafePointer<MainComponent>(this)](const FileChooser& chooser) {
            if (!sp) return;
            auto file = chooser.getResult(); if (file != File()) sp->readSession(file);
            MessageManager::callAsync([sp] { if (sp) sp->sessionChooser.reset(); });
        });
}
bool MainComponent::writeSession(const File& folder) {
    foh::Session s;
    const auto setup = model.dm.getAudioDeviceSetup();
    s.deviceType = model.dm.getCurrentAudioDeviceType().toStdString(); s.inputDevice = setup.inputDeviceName.toStdString(); s.outputDevice = setup.outputDeviceName.toStdString();
    s.sampleRate = setup.sampleRate > 0 ? setup.sampleRate : 48000; s.bufferSize = setup.bufferSize > 0 ? setup.bufferSize : 512;
    s.inputChannels = setup.inputChannels.toString(2).toStdString(); s.outputChannels = setup.outputChannels.toString(2).toStdString();
    s.mic = model.engine.micCh; s.reference = model.engine.refCh; s.outputA = model.engine.outA; s.outputB = model.engine.outB;
    s.micCalEnabled = model.calOn; s.micCalName = model.calFile.toStdString(); s.micCalText = model.calText;
    s.rigProfile = model.rigProfile;
    s.splCal = model.splCalJson(); s.targets = sessionTargets;
    for (int i = 0; i < model.traces.count(); ++i) s.captures.push_back(model.traces.at(i));
    view.storePanelStates();
    if (current >= 0) s.workspace = fohws::toJson(mgr.at(current));
    s.measurement.set("smoothing", model.smoothingFraction); s.measurement.set("averages", model.averages); s.measurement.set("rtaFraction", model.rtaFraction); s.measurement.set("blankThreshold", model.blankThreshold); s.measurement.set("refDelayMs", model.lockedDelayMs); s.measurement.set("showMode", showMode);
    std::string error;
    if (!foh::saveSession(folder.getFullPathName().toStdString(), s, error)) {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Session save failed", String(error)); return false;
    }
    sessionFolder = folder; return true;
}
bool MainComponent::readSession(const File& folder) {
    foh::Session s; std::string error; fohws::Workspace workspace;
    if (!foh::loadSession(folder.getFullPathName().toStdString(), s, error) || !fohws::fromJson(s.workspace, workspace, error)) {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Session open failed", String(error)); return false;
    }
    // Device enumeration and all file operations run on the message thread.
    AudioIODeviceType* type = nullptr;
    for (auto* candidate : model.dm.getAvailableDeviceTypes()) if (candidate->getTypeName().toStdString() == s.deviceType) type = candidate;
    std::vector<std::string> inputs, outputs;
    if (type) { type->scanForDevices(); for (auto name : type->getDeviceNames(true)) inputs.push_back(name.toStdString()); for (auto name : type->getDeviceNames(false)) outputs.push_back(name.toStdString()); }
    bool available = type && foh::sessionDevicesAvailable(s, type->getTypeName().toStdString(), inputs, outputs);
    // Opening a session always leaves the generator off, including a device fallback.
    model.engine.gen = AudioEngine::Off;
    model.cancelSessionOperations();
    if (available) {
        model.dm.setCurrentAudioDeviceType(String(s.deviceType), true);
        auto setup = model.dm.getAudioDeviceSetup();
        setup.inputDeviceName = String(s.inputDevice); setup.outputDeviceName = String(s.outputDevice); setup.sampleRate = s.sampleRate; setup.bufferSize = s.bufferSize;
        setup.useDefaultInputChannels = false; setup.useDefaultOutputChannels = false;
        setup.inputChannels.parseString(String(s.inputChannels), 2); setup.outputChannels.parseString(String(s.outputChannels), 2);
        auto deviceError = model.dm.setAudioDeviceSetup(setup, true); available = deviceError.isEmpty();
    }
    model.rigProfile = s.rigProfile;
    model.notify(AppModel::Rig);
    setShowMode(false); leaveWorkspace(); view.setWorkspace(nullptr);
    int index = mgr.indexOf(workspace.id);
    if (index < 0) index = mgr.saveAs(0, String(workspace.name));
    workspace.id = mgr.at(index).id; workspace.builtIn = mgr.at(index).builtIn;
    mgr.at(index) = std::move(workspace); current = -1; selectWorkspace(index);
    // Routing is session-wide. Prevent a later workspace switch from restoring
    // stale channel selections from that workspace's saved panel state.
    for (int i = 0; i < mgr.count(); ++i) {
        auto routing = foh::Json::object();
        routing.set("mic", s.mic); routing.set("ref", s.reference);
        routing.set("outA", s.outputA); routing.set("outB", s.outputB);
        mgr.at(i).panelState.set("InputsAndDevices", routing);
    }
    model.engine.micCh = s.mic; model.engine.refCh = s.reference; model.engine.outA = s.outputA; model.engine.outB = s.outputB;
    model.calText = s.micCalText; model.cal = foh::parseMicCal(s.micCalText); model.calFile = String(s.micCalName); model.setCalEnabled(s.micCalEnabled);
    model.traces.clear();
    for (const auto& t : s.captures) { model.traces.store(t.name, t.tf, t.refDelayMs); model.traces.at(model.traces.count() - 1).visible = t.visible; }
    model.loadSplCalJson(s.splCal.value_or(foh::Json::object()));
    if (auto* prefs = model.settings.getUserSettings()) { prefs->setValue("splCalibration", String(model.splCalJson().dump())); prefs->saveIfNeeded(); } sessionTargets = s.targets;
    model.setSmoothing(s.measurement["smoothing"].asInt(12)); model.setAverages(s.measurement["averages"].asInt(16)); model.setRtaFraction(s.measurement["rtaFraction"].asInt(3)); model.setBlankThreshold(s.measurement["blankThreshold"].asNumber(0.5));
    model.lockedDelayMs = s.measurement["refDelayMs"].asNumber(); model.send(foh::Command::referenceDelay(model.lockedDelayMs * model.engine.sampleRate.load() / 1000.0));
    model.send(foh::Command::resetAverages()); model.notify(AppModel::Traces | AppModel::Cal | AppModel::Settings);
    sessionFolder = folder;
    if (!available) {
        selectWorkspace("setup");
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Session audio setup needed", "The saved audio device is missing or its settings are unavailable. Choose an input and output in Setup before measuring.");
    } else if (s.measurement["showMode"].asBool()) setShowMode(true);
    return true;
}
