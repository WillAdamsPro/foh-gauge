#include <catch2/catch_test_macros.hpp>
#include "SqConsoleWindow.h"
TEST_CASE("SQ panel starts disabled and dry with verified strip choices", "[app][sq]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    foh::sq::SqConsolePanel panel;
    bool enabled = true, dry = false; int targets = 0;
    for (auto* child : panel.getChildren()) {
        if (auto* button = dynamic_cast<juce::ToggleButton*>(child)) {
            if (button->getButtonText().startsWith("Enable console")) enabled = button->getToggleState();
            if (button->getButtonText().startsWith("Dry-run")) dry = button->getToggleState();
        }
        if (auto* box = dynamic_cast<juce::ComboBox*>(child))
            if (box->getItemText(0) == "Input 1") {
                targets = box->getNumItems();
                CHECK(box->getItemText(48) == "Aux 1"); CHECK(box->getItemText(49) == "Matrix 1 (stereo)");
            }
    }
    CHECK_FALSE(enabled); CHECK(dry); CHECK(targets == 50);
    CHECK(panel.sessionState()["undo"].items().empty());
    // Loading a session cannot restore a live runtime toggle or live-write mode.
    for (auto* child : panel.getChildren())
        if (auto* button = dynamic_cast<juce::ToggleButton*>(child)) button->setToggleState(true,juce::dontSendNotification);
    panel.restoreSession(panel.sessionState());
    for (auto* child : panel.getChildren())
        if (auto* button = dynamic_cast<juce::ToggleButton*>(child)) {
            if (button->getButtonText().startsWith("Enable console")) CHECK_FALSE(button->getToggleState());
            if (button->getButtonText().startsWith("Dry-run")) CHECK(button->getToggleState());
        }
}
