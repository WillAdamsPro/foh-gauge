#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Display-only vector drawings; created and painted on the message thread.
class MicPlacementDiagram : public juce::Component {
public:
    enum class Placement { None, OnAxis, Seam, SubMainCrossover };
    void setPlacement(Placement);
    Placement getPlacement() const noexcept { return placement; }
    void paint(juce::Graphics&) override;
private:
    Placement placement = Placement::None;
};
