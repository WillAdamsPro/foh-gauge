#include <catch2/catch_test_macros.hpp>
#include "console/ConsoleWindow.h"
TEST_CASE("Console text decodes UTF-8 punctuation without corrupting labels", "[app][console][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    foh::console::ConsolePanel panel;
    juce::ComboBox* desk = nullptr;
    for (auto* child : panel.getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(child))
            if (box->getItemText(0) == "Behringer X32 (OSC)") desk = box;
    REQUIRE(desk != nullptr);
    CHECK(desk->getItemText(1).toStdString() == "SQ-5 1.6.3 \xe2\x80\x94 awaiting protocol verification");
    // A selection change exercises status text without opening a socket.
    desk->setSelectedId(2, juce::sendNotificationSync);
    bool foundStatus = false;
    for (auto* child : panel.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            if (label->getText().startsWith("Selection changed")) {
                foundStatus = true;
                CHECK(label->getText().toStdString() == "Selection changed \xe2\x80\x94 connect/read again");
            }
    CHECK(foundStatus);
}

TEST_CASE("Console window owns its panel and closes without connecting or writing", "[app][console][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    {
        foh::console::ConsoleWindow window;
        CHECK(window.isVisible());
        REQUIRE(dynamic_cast<foh::console::ConsolePanel*>(window.getContentComponent()) != nullptr);
        window.closeButtonPressed();
        CHECK_FALSE(window.isVisible());
        window.setVisible(true);
        CHECK(window.isVisible());
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
}
