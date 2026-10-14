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
    title.setFont(FohLookAndFeel::font(20, true));
    explanation.setJustificationType(Justification::topLeft);
    for (auto* label : { &title, &explanation }) addAndMakeVisible(label);
    addAndMakeVisible(channel);
    for (auto* button : { &back, &next, &skip }) addAndMakeVisible(button);
    channel.onChange = [this] { if (!refreshing) { draft[(size_t) step] = channel.getSelectedId() - 2; repaint(); } };
    back.onClick = [this] { if (step > 0) { --step; refresh(); } };
    next.onClick = [this] { advance(); };
    skip.onClick = [this] { if (onClose) onClose(); };
    begin();
}
void RoutingWizard::begin() {
    draft = { engine.micCh.load(), engine.refCh.load(), engine.outB.load(), engine.outA.load() };
    step = 0; refresh();
}
void RoutingWizard::setChannels(const StringArray& inputs, const StringArray& outputs) {
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
    explanation.setText(descriptions[step], dontSendNotification);
    channel.clear(dontSendNotification); channel.addItem("None (leave unassigned)", 1);
    const auto& names = step < 2 ? inputNames : outputNames;
    for (int i = 0; i < names.size(); ++i)
        channel.addItem(String(step < 2 ? "Input " : "Output ") + String(i + 1) + " (" + names[i] + ")", i + 2);
    channel.setSelectedId(draft[(size_t) step] + 2, dontSendNotification);
    back.setEnabled(step > 0); next.setButtonText(step == 3 ? "Finish" : "Next");
    repaint();
}
void RoutingWizard::advance() {
    if (step < 3) { ++step; refresh(); return; }
    engine.micCh = draft[0]; engine.refCh = draft[1]; engine.outB = draft[2]; engine.outA = draft[3];
    if (onApplied) onApplied();
    if (onClose) onClose();
}
void RoutingWizard::resized() {
    auto r = getLocalBounds().reduced(16);
    title.setBounds(r.removeFromTop(32));
    auto buttons = r.removeFromBottom(36);
    back.setBounds(buttons.removeFromLeft(70)); skip.setBounds(buttons.removeFromLeft(70).reduced(4, 0));
    next.setBounds(buttons.removeFromRight(80));
    channel.setBounds(r.removeFromBottom(40).reduced(0, 4));
    explanation.setBounds(r.removeFromTop(jmin(180, r.getHeight() / 2)));
    diagram = r.reduced(0, 8); repaint();
}
void RoutingWizard::paint(Graphics& g) {
    g.fillAll(findColour(juce::ResizableWindow::backgroundColourId));
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
