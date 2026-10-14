#include "EasySetup.h"
using namespace juce;

EasySetup::EasySetup(AppModel& m) : model(m) {
    addAndMakeVisible(form);
    title.setText("Easy Setup (optional)", dontSendNotification);
    title.setFont(FohLookAndFeel::font(20, true));
    instructions.setText("Choose the W guides for your rig. Skip restores the default flow. Counts describe main speaker groups.", dontSendNotification);
    instructions.setJustificationType(Justification::topLeft);
    clusterLabel.setText("Main speaker clusters", dontSendNotification);
    cabinetLabel.setText("Cabinets per cluster", dontSendNotification);
    controlLabel.setText("Cabinet control", dontSendNotification);
    arrangementLabel.setText("Sub arrangement", dontSendNotification);
    for (auto* c : std::initializer_list<Component*>{&title, &instructions, &clusterLabel, &cabinetLabel, &controlLabel,
         &arrangementLabel, &status, &clusters, &cabinets, &control, &arrangement, &fills, &subs, &skip, &apply}) form.canvas.addAndMakeVisible(c);
    clusters.setInputRestrictions(3, "0123456789"); cabinets.setInputRestrictions(3, "0123456789");
    clusters.setName("Clusters"); cabinets.setName("Cabinets per cluster");
    control.addItem("Grouped control", 1); control.addItem("Individual control", 2);
    arrangement.addItem("Centre", 1); arrangement.addItem("Left / right", 2);
    arrangement.addItem("Distributed", 3); arrangement.addItem("Other", 4);
    subs.onClick = [this] { arrangement.setEnabled(subs.getToggleState()); };
    skip.onClick = [this] {
        if (!active) return;
        active = false; SafePointer<EasySetup> safe(this);
        model.rigProfile.reset(); model.notify(AppModel::Rig);
        if (safe) { auto close = safe->onClose; if (close) close(); }
    };
    apply.onClick = [this] {
        if (!active) return;
        foh::RigProfile r;
        r.clusters = clusters.getText().getIntValue(); r.cabinetsPerCluster = cabinets.getText().getIntValue();
        r.control = control.getSelectedId() == 2 ? foh::RigProfile::Control::Individual : foh::RigProfile::Control::Grouped;
        r.frontFills = fills.getToggleState(); r.subs = subs.getToggleState();
        r.subArrangement = static_cast<foh::RigProfile::SubArrangement>(arrangement.getSelectedId() - 1);
        if (!r.valid()) { status.setText("Enter cluster and cabinet counts from 1 to 256.", dontSendNotification); return; }
        // A model listener may close the host during the synchronous notification.
        active = false; SafePointer<EasySetup> safe(this);
        model.rigProfile = r; model.notify(AppModel::Rig);
        if (safe) { auto close = safe->onClose; if (close) close(); }
    };
    makePrimary(apply);
    begin();
    resized();
}
void EasySetup::begin() {
    active = true;
    auto r = model.rigProfile.value_or(foh::RigProfile{});
    clusters.setText(String(r.clusters)); cabinets.setText(String(r.cabinetsPerCluster));
    control.setSelectedId(r.control == foh::RigProfile::Control::Individual ? 2 : 1, dontSendNotification);
    fills.setToggleState(r.frontFills, dontSendNotification); subs.setToggleState(r.subs, dontSendNotification);
    arrangement.setSelectedId(static_cast<int>(r.subArrangement) + 1, dontSendNotification); arrangement.setEnabled(r.subs);
    status.setText({}, dontSendNotification);
    form.setViewPosition(0, 0);
}
void EasySetup::resized() {
    form.setBounds(getLocalBounds());
    form.sizeCanvas(form.layoutWidth(300), 580);
    auto r = form.canvas.getLocalBounds().reduced(16);
    title.setBounds(r.removeFromTop(32)); instructions.setBounds(r.removeFromTop(64));
    auto buttons = r.removeFromBottom(36); skip.setBounds(buttons.removeFromLeft(70)); buttons.removeFromLeft(8); apply.setBounds(buttons);
    status.setBounds(r.removeFromBottom(36));
    int h = jmin(56, r.getHeight() / 6);
    auto row = [&](Label& label, Component& choice) {
        auto b = r.removeFromTop(h); label.setBounds(b.removeFromTop(jmin(22, h / 2))); choice.setBounds(b.reduced(0, 2));
    };
    row(clusterLabel, clusters); row(cabinetLabel, cabinets); row(controlLabel, control);
    fills.setBounds(r.removeFromTop(h)); subs.setBounds(r.removeFromTop(h)); row(arrangementLabel, arrangement);
}

void EasySetup::paint(Graphics& g) { g.fillAll(findColour(ResizableWindow::backgroundColourId)); }
