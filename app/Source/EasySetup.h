#pragma once
#include "AppModel.h"
#include "ScrollableForm.h"

// Optional rig questionnaire; all edits remain local until Use these answers.
class EasySetup : public juce::Component {
public:
    explicit EasySetup(AppModel&);
    void begin();
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onClose;
private:
    AppModel& model;
    ScrollableForm form;
    juce::Label title, instructions, clusterLabel, cabinetLabel, controlLabel, arrangementLabel, status;
    juce::TextEditor clusters, cabinets;
    juce::ComboBox control, arrangement;
    juce::ToggleButton fills { "Front fills" }, subs { "Subs" };
    juce::TextButton skip { "Skip" }, apply { "Use these answers" };
};
