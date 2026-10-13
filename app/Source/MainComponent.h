// MainComponent: window frame only. Toolbar (title, workspace switcher, status pills, Stop), sidebar
// (workspace nav, appearance, UI scale) and the WorkspaceView that hosts the panels.
#pragma once
#include "WorkspaceView.h"

class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener, private AppModel::Listener {
public:
    MainComponent(); ~MainComponent() override;
    void paint(juce::Graphics&) override; void resized() override;
    void selectWorkspace(int index);
    void selectWorkspace(const std::string& id);
protected:
    void timerCallback() override; void changeListenerCallback(juce::ChangeBroadcaster*) override; void modelChanged(int what) override;
    virtual void workspaceSelected() {}
    bool sidebarVisible() const { return getWidth() >= 1280; } // spec §7: sidebar collapses under 1280 px
    FohLookAndFeel lnf;
    AppModel model;
    std::vector<fohws::Workspace> workspaces; int current = -1;
    WorkspaceView view { model, PanelRegistry::standard() };
    juce::Label toolbarTitle; SegmentedControl switcher {{"Setup", "Alignment", "Tuning", "RTA/Show"}};
    StatusPills status; juce::TextButton pauseBtn;
    juce::OwnedArray<NavButton> nav; SegmentedControl appearance {{"Dark", "Sunlight"}}; juce::ComboBox scaleBox;
};
