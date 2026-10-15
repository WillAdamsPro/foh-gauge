#include "SetupPage.h"
#include "Strings.h"
using namespace juce;

SetupPage::SetupPage(AppModel& m, bool showDone) : model(m) {
    addAndMakeVisible(form);
    title.setText("Audio Setup", dontSendNotification);
    title.setFont(FohLookAndFeel::font(foh::theme::type::heading, false, true)); form.canvas.addAndMakeVisible(title);
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) form.canvas.addAndMakeVisible(box);
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
    form.canvas.addAndMakeVisible(easySetup);
    easySetup.onClick = [this] { wizard.begin(); refresh(); wizard.setVisible(true); wizard.toFront(false); };
    addChildComponent(wizard);
    form.canvas.addAndMakeVisible(rigSetup); addChildComponent(questionnaire);
    rigSetup.onClick = [this] { questionnaire.begin(); questionnaire.setVisible(true); questionnaire.toFront(false); };
    questionnaire.onClose = [this] { questionnaire.setVisible(false); form.setViewPosition(0, 0); };
    wizard.onApplied = [this] { model.notify(AppModel::Settings); };
    wizard.onClose = [this] { wizard.setVisible(false); refresh(); };
    form.canvas.addAndMakeVisible(notice);
    loopbackInstructions.setText(String(foh::str::interfaceCorrection) +
        "\n1. Disconnect the PA and mic. Turn phantom power off."
        "\n2. Connect generator output to mic input, and loopback output to reference input."
        "\n3. Keep gains fixed and select Measure interface."
        "\n4. Restore the mic and PA connections after measuring.", dontSendNotification);
    loopbackInstructions.setJustificationType(Justification::topLeft);
    form.canvas.addAndMakeVisible(loopbackInstructions); form.canvas.addAndMakeVisible(loopbackStatus);
    form.canvas.addAndMakeVisible(measureLoopback); form.canvas.addAndMakeVisible(clearLoopback);
    measureLoopback.onClick = [this] {
        SafePointer<SetupPage> safe(this);
        model.confirmGeneratorStart("Pink noise", [safe] {
            if (safe && !safe->model.beginLoopback()) safe->notice.setText("Select two separate inputs and both outputs before measuring.", dontSendNotification);
        }, -20.f);
    };
    clearLoopback.onClick = [this] { model.clearLoopback(); };
    splInstructions.setText(String(foh::str::splCalibration) + ": fit a 1 kHz calibrator to the mic, or play a steady signal and enter its known level. Keep input gain unchanged after calibration.", dontSendNotification);
    splInstructions.setJustificationType(Justification::topLeft);
    form.canvas.addAndMakeVisible(splInstructions); form.canvas.addAndMakeVisible(splStatus);
    splReference.addItem("94 dB / 1 kHz", 1); splReference.addItem("114 dB / 1 kHz", 2); splReference.addItem("Known level", 3);
    splReference.setSelectedId(1); form.canvas.addAndMakeVisible(splReference);
    knownLevel.setFont(FohLookAndFeel::font(foh::theme::type::body));
    knownLevel.setText("94.0"); knownLevel.setInputRestrictions(7, "0123456789."); knownLevel.setReadOnly(true); form.canvas.addAndMakeVisible(knownLevel);
    splReference.onChange = [this] { knownLevel.setReadOnly(splReference.getSelectedId() != 3); if (splReference.getSelectedId() != 3) knownLevel.setText(splReference.getSelectedId() == 2 ? "114.0" : "94.0"); };
    form.canvas.addAndMakeVisible(calibrate); form.canvas.addAndMakeVisible(clearSpl);
    calibrate.onClick = [this] {
        if (!model.calibrateSpl(knownLevel.getText().getDoubleValue()))
            notice.setText("Calibration needs a steady, unclipped mic signal and a valid known level (0-160 dB).", dontSendNotification);
        else notice.setText("SPL calibration saved for this input. Keep the input gain unchanged.", dontSendNotification);
    };
    clearSpl.onClick = [this] { model.clearSpl(); };
    form.canvas.addAndMakeVisible(chime); chime.setTooltip("Short chime at -18 dBFS on the selected generator output");
    chime.onClick = [this] { model.engine.playChime(model.engine.outA.load()); };
    form.canvas.addChildComponent(done); done.setVisible(showDone); makePrimary(done);
    done.onClick = [this] {
        if (auto* prefs = model.settings.getUserSettings()) {
            if (auto state = model.dm.createStateXml()) prefs->setValue("audioDeviceState", state.get());
            prefs->setValue("micChannel", model.engine.micCh.load()); prefs->setValue("refChannel", model.engine.refCh.load());
            prefs->setValue("generatorChannel", model.engine.outA.load()); prefs->setValue("loopbackChannel", model.engine.outB.load());
            prefs->setValue("setupComplete", true); prefs->saveIfNeeded();
        }
        if (onDone) onDone();
    };
    guideSummary.setComponentID("setup-guide-summary");
    form.canvas.addAndMakeVisible(guideSummary);
    for (auto* label : {&notice, &splInstructions, &splStatus, &loopbackInstructions, &loopbackStatus, &guideSummary}) {
        label->setFont(FohLookAndFeel::font(foh::theme::type::body));
        label->setMinimumHorizontalScale(1.f);
        label->setJustificationType(Justification::topLeft);
    }
    model.dm.addChangeListener(this); model.addListener(this); refresh();
}
SetupPage::~SetupPage() { model.dm.removeChangeListener(this); model.removeListener(this); }
void SetupPage::refresh() {
    const ScopedValueSetter<bool> guard(refreshing, true);
    StringArray guides;
    if (model.rigProfile) {
        for (auto step : foh::rigSteps(model.rigProfile)) {
            switch (step) {
                case foh::RigStep::ReferenceDelay: guides.add("Reference Delay"); break;
                case foh::RigStep::SubToMains: guides.add("Sub to Mains"); break;
                case foh::RigStep::FillAlignment: guides.add("Fills to Mains"); break;
            }
        }
        guides.add("Tuning"); guides.add("Pre-Show Check"); guides.add("Polarity Check");
    }
    guideSummary.setText(guides.isEmpty() ? String() : "You'll see: " + guides.joinIntoString(", ") + ".", dontSendNotification);
    resized();
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
    wizard.setRigProfile(model.rigProfile);
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
void SetupPage::focusSection(Section next) {
    section = next;
    form.setViewPosition(0, 0);
    wizard.cancel(); questionnaire.cancel();
    wizard.setVisible(false); questionnaire.setVisible(false);
    detail.reset();
    if (next == Section::MicCalibration || next == Section::ReferenceDelay) {
        detail = PanelRegistry::standard().create(next == Section::MicCalibration ? "Calibration" : "DelayFinder", model);
        if (detail) { detail->setWantsKeyboardFocus(true); form.canvas.addAndMakeVisible(*detail); }
    }
    title.setText(detail ? detail->title() : "Audio Setup", dontSendNotification);
    resized();
    if (detail) detail->grabKeyboardFocus();
    else if (next == Section::Mic) { form.setViewPosition(0, mic.getY() - 16); mic.grabKeyboardFocus(); }
    else if (next == Section::Reference) { form.setViewPosition(0, ref.getY() - 16); ref.grabKeyboardFocus(); }
    else input.grabKeyboardFocus();
}
void SetupPage::lookAndFeelChanged() {
    knownLevel.applyColourToAllText(knownLevel.findColour(TextEditor::textColourId));
}
void SetupPage::resized() {
    form.setBounds(getLocalBounds());
    wizard.setBounds(getLocalBounds()); questionnaire.setBounds(getLocalBounds());
    for (auto* child : form.canvas.getChildren())
        if (child != &done)
            child->setVisible(!detail || child == detail.get() || child == &title);
    const int width = form.layoutWidth(480);
    auto textHeight = [width](const Label& label) {
        GlyphArrangement text;
        text.addJustifiedText(label.getFont(), label.getText(), 0, label.getFont().getAscent(),
                              (float)(width - 48), Justification::left);
        return jmax(28, ((int)std::ceil(text.getBoundingBox(0, -1, true).getHeight()) + 15) / 4 * 4);
    };
    if (detail) {
        form.sizeCanvas(width, 640);
        auto area = form.canvas.getLocalBounds().reduced(16);
        title.setBounds(area.removeFromTop(32));
        done.setBounds(area.removeFromBottom(28).removeFromRight(96));
        detail->setBounds(area);
        return;
    }
    const int summaryHeight = guideSummary.getText().isEmpty() ? 0 : textHeight(guideSummary) + 8;
    const int splHeight = textHeight(splInstructions), correctionHeight = textHeight(loopbackInstructions);
    form.sizeCanvas(width, 696 + summaryHeight + splHeight + correctionHeight);
    auto r = form.canvas.getLocalBounds().reduced(16);
    auto heading = r.removeFromTop(32);
    easySetup.setBounds(heading.removeFromRight(120)); heading.removeFromRight(8); title.setBounds(heading);
    r.removeFromTop(8); rigSetup.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
    guideSummary.setBounds(r.removeFromTop(summaryHeight));
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) {
        box->setBounds(r.removeFromTop(28)); r.removeFromTop(8);
    }
    r.removeFromTop(8);
    splInstructions.setBounds(r.removeFromTop(splHeight));
    splStatus.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
    auto row = r.removeFromTop(28);
    splReference.setBounds(row.removeFromLeft(160)); row.removeFromLeft(8);
    knownLevel.setBounds(row.removeFromLeft(72));
    r.removeFromTop(8); row = r.removeFromTop(28);
    calibrate.setBounds(row.removeFromLeft(128)); row.removeFromLeft(8); clearSpl.setBounds(row.removeFromLeft(112));
    r.removeFromTop(16);
    loopbackInstructions.setBounds(r.removeFromTop(correctionHeight));
    loopbackStatus.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
    row = r.removeFromTop(28);
    measureLoopback.setBounds(row.removeFromLeft(152)); row.removeFromLeft(8);
    clearLoopback.setBounds(row.removeFromLeft(152));
    r.removeFromTop(16); notice.setBounds(r.removeFromTop(48)); r.removeFromTop(8);
    row = r.removeFromTop(28);
    chime.setBounds(row.removeFromLeft(136)); done.setBounds(row.removeFromRight(96));
}
