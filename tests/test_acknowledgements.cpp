#include <catch2/catch_test_macros.hpp>
#include "Acknowledgements.h"

TEST_CASE("Acknowledgements exposes complete offline notices in a scrollable reader", "[ui][compliance]") {
    juce::ScopedJuceInitialiser_GUI gui;
    Acknowledgements screen;
    auto* reader = dynamic_cast<juce::TextEditor*>(screen.getChildComponent(0));
    REQUIRE(reader != nullptr);
    REQUIRE(reader->isReadOnly());
    REQUIRE(reader->isMultiLine());
    const auto text = reader->getText();
    REQUIRE(text.contains("JUCE 8.0.4"));
    REQUIRE(text.contains("Raw Material Software Limited"));
    REQUIRE(text.contains("Smaart, X32, Behringer, SQ and Allen & Heath"));
    REQUIRE(text.contains("Boost Software License - Version 1.0"));
    REQUIRE(text.contains("SIL OPEN FONT LICENSE Version 1.1"));
    REQUIRE(text.contains("OTHER DEALINGS IN THE FONT SOFTWARE."));
    REQUIRE(text.contains("Apache License"));
    REQUIRE(text.contains("HarfBuzz"));
    screen.setSize(480, 320);
    REQUIRE(screen.getLocalBounds().contains(reader->getBounds()));
}
