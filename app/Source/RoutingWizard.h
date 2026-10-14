#pragma once
#include "Widgets.h"

// Optional message-thread walkthrough. Draft choices are applied only on Finish.
class RoutingWizard : public juce::Component, private juce::Timer {
public:
    explicit RoutingWizard(AudioEngine&);
    void setChannels(const juce::StringArray& inputs, const juce::StringArray& outputs);
    void begin();
    void cancel();
    void visibilityChanged() override;
    void parentHierarchyChanged() override { visibilityChanged(); }
    ~RoutingWizard() override;
    void setDevices(const juce::String& input, const juce::String& output);
    void resized() override;
    void paint(juce::Graphics&) override;
    std::function<void()> onApplied, onClose;
private:
    void refresh();
    void timerCallback() override;
    void cancelTest();
    void runTest();
    void advance();
    AudioEngine& engine;
    juce::StringArray inputNames, outputNames;
    // Mic input, reference input, loopback output, console output (physical indices).
    std::array<int, 4> draft {};
    int step = 0, testingStep = -1;
    juce::String inputDevice, outputDevice, suggestion;
    std::array<juce::String, 4> results;
    double testStartedMs = 0;
    bool refreshing = false, active = false, channelsKnown = false;
    juce::Label title, explanation, presetNotice, testStatus;
    juce::ComboBox channel;
    juce::TextButton back { "Back" }, next { "Next" }, skip { "Skip" };
    juce::TextButton test { "Test connection" };
    juce::Rectangle<int> diagram;
};
