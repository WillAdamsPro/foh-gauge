#include <catch2/catch_test_macros.hpp>
#include "Widgets.h"
#include "Panels.h"
#include <limits>

namespace {
constexpr double qnan = std::numeric_limits<double>::quiet_NaN();
constexpr double inf = std::numeric_limits<double>::infinity();
juce::Image render(PlotComponent& plot) {
    juce::Image image(juce::Image::ARGB, 640, 320, true);
    juce::Graphics graphics(image); plot.setSize(640, 320); plot.paint(graphics); return image;
}
bool samePixels(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
}

TEST_CASE("TF view decimation rejects mismatched FFT grids and gaps corrupt bins", "[app][edges]") {
    foh::TFResult tf; tf.fftSize = 1024; tf.sampleRate = 48000;
    std::vector<double> values(513, 0), f, v; std::vector<uint8_t> gaps;
    logDecimate(tf, values, f, v, &gaps); REQUIRE_FALSE(f.empty());
    for (double bad : {qnan, inf, -inf, std::numeric_limits<double>::denorm_min()}) {
        std::fill(values.begin(), values.end(), bad);
        logDecimate(tf, values, f, v, &gaps); REQUIRE_FALSE(gaps.empty());
        for (auto gap : gaps) REQUIRE(gap == 1);
        for (auto value : v) REQUIRE(std::isfinite(value));
        logDecimate(tf, values, f, v); REQUIRE(f.empty());
    }
    for (int size : {0, -1, 3, 512, 2048}) {
        tf.fftSize = size; logDecimate(tf, values, f, v, &gaps);
        REQUIRE(f.empty()); REQUIRE(v.empty()); REQUIRE(gaps.empty());
    }
    tf.fftSize = 1024;
    for (double rate : {0., -1., qnan, inf}) {
        tf.sampleRate = rate; logDecimate(tf, values, f, v, &gaps); REQUIRE(f.empty());
    }
}

TEST_CASE("Magnitude phase RTA and target painting skips invalid or incomplete points", "[app][edges]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    for (bool bars : {false, true}) {
        PlotComponent plot; plot.bars = bars; plot.wrapPhase = !bars;
        auto empty = render(plot);
        PlotComponent::Series series; series.c = juce::Colours::red;
        series.f = {100, 200, 1000}; series.v = {qnan, inf}; plot.series = {series};
        plot.barLo = {80, 180}; plot.barHi = {120};
        PlotComponent::ToleranceBand band; band.f = {20, 20000}; band.lower = {}; band.upper = {3};
        plot.toleranceBands = {band};
        REQUIRE(samePixels(empty, render(plot)));
        band.lower = {-3, -3}; band.upper = {3, qnan}; plot.toleranceBands = {band};
        REQUIRE(samePixels(empty, render(plot)));
        series.v = {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::denorm_min()};
        plot.series = {series}; REQUIRE(samePixels(empty, render(plot)));
        series.f = {qnan, inf, 0}; series.v = {0, 0, 0}; plot.series = {series};
        REQUIRE(samePixels(empty, render(plot)));
        plot.series.clear(); band.upper = {3, 3}; plot.toleranceBands = {band};
        REQUIRE_FALSE(samePixels(empty, render(plot)));
    }
}

TEST_CASE("Plot paint bounds malformed axis ticks instead of hanging", "[app][edges]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    PlotComponent plot;
    for (double step : {0., -1., qnan, inf, std::numeric_limits<double>::denorm_min()}) {
        plot.yStep = step; REQUIRE_FALSE(render(plot).isNull());
    }
    plot.yStep = 6; plot.yMax = plot.yMin; REQUIRE_FALSE(render(plot).isNull());
    plot.yMax = qnan; REQUIRE_FALSE(render(plot).isNull());
    plot.yMax = 12; plot.fMin = 0; REQUIRE_FALSE(render(plot).isNull());
    plot.fMin = 20; plot.fMax = inf; REQUIRE_FALSE(render(plot).isNull());
    plot.fMax = std::nextafter(plot.fMin, inf); REQUIRE_FALSE(render(plot).isNull());
}

TEST_CASE("SPL panel retains the clipping warning when exposure is invalid", "[app][splcal][edges]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    model.splCals.clear();
    foh::SplCal cal; REQUIRE(cal.calibrate(-20, 94));
    model.splCals[model.splInputKey()] = cal;
    model.leq.valid = false; model.micLevelValid = false;
    auto panel = PanelRegistry::standard().create("SPLNumbers", model); REQUIRE(panel);
    panel->setSize(400, 300);
    auto paint = [&] {
        juce::Image image(juce::Image::ARGB, 400, 300, true);
        juce::Graphics g(image); panel->paint(g); return image;
    };
    model.micPeak = 0;
    const auto invalid = paint();
    model.micPeak = 1;
    const auto clipped = paint();
    CHECK_FALSE(samePixels(invalid, clipped)); // invalid early return must retain the warning
    model.micPeak = -0.f;
    CHECK(samePixels(invalid, paint()));
    // Without SPL calibration the panel asks for calibration, never reports a safe exposure.
    model.splCals.clear();
    const auto uncalibrated = paint();
    CHECK_FALSE(samePixels(invalid, uncalibrated));
    model.leq.valid = true; model.leq.session = {120, 120, 120};
    CHECK(samePixels(uncalibrated, paint()));
}

TEST_CASE("Transfer function controls fit minimum width and preserve disclosure state", "[app][layout][tf]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    FohLookAndFeel look;
    AppModel model;
    model.tf.fftSize = 1024; model.tf.sampleRate = 48000; model.tf.averages = 8;
    model.tf.magDb.assign(513, 0); model.tf.coherence.assign(513, 1);
    model.tf.blanked.assign(513, 0);
    for (int i = 3; i < 12; ++i) model.tf.blanked[(size_t)i] = 1;
    model.calApplied = true;
    auto panel = PanelRegistry::standard().create("TransferFunction", model);
    panel->setLookAndFeel(&look);
    panel->addToDesktop(juce::ComponentPeer::windowIsTemporary); panel->setVisible(true);
    auto checkChildren = [](juce::Component& parent, auto&& self) -> void {
        for (int i = 0; i < parent.getNumChildComponents(); ++i) {
            auto* child = parent.getChildComponent(i);
            if (!child->isVisible()) continue;
            INFO(child->getName().toStdString());
            CHECK(parent.getLocalBounds().contains(child->getBounds()));
            for (int j = i + 1; j < parent.getNumChildComponents(); ++j) {
                auto* other = parent.getChildComponent(j);
                if (other->isVisible()) CHECK_FALSE(child->getBounds().intersects(other->getBounds()));
            }
            // Slider internals are JUCE's responsibility; check our containers.
            if (!dynamic_cast<juce::Slider*>(child) && !dynamic_cast<juce::ComboBox*>(child)) self(*child, self);
        }
    };
    CHECK_FALSE(panel->saveState()["multiMicExpanded"].asBool());
    CHECK(panel->helpText.startsWith("This plot shows"));
    for (const auto& theme : foh::theme::ThemeManager::get().all()) {
        foh::theme::ThemeManager::get().setTheme(theme.id); look.applyTheme(); panel->sendLookAndFeelChange();
        for (bool expanded : {false, true}) for (int width : {foh::theme::ws::plotPanelMinWidth, 800}) {
            auto state = panel->saveState(); state.set("multiMicExpanded", expanded); panel->loadState(state);
            panel->setSize(width, 500); model.notify(AppModel::Snapshot); checkChildren(*panel, checkChildren);
            CHECK(panel->saveState()["multiMicExpanded"].asBool() == expanded);
            PlotComponent* plot = nullptr;
            for (int i = 0; i < panel->getNumChildComponents(); ++i)
                if (auto* p = dynamic_cast<PlotComponent*>(panel->getChildComponent(i))) plot = p;
            REQUIRE(plot);
            if (!expanded) CHECK(plot->getHeight() >= panel->getHeight() * 0.75);
            // Render both logical UI scales using the same transform as the host.
            for (int scale : {1, 2}) {
                juce::Image image(juce::Image::ARGB, width * scale, 500 * scale, true);
                juce::Graphics g(image); g.fillAll(foh::ui::col::cardBackground());
                g.addTransform(juce::AffineTransform::scale((float)scale)); panel->paintEntireComponent(g, true);
                if (auto* directory = std::getenv("FOH_UI_SNAPSHOT_DIR")) {
                    juce::File file = juce::File(directory).getChildFile(theme.id + "-tf-" + juce::String(width) + (expanded ? "-expanded-" : "-collapsed-") + juce::String(scale) + ".png");
                    file.deleteFile(); auto stream = file.createOutputStream(); REQUIRE(stream); juce::PNGImageFormat().writeImageToStream(image, *stream);
                }
            }
        }
    }
    panel->setLookAndFeel(nullptr);
    foh::theme::ThemeManager::get().setTheme("apple-pro-dark");
}

TEST_CASE("Low coherence advice can be dismissed without hiding the hatching", "[app][layout][tf]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    PlotComponent plot; plot.showLowCohCallout = true; plot.lowCoh = {{100, 500}};
    render(plot); REQUIRE(plot.dismissLowCoh.isVisible());
    CHECK(plot.getLocalBounds().contains(plot.dismissLowCoh.getBounds()));
    plot.dismissLowCoh.onClick(); render(plot);
    CHECK_FALSE(plot.dismissLowCoh.isVisible()); CHECK(plot.lowCoh.size() == 1);
}

TEST_CASE("Target popover preserves edits and closes safely with its panel", "[app][layout][tf][target]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    FohLookAndFeel look;
    AppModel model;
    auto findButton = [](juce::Component& root, auto&& self) -> juce::TextButton* {
        for (auto* child : root.getChildren()) {
            if (auto* button = dynamic_cast<juce::TextButton*>(child); button && button->getButtonText().startsWith("Target ")) return button;
            if (auto* button = self(*child, self)) return button;
        }
        return nullptr;
    };
    for (const auto& theme : foh::theme::ThemeManager::get().all()) {
        foh::theme::ThemeManager::get().setTheme(theme.id); look.applyTheme();
        auto panel = PanelRegistry::standard().create("TransferFunction", model);
        panel->setLookAndFeel(&look); panel->setSize(800, 500);
        panel->addToDesktop(juce::ComponentPeer::windowIsTemporary); panel->setVisible(true);
        auto* button = findButton(*panel, findButton); REQUIRE(button);
        button->onClick();
        auto* callout = dynamic_cast<juce::CallOutBox*>(juce::Component::getCurrentlyModalComponent()); REQUIRE(callout);
        juce::Slider* offset = nullptr;
        auto findSlider = [&](juce::Component& root, auto&& self) -> void {
            for (auto* child : root.getChildren()) {
                if (auto* slider = dynamic_cast<juce::Slider*>(child)) offset = slider;
                else self(*child, self);
            }
        };
        findSlider(*callout, findSlider); REQUIRE(offset); CHECK(offset->getTextValueSuffix() == " dB");
        offset->setValue(3.5, juce::dontSendNotification); offset->onValueChange();
        CHECK(panel->saveState()["targetOffset"].asNumber() == 3.5);
        for (int scale : {1, 2}) if (auto* directory = std::getenv("FOH_UI_SNAPSHOT_DIR")) {
            auto image = callout->createComponentSnapshot(callout->getLocalBounds(), true, (float)scale);
            auto file = juce::File(directory).getChildFile(theme.id + "-target-" + juce::String(scale) + ".png");
            file.deleteFile(); auto stream = file.createOutputStream(); REQUIRE(stream); juce::PNGImageFormat().writeImageToStream(image, *stream);
        }
        callout->dismiss(); juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
        REQUIRE_FALSE(panel->isCurrentlyBlockedByAnotherModalComponent());
        // Reopening keeps the same controls and edited values.
        button->onClick(); REQUIRE(juce::Component::getCurrentlyModalComponent());
        panel->setLookAndFeel(nullptr); panel.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
        CHECK(juce::Component::getCurrentlyModalComponent() == nullptr);
    }
    foh::theme::ThemeManager::get().setTheme("apple-pro-dark");
}
