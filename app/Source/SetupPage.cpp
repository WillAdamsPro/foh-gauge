#include "SetupPage.h"
using namespace juce;

SetupPage::SetupPage(AppModel& m, bool showDone) : model(m) {
    title.setText("Audio Setup", dontSendNotification);
    title.setFont(FohLookAndFeel::font(20, true)); addAndMakeVisible(title);
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) addAndMakeVisible(box);
    deviceType.setTextWhenNothingSelected("Audio system"); input.setTextWhenNothingSelected("Input device");
    output.setTextWhenNothingSelected("Output device"); rate.setTextWhenNothingSelected("Sample rate");
    deviceType.onChange = [this] { if (!refreshing) model.dm.setCurrentAudioDeviceType(deviceType.getText(), true); };
    input.onChange = [this] { if (!refreshing) applyDevice(true); };
    for (auto* box : { &output, &rate }) box->onChange = [this] { if (!refreshing) applyDevice(); };
    auto routing = [this] {
        if (refreshing) return;
        model.engine.micCh = mic.getSelectedId() - 2; model.engine.refCh = ref.getSelectedId() - 2;
        model.engine.outA = generator.getSelectedId() - 2; model.engine.outB = loopback.getSelectedId() - 2;
        model.notify(AppModel::Settings);
    };
    for (auto* box : { &mic, &ref, &generator, &loopback }) box->onChange = routing;
    addAndMakeVisible(easySetup);
    easySetup.onClick = [this] { wizard.begin(); refresh(); wizard.setVisible(true); wizard.toFront(false); };
    addChildComponent(wizard);
    addAndMakeVisible(rigSetup); addChildComponent(questionnaire);
    rigSetup.onClick = [this] { questionnaire.begin(); questionnaire.setVisible(true); questionnaire.toFront(false); };
    questionnaire.onClose = [this] { questionnaire.setVisible(false); };
    wizard.onApplied = [this] { model.notify(AppModel::Settings); };
    wizard.onClose = [this] { wizard.setVisible(false); refresh(); };
    addAndMakeVisible(notice);
    loopbackInstructions.setText("Interface correction: disconnect the PA and mic (phantom off). Cable generator output to the mic input, and loopback output to reference input. Keep gains fixed; restore the mic after measuring.", dontSendNotification);
    loopbackInstructions.setJustificationType(Justification::topLeft);
    addAndMakeVisible(loopbackInstructions); addAndMakeVisible(loopbackStatus);
    addAndMakeVisible(measureLoopback); addAndMakeVisible(clearLoopback);
    measureLoopback.onClick = [this] {
        if (!model.beginLoopback()) notice.setText("Select two separate inputs and both outputs before measuring.", dontSendNotification);
    };
    clearLoopback.onClick = [this] { model.clearLoopback(); };
    splInstructions.setText("SPL calibration: fit a 1 kHz calibrator to the mic, or play a steady signal and enter its known level. Keep input gain unchanged after calibration.", dontSendNotification);
    splInstructions.setJustificationType(Justification::topLeft);
    addAndMakeVisible(splInstructions); addAndMakeVisible(splStatus);
    splReference.addItem("94 dB / 1 kHz", 1); splReference.addItem("114 dB / 1 kHz", 2); splReference.addItem("Known level", 3);
    splReference.setSelectedId(1); addAndMakeVisible(splReference);
    knownLevel.setText("94.0"); knownLevel.setInputRestrictions(7, "0123456789."); knownLevel.setReadOnly(true); addAndMakeVisible(knownLevel);
    splReference.onChange = [this] { knownLevel.setReadOnly(splReference.getSelectedId() != 3); if (splReference.getSelectedId() != 3) knownLevel.setText(splReference.getSelectedId() == 2 ? "114.0" : "94.0"); };
    addAndMakeVisible(calibrate); addAndMakeVisible(clearSpl);
    calibrate.onClick = [this] {
        if (!model.calibrateSpl(knownLevel.getText().getDoubleValue()))
            notice.setText("Calibration needs a steady, unclipped mic signal and a valid known level (0-160 dB).", dontSendNotification);
        else notice.setText("SPL calibration saved for this input. Keep the input gain unchanged.", dontSendNotification);
    };
    clearSpl.onClick = [this] { model.clearSpl(); };
    addAndMakeVisible(chime); chime.setTooltip("Short chime at -18 dBFS on the selected generator output");
    chime.onClick = [this] { model.engine.playChime(model.engine.outA.load()); };
    addChildComponent(done); done.setVisible(showDone); makePrimary(done);
    done.onClick = [this] {
        if (auto* prefs = model.settings.getUserSettings()) {
            if (auto state = model.dm.createStateXml()) prefs->setValue("audioDeviceState", state.get());
            prefs->setValue("micChannel", model.engine.micCh.load()); prefs->setValue("refChannel", model.engine.refCh.load());
            prefs->setValue("generatorChannel", model.engine.outA.load()); prefs->setValue("loopbackChannel", model.engine.outB.load());
            prefs->setValue("setupComplete", true); prefs->saveIfNeeded();
        }
        if (onDone) onDone();
    };
    model.dm.addChangeListener(this); model.addListener(this); refresh();
}
SetupPage::~SetupPage() { model.dm.removeChangeListener(this); model.removeListener(this); }
void SetupPage::refresh() {
    const ScopedValueSetter<bool> guard(refreshing, true);
    deviceType.clear(dontSendNotification);
    int id = 1; for (auto* type : model.dm.getAvailableDeviceTypes()) deviceType.addItem(type->getTypeName(), id++);
    deviceType.setText(model.dm.getCurrentAudioDeviceType(), dontSendNotification);
    auto setup = model.dm.getAudioDeviceSetup();
    auto* type = model.dm.getCurrentDeviceTypeObject();
    auto devices = [](ComboBox& box, const StringArray& names, const String& selected, const char* prefix) {
        box.clear(dontSendNotification); box.addItem(String(prefix) + ": None", 1);
        for (int i = 0; i < names.size(); ++i) box.addItem(String(prefix) + ": " + names[i], i + 2);
        box.setSelectedId(names.indexOf(selected) + 2, dontSendNotification);
    };
    devices(input, type ? type->getDeviceNames(true) : StringArray(), setup.inputDeviceName, "Input");
    devices(output, type ? type->getDeviceNames(false) : StringArray(), setup.outputDeviceName, "Output");
    rate.clear(dontSendNotification);
    auto* dev = model.dm.getCurrentAudioDevice();
    if (dev) { id = 1; for (auto fs : dev->getAvailableSampleRates()) { rate.addItem(String(fs, 0) + " Hz", id); if (std::abs(fs - setup.sampleRate) < 0.5) rate.setSelectedId(id, dontSendNotification); ++id; } }
    auto channels = [](ComboBox& box, const StringArray& names, int selected, const char* prefix) {
        box.clear(dontSendNotification); box.addItem(String(prefix) + ": None", 1);
        for (int i = 0; i < names.size(); ++i) box.addItem(String(prefix) + ": " + String(i + 1) + " (" + names[i] + ")", i + 2);
        box.setSelectedId(selected >= 0 && selected < names.size() ? selected + 2 : 1, dontSendNotification);
    };
    auto ins = dev ? dev->getInputChannelNames() : StringArray(); auto outs = dev ? dev->getOutputChannelNames() : StringArray();
    wizard.setChannels(ins, outs);
    wizard.setDevices(setup.inputDeviceName, setup.outputDeviceName);
    channels(mic, ins, model.engine.micCh, "Mic"); channels(ref, ins, model.engine.refCh, "Reference");
    channels(generator, outs, model.engine.outA, "Generator output"); channels(loopback, outs, model.engine.outB, "Loopback output");
    // Enable every physical channel so callback indices match the displayed channel numbers.
    if (dev && (setup.inputChannels.countNumberOfSetBits() != ins.size() || setup.outputChannels.countNumberOfSetBits() != outs.size())) {
        setup.useDefaultInputChannels = setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear(); setup.inputChannels.setRange(0, ins.size(), true);
        setup.outputChannels.clear(); setup.outputChannels.setRange(0, outs.size(), true);
        notice.setText(model.dm.setAudioDeviceSetup(setup, true), dontSendNotification);
    }
    chime.setEnabled(dev && outs.size() > 0);
    const bool hasLoopback = model.loopbackCals.count(model.loopbackKey()) != 0;
    loopbackStatus.setText(model.loopbackStatus.isNotEmpty() ? model.loopbackStatus
        : hasLoopback ? "Interface correction active for this routing." : "No interface correction for this routing.", dontSendNotification);
    measureLoopback.setEnabled(dev && !model.loopbackMeasuring); clearLoopback.setEnabled(hasLoopback && !model.loopbackMeasuring);
    auto c = model.splCal();
    splStatus.setText(c.calibrated ? "This input: dB SPL (offset " + String(c.offsetDb, 2) + " dB)" : "This input: uncalibrated, dBFS", dontSendNotification);
    calibrate.setEnabled(dev && model.engine.micCh.load() >= 0); clearSpl.setEnabled(c.calibrated);
}
void SetupPage::applyDevice(bool inputChanged) {
    auto setup = model.dm.getAudioDeviceSetup(); auto* type = model.dm.getCurrentDeviceTypeObject(); if (!type) return;
    auto name = [type](const ComboBox& box, bool isInput) { auto names = type->getDeviceNames(isInput); int i = box.getSelectedId() - 2; return i >= 0 && i < names.size() ? names[i] : String(); };
    setup.inputDeviceName = name(input, true); setup.outputDeviceName = name(output, false);
    // ASIO (and other unified backends) cannot open different input/output devices.
    // WASAPI retains independent endpoint choices. All changes stay on the UI thread.
    if (!type->hasSeparateInputsAndOutputs()) {
        const auto selected = inputChanged ? setup.inputDeviceName : setup.outputDeviceName;
        setup.inputDeviceName = setup.outputDeviceName = selected;
    }
    if (rate.getSelectedId() > 0) setup.sampleRate = rate.getText().getDoubleValue();
    notice.setText(model.dm.setAudioDeviceSetup(setup, true), dontSendNotification); refresh();
}
void SetupPage::resized() {
    wizard.setBounds(getLocalBounds()); questionnaire.setBounds(getLocalBounds());
    auto r = getLocalBounds().reduced(16); auto heading = r.removeFromTop(32); easySetup.setBounds(heading.removeFromRight(110)); title.setBounds(heading);
    rigSetup.setBounds(r.removeFromTop(30));
    auto buttons = r.removeFromBottom(36); done.setBounds(buttons.removeFromRight(90)); chime.setBounds(buttons.removeFromLeft(130));
    notice.setBounds(r.removeFromBottom(32));
    auto correction = r.removeFromBottom(122);
    loopbackInstructions.setBounds(correction.removeFromTop(60));
    loopbackStatus.setBounds(correction.removeFromTop(26));
    auto correctionButtons = correction.removeFromTop(32);
    measureLoopback.setBounds(correctionButtons.removeFromLeft(145)); correctionButtons.removeFromLeft(8);
    clearLoopback.setBounds(correctionButtons.removeFromLeft(145));
    auto spl = r.removeFromBottom(142);
    splInstructions.setBounds(spl.removeFromTop(48)); splStatus.setBounds(spl.removeFromTop(28));
    auto row = spl.removeFromTop(32); splReference.setBounds(row.removeFromLeft(155)); row.removeFromLeft(6);
    knownLevel.setBounds(row.removeFromLeft(65)); row.removeFromLeft(6); calibrate.setBounds(row.removeFromLeft(115)); row.removeFromLeft(6); clearSpl.setBounds(row.removeFromLeft(90));
    int h = jmin(40, r.getHeight() / 8);
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) box->setBounds(r.removeFromTop(h).reduced(0, 3));
}
