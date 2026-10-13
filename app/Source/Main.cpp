#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
class FOHGaugeApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "FOH Gauge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window.reset(new Window()); }
    void shutdown() override { window = nullptr; }
    struct Window : juce::DocumentWindow {
        Window() : DocumentWindow("FOH Gauge", juce::Colour(0xff0e1013), allButtons) {
            setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true);
            setResizable(true, true); setResizeLimits(1280, 800, 4000, 3000); centreWithSize(1440, 900); setVisible(true); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(FOHGaugeApp)
