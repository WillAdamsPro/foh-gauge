#pragma once
#include "AppModel.h"
#include "RoutingWizard.h"
#include "EasySetup.h"
#include "Panels.h"

// Device changes and preference writes happen only on the message thread.
class SetupPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener {
public:
    explicit SetupPage(AppModel&, bool showDone = true);
    ~SetupPage() override;
    void resized() override;
    void lookAndFeelChanged() override;
    void visibilityChanged() override { if (!isVisible()) { wizard.cancel(); questionnaire.cancel(); wizard.setVisible(false); questionnaire.setVisible(false); } }
    enum class Section { Audio, Mic, Reference, MicCalibration, ReferenceDelay };
    void focusSection(Section);
    Section focusedSection() const { return section; }
    void focusSplCalibration() { form.setViewPosition(0, splInstructions.getY()); splReference.grabKeyboardFocus(); }
    std::function<void()> onDone;
private:
    void modelChanged(int what) override {
        if (what & AppModel::Session) {
            wizard.cancel(); questionnaire.cancel();
            wizard.setVisible(false); questionnaire.setVisible(false);
        }
        if (what & (AppModel::Settings | AppModel::Cal | AppModel::Rig | AppModel::Session)) refresh();
    }
    void refresh();
    void applyDevice(bool inputChanged = false);
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    AppModel& model;
    ScrollableForm form;
    juce::Label title, notice, splInstructions, splStatus, loopbackInstructions, loopbackStatus, guideSummary;
    juce::TextButton measureLoopback { "Measure interface" }, clearLoopback { "Clear correction" };
    juce::ComboBox splReference; juce::TextEditor knownLevel;
    juce::TextButton calibrate { "Calibrate SPL" }, clearSpl { "Clear SPL" };
    juce::ComboBox deviceType, input, output, rate, mic, ref, generator, loopback;
    juce::TextButton chime { "Play chime" }, done { "Done" }, easySetup { "Easy Setup" };
    juce::TextButton rigSetup { "Rig questionnaire" };
    EasySetup questionnaire { model };
    RoutingWizard wizard { model.engine };
    Section section = Section::Audio;
    std::unique_ptr<Panel> detail;
    bool refreshing = false;
};
