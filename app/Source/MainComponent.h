// MainComponent: window frame only. Toolbar (title, workspace switcher, status pills, Stop), sidebar
// (appearance, UI scale) and the WorkspaceView that hosts the panels.
#pragma once
#include "FloatingPanels.h"
#include <fohcore/session.h>
#include "SetupPage.h"
#include "WorkspaceManager.h"
#include "WorkspacePresetCommands.h"

class MainComponent : public juce::Component, public juce::ApplicationCommandTarget, private juce::Timer, private juce::ChangeListener, private AppModel::Listener {
public:
    MainComponent(); ~MainComponent() override;
    enum : juce::CommandID { openSessionCommand = 0x3000, saveSessionCommand, saveSessionAsCommand };
    juce::PopupMenu getFileMenu(juce::ApplicationCommandManager&);
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>&) override;
    void getCommandInfo(juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform(const InvocationInfo&) override;
    void saveSession(bool saveAs);
    void openSession();
    bool writeSession(const juce::File&);
    bool readSession(const juce::File&);
    void resized() override;
    int uiScalePercent() const { return uiScale; }
    void setUiScale(int percent);
    void stepUiScale(int direction);
    void fitSavedScale();
    bool isSunlightTheme() const { return foh::theme::ThemeManager::get().current().isLight; }
    void setSunlightTheme(bool enabled);
    void selectWorkspace(int index);
    void selectWorkspace(const std::string& id);
    bool keyPressed(const juce::KeyPress&) override;
    void floatPanel(const std::string& id);
    void dockPanel(const std::string& id);
protected:
    void timerCallback() override; void changeListenerCallback(juce::ChangeBroadcaster*) override; void modelChanged(int what) override;
    virtual void workspaceSelected() {}
    bool sidebarVisible() const { return !showMode && content.getWidth() >= 1280; } // spec §7: sidebar collapses under 1280 px
    // Only this child is transformed; the native window stays in display coordinates.
    struct Content : juce::Component {
        explicit Content(MainComponent& ownerIn) : owner(ownerIn) {}
        void paint(juce::Graphics& g) override { owner.paintContent(g); }
        MainComponent& owner;
    } content { *this };
    void paintContent(juce::Graphics&);
    void layoutContent();
    int uiScale = 100;
    juce::File sessionFolder;
    std::unique_ptr<juce::FileChooser> sessionChooser;
    std::optional<foh::Json> sessionTargets;
    FohLookAndFeel lnf;
    AppModel model;
    WorkspaceManager mgr; int current = -1;
    WorkspacePresetCommands presetCommands { [this](int i) { selectWorkspace(i); } };
    juce::OwnedArray<FloatingPanelWindow> floats;
    bool showMode = false;
    void setShowMode(bool enabled);
    void openSetup();
    SetupPage setup { model };
    juce::TextButton setupBtn { "Audio Setup" }, splBtn { "SPL Cal" };
    juce::TextButton wsMenuBtn, showModeBtn;
    void openFloatingWindows(); void closeFloatingWindows();
    void showWorkspaceMenu(); void updateSwitcher(); void leaveWorkspace();
    WorkspaceView view { model, PanelRegistry::standard() };
    juce::Label audioNotice;
    juce::Label toolbarTitle; SegmentedControl switcher {{"Setup", "Alignment", "Tuning", "RTA/Show"}};
    StatusPills status; juce::TextButton pauseBtn;
    SegmentedControl appearance {{"Dark", "Sunlight"}}; juce::ComboBox scaleBox;
};
