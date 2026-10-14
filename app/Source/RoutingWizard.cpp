#include "RoutingWizard.h"
using namespace juce;

namespace {
const char* titles[] = { "1 of 4: Measurement mic", "2 of 4: Reference input", "3 of 4: Loopback cable", "4 of 4: Console / PA output" };
const char* descriptions[] = {
    "Connect your measurement mic to an interface input. It listens to the sound from your speakers. Choose that input below.",
    "Choose a different interface input for the reference cable. It receives a direct copy of the test sound so we can compare it with the mic.",
    "Connect an interface output directly to the reference input you just chose. This cable gives us the reference signal. Choose that output below.",
    "Connect a different interface output to a console line input or your PA input. It sends the test sound to the speakers. Keep the system level low when you first listen. Choose that output below."
};
String endpoint(const StringArray& names, int index, const char* direction) {
    return String(direction) + (index >= 0 && index < names.size() ? " " + String(index + 1) : " (None)");
}
}
RoutingWizard::RoutingWizard(AudioEngine& e) : engine(e) {
    addAndMakeVisible(form);
    form.canvas.onPaint = [this](Graphics& g) { paintDiagram(g); };
    title.setFont(FohLookAndFeel::font(20, true));
    explanation.setJustificationType(Justification::topLeft);
    for (auto* label : { &title, &explanation, &presetNotice, &testStatus }) form.canvas.addAndMakeVisible(label);
    form.canvas.addAndMakeVisible(channel);
    for (auto* button : { &back, &next, &skip, &test }) form.canvas.addAndMakeVisible(button);
    channel.onChange = [this] { if (!refreshing && active) { cancelTest(); draft[(size_t) step] = channel.getSelectedId() - 2; results.fill({}); refresh(); } };
    back.onClick = [this] { if (active && step > 0) { cancelTest(); --step; refresh(); } };
    next.onClick = [this] { advance(); };
    skip.onClick = [this] { if (!active) return; cancel(); auto close = onClose; if (close) close(); };
    test.onClick = [this] { runTest(); };
    begin();
}
RoutingWizard::~RoutingWizard() { cancelTest(); }
void RoutingWizard::cancel() { active = false; cancelTest(); }
void RoutingWizard::visibilityChanged() { if (!isVisible()) cancel(); }
void RoutingWizard::cancelTest() {
    stopTimer();
    if (testingStep >= 0) {
        engine.cancelConnectionTest();
        results[(size_t)testingStep] = "Test canceled. Test this connection again.";
    }
    testingStep = -1;
}
void RoutingWizard::setDevices(const String& input, const String& output) {
    if (input == inputDevice && output == outputDevice) return;
    const int interrupted = testingStep;
    cancelTest(); results.fill({});
    if (interrupted >= 0) results[(size_t)interrupted] = "Test failed: audio device changed. Check Audio Setup and test again.";
    inputDevice = input; outputDevice = output;
    suggestion = "Unknown interface: choose channels manually.";
    refresh();
}
void RoutingWizard::runTest() {
    if (!active || testingStep >= 0) return;
    cancelTest();
    const int expected = step == 0 || step == 3 ? draft[0] : draft[1];
    const int output = step == 0 || step == 3 ? draft[3] : draft[2];
    engine.testConnection(output, expected);
    testingStep = step; testStartedMs = Time::getMillisecondCounterHiRes();
    results[(size_t)step] = juce::String::fromUTF8("Testing connection\xe2\x80\xa6");
    refresh(); startTimer(50);
}
void RoutingWizard::timerCallback() {
    if (!active || testingStep < 0) { stopTimer(); return; }
    const auto result = engine.connectionResult();
    if (result == foh::ConnectionResult::pending) {
        if (Time::getMillisecondCounterHiRes() - testStartedMs < 2500) return;
        const int failedStep = testingStep;
        cancelTest();
        results[(size_t)failedStep] = "Test failed: audio device did not complete the test. Check Audio Setup.";
        refresh(); return;
    }
    results[(size_t)testingStep] = String(foh::connectionHint(result)).replace("Fail:", "Test failed:");
    stopTimer(); testingStep = -1; refresh();
}
void RoutingWizard::setRigProfile(const std::optional<foh::RigProfile>& profile) {
    const String nextGuidance = profile && profile->valid() ? String(profile->routingGuidance()) : String();
    if (nextGuidance != rigGuidance) { rigGuidance = nextGuidance; refresh(); }
}
void RoutingWizard::begin() {
    cancelTest(); results.fill({});
    draft = { engine.micCh.load(), engine.refCh.load(), engine.outB.load(), engine.outA.load() };
    suggestion = "Unknown interface: choose channels manually.";
    const auto* preset = foh::findInterfacePreset(inputDevice.toStdString());
    if (preset && preset == foh::findInterfacePreset(outputDevice.toStdString())
        && inputNames.size() > jmax(preset->micInput, preset->referenceInput)
        && outputNames.size() > jmax(preset->loopbackOutput, preset->consoleOutput)) {
        draft = { preset->micInput, preset->referenceInput, preset->loopbackOutput, preset->consoleOutput };
        suggestion = String(preset->name) + ": suggested channels below; change them if needed.";
    }
    active = true; step = 0; form.setViewPosition(0, 0);
    if (channelsKnown) setChannels(inputNames, outputNames); else refresh();
}
void RoutingWizard::setChannels(const StringArray& inputs, const StringArray& outputs) {
    channelsKnown = true;
    if (inputs != inputNames || outputs != outputNames) {
        const int interrupted = testingStep;
        cancelTest(); results.fill({});
        if (interrupted >= 0) results[(size_t)interrupted] = "Test failed: audio channels changed. Check Audio Setup and test again.";
    }
    inputNames = inputs; outputNames = outputs;
    for (size_t i = 0; i < draft.size(); ++i) {
        auto count = i < 2 ? inputNames.size() : outputNames.size();
        if (draft[i] < 0 || draft[i] >= count) draft[i] = -1;
    }
    refresh();
}
void RoutingWizard::refresh() {
    const ScopedValueSetter<bool> guard(refreshing, true);
    title.setText(titles[step], dontSendNotification);
    presetNotice.setText(suggestion, dontSendNotification);
    testStatus.setJustificationType(Justification::topLeft);
    const int testInput = step == 0 || step == 3 ? draft[0] : draft[1];
    const int testOutput = step == 0 || step == 3 ? draft[3] : draft[2];
    testStatus.setText(results[(size_t)step].isEmpty()
        ? "Test: " + endpoint(outputNames, testOutput, "Output") + " to " + endpoint(inputNames, testInput, "Input")
        : results[(size_t)step], dontSendNotification);
    test.setEnabled(testingStep < 0);
    test.setTooltip("Plays a chime on the chosen output, then checks the expected input. Keep speaker level low.");
    explanation.setText(String(descriptions[step]) + (step == 3 && rigGuidance.isNotEmpty() ? "\n" + rigGuidance : String()), dontSendNotification);
    channel.clear(dontSendNotification); channel.addItem("None (leave unassigned)", 1);
    const auto& names = step < 2 ? inputNames : outputNames;
    for (int i = 0; i < names.size(); ++i)
        channel.addItem(String(step < 2 ? "Input " : "Output ") + String(i + 1) + " (" + names[i] + ")", i + 2);
    channel.setSelectedId(draft[(size_t) step] + 2, dontSendNotification);
    back.setEnabled(step > 0); next.setButtonText(step == 3 ? "Finish" : "Next");
    resized(); repaint();
}
void RoutingWizard::advance() {
    if (!active) return;
    cancelTest();
    if (step < 3) { ++step; refresh(); return; }
    engine.micCh = draft[0]; engine.refCh = draft[1]; engine.outB = draft[2]; engine.outA = draft[3];
    active = false;
    SafePointer<RoutingWizard> safe(this);
    auto applied = onApplied; if (applied) applied();
    if (safe) { auto close = safe->onClose; if (close) close(); }
}
void RoutingWizard::resized() {
    form.setBounds(getLocalBounds());
    const int width = form.layoutWidth(300);
    GlyphArrangement text;
    text.addJustifiedText(explanation.getFont(), explanation.getText(), 0, explanation.getFont().getAscent(), (float)(width - 42), Justification::left);
    const int bodyH = jmax(120, (int)std::ceil(text.getBoundingBox(0, -1, true).getBottom()) + 12);
    form.sizeCanvas(width, jmax(520, bodyH + 320));
    auto r = form.canvas.getLocalBounds().reduced(16);
    title.setBounds(r.removeFromTop(32));
    auto buttons = r.removeFromBottom(36);
    back.setBounds(buttons.removeFromLeft(70)); skip.setBounds(buttons.removeFromLeft(70).reduced(4, 0));
    next.setBounds(buttons.removeFromRight(80));
    auto testRow = r.removeFromBottom(72);
    test.setBounds(testRow.removeFromTop(28).removeFromLeft(130));
    testStatus.setBounds(testRow);
    channel.setBounds(r.removeFromBottom(40).reduced(0, 4));
    presetNotice.setBounds(r.removeFromTop(36));
    explanation.setBounds(r.removeFromTop(bodyH));
    diagram = r.reduced(0, 8); form.canvas.repaint();
}
void RoutingWizard::paint(Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
}
void RoutingWizard::paintDiagram(Graphics& g) {
    if (diagram.isEmpty()) return;
    String from, to;
    switch (step) {
        case 0: from = "Measurement mic"; to = endpoint(inputNames, draft[0], "Interface input"); break;
        case 1: from = "Reference cable"; to = endpoint(inputNames, draft[1], "Interface input"); break;
        case 2: from = endpoint(outputNames, draft[2], "Interface output"); to = endpoint(inputNames, draft[1], "Reference input"); break;
        default: from = endpoint(outputNames, draft[3], "Interface output"); to = "Console / PA input"; break;
    }
    auto row = diagram.withSizeKeepingCentre(diagram.getWidth(), jmin(72, diagram.getHeight()));
    const int width = jmax(1, (row.getWidth() - 40) / 2);
    auto left = row.removeFromLeft(width).toFloat(); auto right = row.removeFromRight(width).toFloat();
    g.setColour(findColour(Label::textColourId));
    g.drawRoundedRectangle(left.reduced(1), 6, 1); g.drawRoundedRectangle(right.reduced(1), 6, 1);
    g.setFont(14); g.drawFittedText(from, left.toNearestInt().reduced(6), Justification::centred, 3);
    g.drawFittedText(to, right.toNearestInt().reduced(6), Justification::centred, 3);
    const float y = left.getCentreY(), end = right.getX() - 4;
    g.drawArrow(Line<float>(left.getRight() + 4, y, end, y), 2, 7, 7);
}
