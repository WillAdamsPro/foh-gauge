#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#if JUCE_MAC
 #import <AVFoundation/AVFoundation.h>
#endif
class FOHGaugeApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "FOH Gauge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
#if JUCE_MAC
        // Resolve consent before MainComponent constructs AppModel and opens audio
        // devices. A known decision (including denial) must never request again.
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]
                == AVAuthorizationStatusNotDetermined) {
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                completionHandler:^(BOOL) {
                    // AVFoundation completes off the message thread. Do not retain
                    // the app or create a window if it quit while consent was pending.
                    juce::MessageManager::callAsync([] {
                        if (auto* app = dynamic_cast<FOHGaugeApp*>(juce::JUCEApplication::getInstance()))
                            if (!app->shuttingDown) app->window.reset(new Window());
                    });
                }];
            return;
        }
#endif
        window.reset(new Window());
    }
    void shutdown() override { shuttingDown = true; window = nullptr; }
    struct Window : juce::DocumentWindow {
        Window() : DocumentWindow("FOH Gauge", foh::ui::col::background(), allButtons) {
            setUsingNativeTitleBar(true); setContentOwned(new MainComponent(), true);
            setResizable(true, true); setResizeLimits(1280, 800, 4000, 3000); centreWithSize(getWidth(), getHeight()); setVisible(true); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
    bool shuttingDown = false;
};
START_JUCE_APPLICATION(FOHGaugeApp)
