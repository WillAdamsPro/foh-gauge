#include <catch2/catch_test_macros.hpp>
#include "console/ConsoleWindow.h"
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
