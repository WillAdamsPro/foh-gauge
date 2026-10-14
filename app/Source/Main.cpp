#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
class FOHGaugeApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "FOH Gauge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override { window.reset(new Window()); }
    void shutdown() override { window = nullptr; }
    struct Window : juce::DocumentWindow {
        Window() : DocumentWindow("FOH Gauge", foh::ui::col::background(), allButtons) {
            setUsingNativeTitleBar(true);
            auto* content = new MainComponent(); setContentOwned(content, true);
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(content);
           #else
            setMenuBar(content);
           #endif
            setResizable(true, true); setResizeLimits(1280, 800, 4000, 3000); centreWithSize(getWidth(), getHeight()); setVisible(true); }
        ~Window() override {
           #if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
           #endif
            setMenuBar(nullptr);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
START_JUCE_APPLICATION(FOHGaugeApp)
