#pragma once
#include "SqConnection.h"
#include <juce_gui_extra/juce_gui_extra.h>
namespace foh::sq {
class SqConsolePanel : public juce::Component, private juce::Timer {
public:
    SqConsolePanel();
    ~SqConsolePanel() override;
    void resized() override;
    void disconnect();
    foh::Json sessionState() const { return controller.sessionState(); }
    void restoreSession(const foh::Json&);
private:
    void timerCallback() override;
    void review(bool undo);
    void confirm();
    Strip strip() const;
    void refresh();
    SqController controller;
    std::unique_ptr<SqConnection> connection;
    juce::ToggleButton enabled{"Enable console connection (off by default)"}, dryRun{"Dry-run (no parameter writes)"}, delayIn{"Delay in"};
    juce::TextEditor host, frequency, gain, delay, proposed;
    juce::ComboBox target, band, mode;
    juce::TextButton connect{"Connect"}, preview{"Review corrections"}, apply{"Confirm batch"}, undo{"Undo"};
    juce::Label status, help;
    juce::ScopedMessageBox confirmation;
    bool wasConnected = false;
};
class SqConsoleWindow : public juce::DocumentWindow {
public:
    SqConsoleWindow();
    void closeButtonPressed() override;
    SqConsolePanel& panel() { return *static_cast<SqConsolePanel*>(getContentComponent()); }
};
}
