#pragma once
#include "FloatingPanels.h"

// Visible panel tests must use the app's window owner. A bare addToDesktop(0)
// creates an automatically animated NSWindow whose close animation can outlive
// its JUCE peer on macOS 26. Declare this fixture after the panel, and destroy
// it before explicitly resetting the panel (the window does not own content).
class VisiblePanelWindow {
public:
    explicit VisiblePanelWindow(Panel& panel)
        : width(panel.getWidth()), height(panel.getHeight()),
          state{"", {100, 100, width, height + 32}, "", false, ""},
          window(panel, state) {
        window.setContentComponentSize(width, height);
    }

    FloatingPanelWindow& get() { return window; }

private:
    const int width, height;
    fohws::FloatingPanel state;
    FloatingPanelWindow window;
};
