// MainComponent: window frame only. Toolbar (title, workspace switcher, status pills, Stop), sidebar
// (workspace nav, appearance, UI scale) and the WorkspaceView that hosts the panels.
#pragma once
#include "FloatingPanels.h"
#include "WorkspaceManager.h"
#include "WorkspacePresetCommands.h"

class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener, private AppModel::Listener {
public:
    MainComponent(); ~MainComponent() override;
    void paint(juce::Graphics&) override; void resized() override;
    void selectWorkspace(int index);
    void selectWorkspace(const std::string& id);
    bool keyPressed(const juce::KeyPress&) override;
    void floatPanel(const std::string& id);
    void dockPanel(const std::string& id);
protected:
    void timerCallback() override; void changeListenerCallback(juce::ChangeBroadcaster*) override; void modelChanged(int what) override;
    virtual void workspaceSelected() {}
    bool sidebarVisible() const { return !showMode && getWidth() >= 1280; } // spec §7: sidebar collapses under 1280 px
    FohLookAndFeel lnf;
    AppModel model;
    WorkspaceManager mgr; int current = -1;
    WorkspacePresetCommands presetCommands { [this](int i) { selectWorkspace(i); } };
    juce::OwnedArray<FloatingPanelWindow> floats;
    bool showMode = false;
    void setShowMode(bool enabled);
    juce::TextButton wsMenuBtn, showModeBtn;
    void openFloatingWindows(); void closeFloatingWindows();
    void showWorkspaceMenu(); void updateSwitcher(); void leaveWorkspace();
    WorkspaceView view { model, PanelRegistry::standard() };
    juce::Label toolbarTitle; SegmentedControl switcher {{"Setup", "Alignment", "Tuning", "RTA/Show"}};
    StatusPills status; juce::TextButton pauseBtn;
    juce::OwnedArray<NavButton> nav; SegmentedControl appearance {{"Dark", "Sunlight"}}; juce::ComboBox scaleBox;
};
