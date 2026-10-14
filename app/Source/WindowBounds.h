#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace fohui {
// Message-thread window geometry; area already excludes any native frame.
inline juce::Rectangle<int> clampWindowBounds(juce::Rectangle<int> bounds,
                                             juce::Rectangle<int> area,
                                             int minWidth, int minHeight) {
    if (area.isEmpty()) return bounds; // Preserve geometry during a transient display refresh.
    return bounds.withSize(juce::jlimit(juce::jmin(minWidth, area.getWidth()), area.getWidth(), bounds.getWidth()),
                           juce::jlimit(juce::jmin(minHeight, area.getHeight()), area.getHeight(), bounds.getHeight()))
                 .constrainedWithin(area);
}
}
