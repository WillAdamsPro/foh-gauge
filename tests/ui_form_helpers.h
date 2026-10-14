#pragma once
#include "ScrollableForm.h"

inline juce::Component& formCanvas(juce::Component& component) {
    for (auto* child : component.getChildren())
        if (auto* form = dynamic_cast<ScrollableForm*>(child)) return form->canvas;
    return component;
}
