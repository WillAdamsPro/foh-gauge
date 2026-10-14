// MainComponent: window frame only. Toolbar (title, workspace switcher, status pills, Stop), sidebar
// (workspace nav, appearance, UI scale) and the WorkspaceView that hosts the panels.
#pragma once
#include "FloatingPanels.h"
#include "SetupPage.h"
#include "WorkspaceManager.h"
#include "WorkspacePresetCommands.h"

class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener, private AppModel::Listener {
public:
    MainComponent(); ~MainComponent() override;
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
    FohLookAndFeel lnf;
    AppModel model;
    WorkspaceManager mgr; int current = -1;
    WorkspacePresetCommands presetCommands { [this](int i) { selectWorkspace(i); } };
    juce::OwnedArray<FloatingPanelWindow> floats;
    bool showMode = false;
    void setShowMode(bool enabled);
    void openSetup();
    SetupPage setup { model };
    juce::TextButton setupBtn { "Audio Setup" };
    juce::TextButton wsMenuBtn, showModeBtn;
    void openFloatingWindows(); void closeFloatingWindows();
    void showWorkspaceMenu(); void updateSwitcher(); void leaveWorkspace();
    WorkspaceView view { model, PanelRegistry::standard() };
    juce::Label toolbarTitle; SegmentedControl switcher {{"Setup", "Alignment", "Tuning", "RTA/Show"}};
    StatusPills status; juce::TextButton pauseBtn;
    juce::OwnedArray<NavButton> nav; SegmentedControl appearance {{"Dark", "Sunlight"}}; juce::ComboBox scaleBox;
};
