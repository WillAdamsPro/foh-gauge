#include <catch2/catch_test_macros.hpp>
#include "Widgets.h"
#include <cmath>

namespace {
double luminance(juce::Colour c) {
    auto linear = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(c.getFloatRed()) + 0.7152 * linear(c.getFloatGreen()) + 0.0722 * linear(c.getFloatBlue());
}
double contrast(juce::Colour fg, juce::Colour bg) {
    const auto a = luminance(bg.overlaidWith(fg)), b = luminance(bg);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}
}
TEST_CASE("Theme text and stored traces meet contrast requirements", "[ui][theme]") {
    for (const auto& t : {foh::theme::proDark(), foh::theme::proSunlight()}) {
        INFO(t.id.toStdString());
        for (auto bg : {t.surfaces.background, t.surfaces.nav, t.surfaces.surface,
                        t.surfaces.surfaceRaised, t.surfaces.plotWell, t.surfaces.control})
            for (auto fg : {t.text.primary, t.text.secondary, t.text.axis}) {
                INFO("foreground " << fg.toString().toStdString() << " background " << bg.toString().toStdString());
                REQUIRE(contrast(fg, bg) >= 4.5);
            }
        REQUIRE(contrast(t.text.onAccent, t.accent) >= 4.5);
        REQUIRE(t.trace.magnitude == t.brand.needle);
        REQUIRE(t.trace.lowCoherenceHatchAlpha >= 0.35f);
        for (const auto& slot : t.traceSlots) {
            REQUIRE(juce::String(slot.name).isNotEmpty());
            REQUIRE((slot.dashCount == 0 || slot.dashCount == 2 || slot.dashCount == 4));
            if (t.isLight) REQUIRE(contrast(slot.colour, t.surfaces.control) >= 4.5);
        }
    }
    REQUIRE(foh::theme::proDark().trace.magnitude.toString() == "ffff6a3d");
}
TEST_CASE("Theme ids migrate and type roles resolve against the selected theme", "[ui][theme]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& manager = foh::theme::ThemeManager::get();
    for (auto id : {"sunlight", "c-sunlight", "apple-pro-sunlight", "pro-sunlight"}) {
        manager.setTheme(id);
        REQUIRE(manager.current().id == "pro-sunlight");
        REQUIRE(FohLookAndFeel::font(foh::theme::type::heading).getHeight() == 19);
        REQUIRE(FohLookAndFeel::numeric(foh::theme::type::valueLg).getHeight() == 42);
    }
    for (auto id : {"dark", "a", "b", "c", "apple-pro-dark", "pro-dark"}) {
        manager.setTheme(id);
        REQUIRE(manager.current().id == "pro-dark");
        REQUIRE(FohLookAndFeel::font(foh::theme::type::heading).getHeight() == 17);
    }
    REQUIRE(manager.all().size() == 2);
}

TEST_CASE("Theme changes resize existing label fonts", "[ui][theme]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& manager = foh::theme::ThemeManager::get();
    manager.setTheme("pro-dark");
    FohLookAndFeel look;
    juce::Component root;
    juce::Label label;
    root.addAndMakeVisible(label);
    label.setFont(FohLookAndFeel::font(foh::theme::type::heading));
    manager.setTheme("pro-sunlight");
    look.refreshThemeFonts(root); look.applyTheme();
    REQUIRE(label.getFont().getHeight() == 19);
    manager.setTheme("pro-dark");
    look.refreshThemeFonts(root); look.applyTheme();
    REQUIRE(label.getFont().getHeight() == 17);
}
