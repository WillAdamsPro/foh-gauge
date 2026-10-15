#include "Strings.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "MainComponent.h"
#include "Acknowledgements.h"
#include "WindowBounds.h"
#if JUCE_MAC
 #import <AVFoundation/AVFoundation.h>
 #import <AppKit/AppKit.h>
#endif
class FOHGaugeApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "FOH Gauge"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const juce::String&) override {
        // Brief launch splash (embedded PNG); click to dismiss early. Deletes itself.
        auto splash = juce::ImageCache::getFromMemory(BinaryData::splash_png, BinaryData::splash_pngSize);
        if (splash.isValid())
            (new juce::SplashScreen("FOH Gauge", splash.rescaled(640, 400, juce::Graphics::highResamplingQuality), true))
                ->deleteAfterDelay(juce::RelativeTime::seconds(1.5), true);
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
    struct Window : juce::DocumentWindow, juce::MenuBarModel, juce::ApplicationCommandTarget, private juce::Timer {
        enum { resetScale = 1, largerScale, smallerScale, scale125, scale150, scale200, darkTheme, sunlightTheme, about, expertMode, showMode };
        MainComponent* main = nullptr; // owned by DocumentWindow
        juce::ApplicationCommandManager commands;
        juce::Component::SafePointer<juce::DialogWindow> acknowledgements;
        Window() : DocumentWindow("FOH Gauge", foh::ui::col::background(), allButtons) {
            setUsingNativeTitleBar(true);
            main = new MainComponent();
            updateMicrophoneConsent();
            setContentOwned(main, true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            constrainToDisplay();
            main->fitSavedScale();
            commands.registerAllCommandsForTarget(main);
            commands.registerAllCommandsForTarget(this);
            commands.setFirstCommandTarget(this);
            addKeyListener(commands.getKeyMappings());
#if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(this);
#else
            setMenuBar(this);
#endif
            setVisible(true);
#if JUCE_MAC
            startTimer(1000); // Refresh consent after a change in System Settings.
#endif
        }
        ~Window() override {
            stopTimer();
            if (acknowledgements != nullptr) delete acknowledgements.getComponent();
#if JUCE_MAC
            juce::MenuBarModel::setMacMainMenu(nullptr);
#else
            setMenuBar(nullptr);
#endif
            removeKeyListener(commands.getKeyMappings());
        }
        void updateMicrophoneConsent() {
#if JUCE_MAC
            const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
            main->setMicrophoneAccessBlocked(status == AVAuthorizationStatusDenied
                                            || status == AVAuthorizationStatusRestricted);
#endif
        }
        bool lastShowMode = false, lastExpertMode = false;
        void timerCallback() override {
            updateMicrophoneConsent();
            if (lastShowMode != main->isShowMode() || lastExpertMode != main->isExpertMode()) {
                lastShowMode = main->isShowMode(); lastExpertMode = main->isExpertMode();
                commands.commandStatusChanged();
            }
            annotateExpertTooltip();
        }
        void annotateExpertTooltip() {
#if JUCE_MAC
            // JUCE's native menu bridge omits command descriptions/tooltips.
            // Annotate the native item so disabled Expert explains how to enable it.
            std::function<void(NSMenu*)> annotate = [&](NSMenu* menu) {
                for (NSMenuItem* item in [menu itemArray]) {
                    if ([[item title] isEqualToString:@"Expert"])
                        [item setToolTip:main->isShowMode() ? @"Exit Show mode first" : @"Reveal all charts and settings"];
                    if ([item submenu] != nil) annotate([item submenu]);
                }
            };
            annotate([NSApp mainMenu]);
#endif
        }
        bool constraining = false;
        void constrainToDisplay() {
            if (constraining) return;
            const juce::ScopedValueSetter<bool> guard(constraining, true);
            auto& displays = juce::Desktop::getInstance().getDisplays();
            if (auto* display = displays.getDisplayForRect(getBounds())) {
                auto area = display->userArea;
                // JUCE bounds exclude the native frame, so reserve that space too.
                if (auto* peer = getPeer())
                    if (const auto frame = peer->getFrameSizeIfPresent()) frame->subtractFrom(area);
                if (area.isEmpty()) return;
                setResizeLimits(juce::jmin(640, area.getWidth()), juce::jmin(400, area.getHeight()),
                                area.getWidth(), area.getHeight());
                setBounds(fohui::clampWindowBounds(getBounds(), area, 640, 400));
            }
        }
        void parentSizeChanged() override {
            DocumentWindow::parentSizeChanged();
            constrainToDisplay();
        }
        void moved() override { constrainToDisplay(); }
        void resized() override { DocumentWindow::resized(); constrainToDisplay(); }
        juce::StringArray getMenuBarNames() override { return { "File", "View", "Help" }; }
        juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
            if (index == 0) return main->getFileMenu(commands);
            juce::PopupMenu menu;
#if JUCE_MAC
            // Native items are created after getMenuForIndex returns.
            juce::MessageManager::callAsync([self = juce::Component::SafePointer<Window>(this)] {
                if (self) self->annotateExpertTooltip();
            });
#endif
            if (index == 2) { menu.addCommandItem(&commands, about); return menu; }
            for (int id : { resetScale, scale125, scale150, scale200, largerScale, smallerScale })
                menu.addCommandItem(&commands, id);
            menu.addSeparator();
            juce::PopupMenu appearance;
            appearance.addCommandItem(&commands, darkTheme);
            appearance.addCommandItem(&commands, sunlightTheme);
            menu.addSubMenu("Appearance", appearance);
            menu.addSeparator();
            menu.addCommandItem(&commands, expertMode);
            menu.addCommandItem(&commands, showMode);
            return menu;
        }
        // Command items dispatch through the manager to Window (View) or MainComponent (File).
        void menuItemSelected(int, int) override {}
        juce::ApplicationCommandTarget* getNextCommandTarget() override { return main; }
        void getAllCommands(juce::Array<juce::CommandID>& ids) override {
            for (int id : { resetScale, largerScale, smallerScale, scale125, scale150, scale200, darkTheme, sunlightTheme, about, expertMode, showMode }) ids.add(id);
        }
        void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
            if (id == expertMode || id == showMode) {
                info.setInfo(id == expertMode ? "Expert" : "Show mode",
                    id == expertMode && main->isShowMode() ? "Exit Show mode first" : id == expertMode ? "Reveal all charts and settings" : "Monitor the show against a stored reference", "View", 0);
                info.setTicked(id == expertMode ? main->isExpertMode() : main->isShowMode());
                info.setActive(id != expertMode || !main->isShowMode());
                return;
            }
            if (id == about) {
                info.setInfo("About / Acknowledgements", "Third-party notices and font license", "Help", 0);
                return;
            }
            if (id < resetScale || id > sunlightTheme) return;
            const char* names[] = { "100%", "Increase UI scale", "Decrease UI scale", "125%", "150%", "200%", foh::str::dark, foh::str::sunlight };
            info.setInfo(names[id - 1], names[id - 1], "View", 0);
            const int percent = id == resetScale ? 100 : id == scale125 ? 125 : id == scale150 ? 150 : id == scale200 ? 200 : 0;
            if (percent) info.setTicked(main->uiScalePercent() == percent);
            if (id == darkTheme || id == sunlightTheme) info.setTicked(main->isSunlightTheme() == (id == sunlightTheme));
            if (id <= smallerScale) info.addDefaultKeypress(id == resetScale ? '0' : id == largerScale ? '=' : '-', juce::ModifierKeys::commandModifier);
        }
        bool perform(const InvocationInfo& info) override {
            switch (info.commandID) {
                case about: {
                    if (acknowledgements != nullptr) {
                        acknowledgements->toFront(true);
                        break;
                    }
                    juce::DialogWindow::LaunchOptions options;
                    options.content.setOwned(new Acknowledgements());
                    options.dialogTitle = "About FOH Gauge / Acknowledgements";
                    options.dialogBackgroundColour = foh::ui::col::background();
                    options.componentToCentreAround = this;
                    options.useNativeTitleBar = true;
                    options.resizable = true;
                    options.escapeKeyTriggersCloseButton = true;
                    acknowledgements = options.launchAsync();
                    break;
                }
                case expertMode: main->setExpertMode(!main->isExpertMode()); break;
                case showMode: main->setShowMode(!main->isShowMode()); break;
                case resetScale: main->setUiScale(100); break;
                case largerScale: main->stepUiScale(1); break;
                case smallerScale: main->stepUiScale(-1); break;
                case scale125: main->setUiScale(125); break;
                case scale150: main->setUiScale(150); break;
                case scale200: main->setUiScale(200); break;
                case darkTheme: main->setSunlightTheme(false); break;
                case sunlightTheme: main->setSunlightTheme(true); break;
                default: return false;
            }
            commands.commandStatusChanged();
            return true;
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
    bool shuttingDown = false;
};
START_JUCE_APPLICATION(FOHGaugeApp)
