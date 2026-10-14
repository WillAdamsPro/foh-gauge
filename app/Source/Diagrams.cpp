#include "Diagrams.h"
#include "Widgets.h"

namespace C = foh::ui::col;

void MicPlacementDiagram::setPlacement(Placement value, bool mainArray) {
    placement = value; array = mainArray;
    const char* description = "";
    switch (placement) {
        case Placement::OnAxis: description = array ? "Mic on-axis in one main cluster coverage, where the cabinets combine" : "Mic on-axis, 1-3 m from one main, at ear height"; break;
        case Placement::Seam: description = "Mic in the main/fill seam, inside the fill area, where both are equally loud"; break;
        case Placement::SubMainCrossover: description = "Mic where sub and mains are equally loud in the crossover band"; break;
        case Placement::None: break;
    }
    setTitle("Mic placement");
    setDescription(description);
    setVisible(placement != Placement::None);
    repaint();
}

void MicPlacementDiagram::paint(juce::Graphics& g) {
    if (placement == Placement::None || getWidth() <= 0 || getHeight() <= 0) return;
    // A fixed design space scales uniformly, including text, in docked and floating panels.
    const float scale = juce::jmin(getWidth() / 232.f, getHeight() / 104.f);
    g.addTransform(juce::AffineTransform::scale(scale).translated(
        (getWidth() - 232.f * scale) / 2, (getHeight() - 104.f * scale) / 2));
    g.setColour(C::divider());
    g.drawRoundedRectangle(0.5f, 0.5f, 231.f, 103.f, 6.f, 1.f);
    g.setFont(FohLookAndFeel::font(12.f));
    auto label = [&](const char* text, int x, int y, int w) {
        g.setColour(C::textPrimary());
        g.drawText(text, x, y, w, 18, juce::Justification::centred);
    };
    auto speaker = [&](float x, float y, const char* name) {
        g.setColour(C::textSecondary());
        g.drawRoundedRectangle(x, y, 24.f, 28.f, 3.f, 1.5f);
        g.drawEllipse(x + 7, y + 8, 10.f, 10.f, 1.5f);
        label(name, (int) x - 10, (int) y - 20, 44);
    };
    auto mic = [&](float x, float y) {
        g.setColour(C::accent());
        g.fillEllipse(x - 4, y - 4, 8.f, 8.f);
        g.drawLine(x, y + 4, x, y + 17, 2.f);
        g.drawLine(x - 7, y + 17, x + 7, y + 17, 1.5f);
        label("Mic", (int) x - 18, (int) y + 18, 36);
    };
    auto pathToMic = [&](float x, float y, float mx, float my) {
        g.setColour(C::accent().withAlpha(0.65f));
        const float dashes[] = {4.f, 3.f};
        g.drawDashedLine(juce::Line<float>(x, y, mx, my), dashes, 2, 1.5f);
    };
    if (placement == Placement::OnAxis) {
        speaker(20, 30, "Main");
        pathToMic(44, 44, 184, 44);
        mic(184, 44);
        label(array ? "Array coverage" : "1-3 m on-axis", 54, 23, 116);
        label(array ? "One main cluster on" : "Ear height; one main on", 12, 82, 208);
    } else {
        speaker(20, 30, "Main");
        speaker(188, 30, placement == Placement::Seam ? "Fill" : "Sub");
        // These lines show the listening overlap, not equal distances or a midpoint rule.
        pathToMic(44, 58, 116, 66);
        pathToMic(188, 58, 116, 66);
        mic(116, 66);
        label(placement == Placement::Seam ? "Seam / fill area" : "Crossover band", 52, 24, 128);
        label("Equal level here", 52, 42, 128);
    }
}
