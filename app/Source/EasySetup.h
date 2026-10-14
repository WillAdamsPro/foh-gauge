#pragma once
#include "AppModel.h"
#include "ScrollableForm.h"

// Optional rig questionnaire; all edits remain local until Use these answers.
class EasySetup : public juce::Component {
public:
    explicit EasySetup(AppModel&);
    void begin();
    void cancel() { active = false; }
    void visibilityChanged() override { if (!isVisible()) cancel(); }
    void parentHierarchyChanged() override { visibilityChanged(); }
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onClose;
private:
    bool active = false;
    AppModel& model;
    ScrollableForm form;
    void rebuildClusters();
    void updateGroups();
    juce::Label title, instructions, clusterLabel, cabinetLabel, controlLabel, arrangementLabel, status,
        fillLabel, subLabel, fillGroupLabel, subGroupLabel, placementLabel;
    std::vector<std::unique_ptr<juce::Slider>> clusterCounts;
    std::vector<std::unique_ptr<juce::Label>> clusterLabels;
    juce::Slider fillCount, subCount, fillGroups, subGroups;
    juce::TextEditor clusters, cabinets;
    juce::ComboBox control, arrangement, fillControl, subControl, placement;
    juce::TextButton match { "Apply default to all clusters" };
    juce::TextButton skip { "Skip" }, apply { "Use these answers" };
};
