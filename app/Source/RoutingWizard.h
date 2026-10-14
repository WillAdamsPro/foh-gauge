#pragma once
#include "Widgets.h"

// Optional message-thread walkthrough. Draft choices are applied only on Finish.
class RoutingWizard : public juce::Component {
public:
    explicit RoutingWizard(AudioEngine&);
    void setChannels(const juce::StringArray& inputs, const juce::StringArray& outputs);
    void begin();
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onApplied, onClose;
private:
    void refresh();
    void advance();
    AudioEngine& engine;
    juce::StringArray inputNames, outputNames;
    // Mic input, reference input, loopback output, console output (physical indices).
    std::array<int, 4> draft {};
    int step = 0;
    bool refreshing = false;
    juce::Label title, explanation;
    juce::ComboBox channel;
    juce::TextButton back { "Back" }, next { "Next" }, skip { "Skip" };
    juce::Rectangle<int> diagram;
};
