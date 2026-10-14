#pragma once
#include "AppModel.h"

// Device changes and preference writes happen only on the message thread.
class SetupPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener {
public:
    explicit SetupPage(AppModel&, bool showDone = true);
    ~SetupPage() override;
    void resized() override;
    std::function<void()> onDone;
private:
    void modelChanged(int what) override { if (what & AppModel::Settings) refresh(); }
    void refresh();
    void applyDevice();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    AppModel& model;
    juce::Label title, notice;
    juce::ComboBox deviceType, input, output, rate, mic, ref, generator, loopback;
    juce::TextButton chime { "Play chime" }, done { "Done" };
    bool refreshing = false;
};
