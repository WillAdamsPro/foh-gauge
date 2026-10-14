// Tear-off floating windows (spec §5). The Panel component is re-parented into the window and back,
// never re-created, so meters and averages don't reset. Bounds + display are remembered per panel in
// the Workspace (fohws::FloatingPanel) and restored with fohws::clampToDisplays.
#pragma once
#include "WorkspaceView.h"

std::vector<fohws::DisplayInfo> currentDisplays();          // juce::Displays -> fohws (id = display's total area)
std::string displayIdFor(juce::Rectangle<int> screenBounds);

class FloatingPanelWindow : public juce::DocumentWindow {
public:
    FloatingPanelWindow(Panel& p, fohws::FloatingPanel& state);
    ~FloatingPanelWindow() override;
    void closeButtonPressed() override { if (onClose) onClose(); }
    void moved() override; void resized() override;
    void parentSizeChanged() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;   // title bar double-click = dock to last slot
    void setPinned(bool);
    void restyle();
    Panel& panel;
    std::function<void()> onDock, onClose, onChanged;
private:
    struct TitleButton;
    void restoreToDisplays(const std::vector<fohws::DisplayInfo>&);
    fohws::FloatingPanel& st; std::unique_ptr<TitleButton> pin, dock; bool ready = false;
};
