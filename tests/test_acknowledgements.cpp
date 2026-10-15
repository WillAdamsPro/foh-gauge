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
    REQUIRE(text.startsWith(juce::String::fromUTF8(
        "FOH Gauge \xe2\x80\x94 About / Acknowledgements\n\n")));
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

TEST_CASE("About lockup renders against raised surfaces in both themes", "[ui][brand]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (const auto* theme : {"pro-dark", "pro-sunlight"}) {
        foh::theme::ThemeManager::get().setTheme(theme);
        Acknowledgements screen;
        auto image = screen.createComponentSnapshot({0, 0, screen.getWidth(), Acknowledgements::headerHeight});
        CHECK(image.getPixelAt(0, 0) == foh::ui::col::pillBackground());
        if (auto* folder = std::getenv("FOH_TOOLBAR_QA_DIR")) {
            juce::File dir(folder); dir.createDirectory();
            auto stream = dir.getChildFile(juce::String("about-") + theme + ".png").createOutputStream();
            REQUIRE(stream); juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream));
        }
    }
    foh::theme::ThemeManager::get().setTheme("pro-dark");
}
