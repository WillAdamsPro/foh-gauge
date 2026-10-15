#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Display-only vector drawings; created and painted on the message thread.
class MicPlacementDiagram : public juce::Component {
public:
    MicPlacementDiagram() { setTitle("Mic placement diagram"); setDescription("Illustration of the mic position described in the guide instructions"); }
    enum class Placement { None, OnAxis, Seam, SubMainCrossover };
    void setPlacement(Placement, bool mainArray = false);
    Placement getPlacement() const noexcept { return placement; }
    void paint(juce::Graphics&) override;
private:
    bool array = false;
    Placement placement = Placement::None;
};
