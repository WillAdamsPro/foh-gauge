#include <catch2/catch_test_macros.hpp>
#include "console/ConsoleWindow.h"
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif
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

TEST_CASE("Console window visibility changes cannot leave native animations after teardown", "[app][console][lifetime][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    for (int cycle = 0; cycle < 24; ++cycle) {
        INFO("window lifetime " << cycle);
        {
            foh::console::ConsoleWindow window;
            REQUIRE(window.getPeer() != nullptr);
#if JUCE_MAC
            NSWindow* native = [(NSView*) window.getPeer()->getNativeHandle() window];
            REQUIRE(native != nil);
            CHECK(native.animationBehavior == NSWindowAnimationBehaviorNone);
#endif
            window.closeButtonPressed();
            CHECK_FALSE(window.isVisible());
            window.setVisible(true);
            CHECK(window.isVisible());
            // Destroy immediately after reopening, before AppKit can complete
            // an automatic animation. No connection or console write is made.
        }
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
}
