#include "EasySetup.h"
using namespace juce;

EasySetup::EasySetup(AppModel& m) : model(m) {
    addAndMakeVisible(form);
    title.setText("Easy Setup (optional)", dontSendNotification);
    for (auto* label : {&instructions, &clusterLabel, &cabinetLabel, &controlLabel, &arrangementLabel,
                        &fillLabel, &subLabel, &fillGroupLabel, &subGroupLabel, &placementLabel}) {
        label->setFont(FohLookAndFeel::font(foh::theme::type::body));
        label->setMinimumHorizontalScale(1.f);
    }
    title.setFont(FohLookAndFeel::font(foh::theme::type::heading, false, true));
    instructions.setText("Tell us about your PA so we only show the guides you need.", dontSendNotification);
    instructions.setJustificationType(Justification::topLeft);
    clusterLabel.setText("Main speaker clusters", dontSendNotification);
    cabinetLabel.setText("Default cabinets for new clusters", dontSendNotification);
    controlLabel.setText("Main cabinet control", dontSendNotification);
    arrangementLabel.setText("Sub arrangement", dontSendNotification);
    fillLabel.setText("Front fills (0-64)", dontSendNotification);
    subLabel.setText("Subs (0-64)", dontSendNotification);
    fillGroupLabel.setText("Fill groups", dontSendNotification);
    subGroupLabel.setText("Sub groups", dontSendNotification);
    placementLabel.setText("Sub placement", dontSendNotification);
    for (auto* c : std::initializer_list<Component*>{&title, &instructions, &clusterLabel, &cabinetLabel, &controlLabel,
         &arrangementLabel, &clusters, &cabinets, &control, &arrangement, &fillLabel, &subLabel, &fillGroupLabel, &subGroupLabel, &placementLabel,
         &fillCount, &subCount, &fillGroups, &subGroups, &fillControl, &subControl, &placement, &match, &skip, &apply}) form.canvas.addAndMakeVisible(c);
    clusterHint.setText("1-64 groups of main speakers; usually 2 (left and right).", dontSendNotification);
    cabinetHint.setText("1-64 speakers per new cluster; usually 1. Adjust each below.", dontSendNotification);
    for (auto* hint : {&clusterHint, &cabinetHint}) {
        form.canvas.addAndMakeVisible(hint);
        hint->setFont(FohLookAndFeel::font(foh::theme::type::body));
        hint->setMinimumHorizontalScale(1.f);
        hint->setJustificationType(Justification::topLeft);
    }
    control.addItem("Grouped per cluster", 1); control.addItem("Individual cabinets", 2);
    arrangement.addItem("Center", 1); arrangement.addItem("Left / right", 2);
    arrangement.addItem("Distributed", 3); arrangement.addItem("Other", 4);
    arrangement.addItem("Cardioid", 5); arrangement.addItem("End-fire", 6);
    placement.addItem("Ground", 1); placement.addItem("Flown", 2);
    fillControl.addItem("Individual fills", 1); fillControl.addItem("Grouped fills", 2);
    subControl.addItem("Individual subs", 1); subControl.addItem("Grouped subs", 2);
    auto stepper = [](Slider& slider, const char* name, int low) {
        slider.setName(name); slider.setSliderStyle(Slider::IncDecButtons);
        slider.setTextBoxStyle(Slider::TextBoxLeft, false, 80, 28);
        slider.setRange(low, 64, 1);
    };
    stepper(clusters, "Clusters", 1); stepper(cabinets, "Default cabinets", 1);
    stepper(fillCount, "Front fill count", 0); stepper(subCount, "Sub count", 0);
    stepper(fillGroups, "Fill groups", 1); stepper(subGroups, "Sub groups", 1);
    fillCount.onValueChange = subCount.onValueChange = [this] { updateGroups(); };
    fillControl.onChange = subControl.onChange = [this] { updateGroups(); };
    clusters.onValueChange = [this] { rebuildClusters(); resized(); };
    match.onClick = [this] {
        const int n = (int)cabinets.getValue();
        for (auto& slider : clusterCounts) slider->setValue(n, dontSendNotification);
    };
    skip.onClick = [this] {
        if (!active) return;
        active = false; SafePointer<EasySetup> safe(this);
        model.rigProfile.reset(); model.notify(AppModel::Rig);
        if (safe) { auto close = safe->onClose; if (close) close(); }
    };
    apply.onClick = [this] {
        if (!active) return;
        foh::RigProfile r;
        rebuildClusters(); // Commit the current bounded count before taking the draft.
        r.clusterCabinets.clear();
        for (const auto& slider : clusterCounts) r.clusterCabinets.push_back((int)slider->getValue());
        r.control = control.getSelectedId() == 2 ? foh::RigProfile::Control::Individual : foh::RigProfile::Control::Grouped;
        r.frontFillCount = (int)fillCount.getValue(); r.subCount = (int)subCount.getValue();
        r.fillControl = fillControl.getSelectedId() == 1 ? foh::RigProfile::Control::Individual : foh::RigProfile::Control::Grouped;
        r.subControl = subControl.getSelectedId() == 1 ? foh::RigProfile::Control::Individual : foh::RigProfile::Control::Grouped;
        r.fillGroups = (int)fillGroups.getValue(); r.subGroups = (int)subGroups.getValue();
        r.subPlacement = static_cast<foh::RigProfile::SubPlacement>(placement.getSelectedId() - 1);
        r.subArrangement = static_cast<foh::RigProfile::SubArrangement>(arrangement.getSelectedId() - 1);
        if (!r.valid()) return;
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
    auto r = model.rigProfile && model.rigProfile->valid() ? *model.rigProfile : foh::RigProfile{};
    cabinets.setValue(r.clusterCabinets.front(), dontSendNotification);
    clusters.setValue((double)r.clusterCabinets.size(), dontSendNotification);
    clusterCounts.clear(); clusterLabels.clear(); rebuildClusters();
    for (size_t i = 0; i < clusterCounts.size(); ++i) clusterCounts[i]->setValue(r.clusterCabinets[i], dontSendNotification);
    control.setSelectedId(r.control == foh::RigProfile::Control::Individual ? 2 : 1, dontSendNotification);
    fillCount.setValue(r.frontFillCount, dontSendNotification); subCount.setValue(r.subCount, dontSendNotification);
    fillControl.setSelectedId(r.fillControl == foh::RigProfile::Control::Individual ? 1 : 2, dontSendNotification);
    subControl.setSelectedId(r.subControl == foh::RigProfile::Control::Individual ? 1 : 2, dontSendNotification);
    updateGroups();
    fillGroups.setValue(r.fillGroups, dontSendNotification); subGroups.setValue(r.subGroups, dontSendNotification);
    placement.setSelectedId(static_cast<int>(r.subPlacement) + 1, dontSendNotification);
    arrangement.setSelectedId(static_cast<int>(r.subArrangement) + 1, dontSendNotification);
    resized();
    form.setViewPosition(0, 0);
}
void EasySetup::rebuildClusters() {
    const int n = (int)clusters.getValue();
    while ((int)clusterCounts.size() > n) { clusterCounts.pop_back(); clusterLabels.pop_back(); }
    while ((int)clusterCounts.size() < n) {
        auto slider = std::make_unique<Slider>();
        form.canvas.addAndMakeVisible(*slider); // Inherit the theme before creating the text box.
        slider->setSliderStyle(Slider::IncDecButtons); slider->setTextBoxStyle(Slider::TextBoxLeft, false, 80, 28);
        slider->setRange(1, 64, 1); slider->setValue(cabinets.getValue(), dontSendNotification);
        clusterCounts.push_back(std::move(slider));
        auto label = std::make_unique<Label>(); label->setFont(FohLookAndFeel::font(foh::theme::type::body)); form.canvas.addAndMakeVisible(*label); clusterLabels.push_back(std::move(label));
    }
    foh::RigProfile names; names.clusterCabinets.resize((size_t)n);
    for (size_t i = 0; i < clusterCounts.size(); ++i) {
        auto name = String(names.clusterName(i));
        clusterLabels[i]->setText(name + " cabinets", dontSendNotification);
        clusterCounts[i]->setName(name + " cabinets");
    }
}
void EasySetup::updateGroups() {
    // JUCE requires a non-empty range even for absent/single-cabinet groups.
    auto groups = [](Slider& slider, int count, bool grouped) {
        slider.setRange(1, jmax(2, count), 1);
        slider.setValue(jlimit(1, jmax(1, count), (int)slider.getValue()), dontSendNotification);
        slider.setEnabled(count > 1 && grouped);
    };
    groups(fillGroups, (int)fillCount.getValue(), fillControl.getSelectedId() == 2);
    groups(subGroups, (int)subCount.getValue(), subControl.getSelectedId() == 2);
    fillControl.setEnabled(fillCount.getValue() > 0); subControl.setEnabled(subCount.getValue() > 0);
    arrangement.setEnabled(subCount.getValue() > 0); placement.setEnabled(subCount.getValue() > 0);
}
void EasySetup::resized() {
    form.setBounds(getLocalBounds());
    form.sizeCanvas(form.layoutWidth(400), 936 + (int)clusterCounts.size() * 40);
    auto r = form.canvas.getLocalBounds().reduced(16);
    title.setBounds(r.removeFromTop(32)); instructions.setBounds(r.removeFromTop(48));
    auto buttons = r.removeFromBottom(28); skip.setBounds(buttons.removeFromLeft(72)); buttons.removeFromLeft(8); apply.setBounds(buttons);

    auto row = [&](Label& label, Component& choice) {
        label.setBounds(r.removeFromTop(24)); choice.setBounds(r.removeFromTop(28)); r.removeFromTop(12);
    };
    row(clusterLabel, clusters); clusterHint.setBounds(r.removeFromTop(48));
    row(cabinetLabel, cabinets); cabinetHint.setBounds(r.removeFromTop(48));
    match.setBounds(r.removeFromTop(28)); r.removeFromTop(8);
    for (size_t i = 0; i < clusterCounts.size(); ++i) {
        auto b = r.removeFromTop(40).reduced(0, 4);
        clusterLabels[i]->setBounds(b.removeFromLeft(144)); b.removeFromLeft(8); clusterCounts[i]->setBounds(b);
    }
    row(controlLabel, control); row(fillLabel, fillCount);
    fillControl.setBounds(r.removeFromTop(28)); r.removeFromTop(8); row(fillGroupLabel, fillGroups);
    row(subLabel, subCount); subControl.setBounds(r.removeFromTop(28)); r.removeFromTop(8); row(subGroupLabel, subGroups);
    row(arrangementLabel, arrangement); row(placementLabel, placement);
}

void EasySetup::paint(Graphics& g) { g.fillAll(findColour(ResizableWindow::backgroundColourId)); }
