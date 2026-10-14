#include "SetupPage.h"
using namespace juce;

SetupPage::SetupPage(AppModel& m, bool showDone) : model(m) {
    title.setText("Audio Setup", dontSendNotification);
    title.setFont(FohLookAndFeel::font(20, true)); addAndMakeVisible(title);
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) addAndMakeVisible(box);
    deviceType.setTextWhenNothingSelected("Audio system"); input.setTextWhenNothingSelected("Input device");
    output.setTextWhenNothingSelected("Output device"); rate.setTextWhenNothingSelected("Sample rate");
    deviceType.onChange = [this] { if (!refreshing) model.dm.setCurrentAudioDeviceType(deviceType.getText(), true); };
    for (auto* box : { &input, &output, &rate }) box->onChange = [this] { if (!refreshing) applyDevice(); };
    auto routing = [this] {
        if (refreshing) return;
        model.engine.micCh = mic.getSelectedId() - 2; model.engine.refCh = ref.getSelectedId() - 2;
        model.engine.outA = generator.getSelectedId() - 2; model.engine.outB = loopback.getSelectedId() - 2;
        model.notify(AppModel::Settings);
    };
    for (auto* box : { &mic, &ref, &generator, &loopback }) box->onChange = routing;
    addAndMakeVisible(notice);
    addAndMakeVisible(chime); chime.setTooltip("Short chime at -18 dBFS on the selected generator and loopback outputs");
    chime.onClick = [this] { model.engine.playChime(); };
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
}
void SetupPage::applyDevice() {
    auto setup = model.dm.getAudioDeviceSetup(); auto* type = model.dm.getCurrentDeviceTypeObject(); if (!type) return;
    auto name = [type](const ComboBox& box, bool isInput) { auto names = type->getDeviceNames(isInput); int i = box.getSelectedId() - 2; return i >= 0 && i < names.size() ? names[i] : String(); };
    setup.inputDeviceName = name(input, true); setup.outputDeviceName = name(output, false);
    if (rate.getSelectedId() > 0) setup.sampleRate = rate.getText().getDoubleValue();
    notice.setText(model.dm.setAudioDeviceSetup(setup, true), dontSendNotification); refresh();
}
void SetupPage::resized() {
    auto r = getLocalBounds().reduced(16); title.setBounds(r.removeFromTop(32));
    auto buttons = r.removeFromBottom(36); done.setBounds(buttons.removeFromRight(90)); chime.setBounds(buttons.removeFromLeft(130));
    notice.setBounds(r.removeFromBottom(32));
    int h = jmin(40, r.getHeight() / 8);
    for (auto* box : { &deviceType, &input, &output, &rate, &mic, &ref, &generator, &loopback }) box->setBounds(r.removeFromTop(h).reduced(0, 3));
}
