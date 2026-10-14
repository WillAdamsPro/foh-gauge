#pragma once
#include "AppModel.h"
#include "RoutingWizard.h"
#include "EasySetup.h"

// Device changes and preference writes happen only on the message thread.
class SetupPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener {
public:
    explicit SetupPage(AppModel&, bool showDone = true);
    ~SetupPage() override;
    void resized() override;
    void focusSplCalibration() { splReference.grabKeyboardFocus(); }
    std::function<void()> onDone;
private:
    void modelChanged(int what) override { if (what & (AppModel::Settings | AppModel::Cal)) refresh(); }
    void refresh();
    void applyDevice();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    AppModel& model;
    juce::Label title, notice, splInstructions, splStatus, loopbackInstructions, loopbackStatus;
    juce::TextButton measureLoopback { "Measure interface" }, clearLoopback { "Clear correction" };
    juce::ComboBox splReference; juce::TextEditor knownLevel;
    juce::TextButton calibrate { "Calibrate SPL" }, clearSpl { "Clear SPL" };
    juce::ComboBox deviceType, input, output, rate, mic, ref, generator, loopback;
    juce::TextButton chime { "Play chime" }, done { "Done" }, easySetup { "Easy Setup" };
    juce::TextButton rigSetup { "Rig questionnaire" };
    EasySetup questionnaire { model };
    RoutingWizard wizard { model.engine };
    bool refreshing = false;
};
