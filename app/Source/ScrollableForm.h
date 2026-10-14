#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Message-thread form canvas: keep controls readable when the host is smaller than
// their minimum layout, with both axes reachable through the viewport.
class ScrollableForm : public juce::Viewport {
public:
    struct Canvas : juce::Component {
        std::function<void(juce::Graphics&)> onPaint;
        void paint(juce::Graphics& g) override { if (onPaint) onPaint(g); }
    } canvas;
    ScrollableForm() { setViewedComponent(&canvas, false); }
    ~ScrollableForm() override { setViewedComponent(nullptr, false); }
    int layoutWidth(int minimum) const {
        return juce::jmax(minimum, getWidth() - getScrollBarThickness());
    }
    void sizeCanvas(int width, int height) {
        canvas.setSize(width, juce::jmax(height, getHeight() - getScrollBarThickness()));
    }
};
