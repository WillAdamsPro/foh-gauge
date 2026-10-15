#include <catch2/catch_test_macros.hpp>
#include "Widgets.h"
#include "Panels.h"
#include "ui_panel_window.h"
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
    foh::theme::ThemeManager::get().setTheme("pro-dark");
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
        // Dismissal and deletion are queued by JUCE. Wait for the callback rather
        // than assuming a loaded macOS runner drains its queue within 250 ms.
        auto waitFor = [](auto ready) {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
            while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            return ready();
        };
        juce::Component::SafePointer<juce::CallOutBox> closing(callout);
        callout->dismiss();
        REQUIRE(waitFor([&] { return closing == nullptr; }));
        REQUIRE_FALSE(panel->isCurrentlyBlockedByAnotherModalComponent());
        // Reopening keeps the same controls and edited values.
        button->onClick(); REQUIRE(juce::Component::getCurrentlyModalComponent());
        panel->setLookAndFeel(nullptr); panel.reset();
        CHECK(waitFor([] { return juce::Component::getCurrentlyModalComponent() == nullptr; }));
    }
    foh::theme::ThemeManager::get().setTheme("pro-dark");
}

TEST_CASE("Readout panel actions and layouts work in both appearances and scales", "[app][readouts][layout]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    auto& themes = foh::theme::ThemeManager::get();
    const auto originalTheme = themes.current().id;
    FohLookAndFeel look;
    AppModel model;
    model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    model.engine.gen = AudioEngine::Off;
    bool calibrationRequested = false;
    model.requestSplCalibration = [&] { calibrationRequested = true; };
    model.delay.arrivals = {{480, 10, 3.43, 0, true}, {960, 20, 6.86, -2, false}, {1440, 30, 10.29, -5, false}};
    model.delay.confidence = 0.82; model.delay.twoSimilarArrivals = true;
    foh::TFResult tf; tf.sampleRate = 48000; tf.fftSize = 1024;
    tf.freq = {0, 100, 1000, 10000}; tf.magDb.assign(4, 0); tf.phaseDeg.assign(4, 0); tf.coherence.assign(4, 1); tf.H.assign(4, {1, 0});
    model.tf = tf;
    REQUIRE(model.traces.store("Synthetic reference", tf, 3.1));
    for (bool sunlight : {false, true}) for (int scale : {100, 200}) {
        themes.setTheme(sunlight ? "pro-sunlight" : "pro-dark"); look.applyTheme();
        for (const auto* id : {"DelayFinder", "ShowMonitoring", "Coherence", "RTA", "SignalMeters", "SPLNumbers", "TraceList"}) {
            INFO(id << " sunlight=" << sunlight << " scale=" << scale);
            auto panel = PanelRegistry::standard().create(id, model); REQUIRE(panel);
            panel->setLookAndFeel(&look); auto minimum = panel->minSize(); panel->setSize(minimum.w, minimum.h);
            VisiblePanelWindow window(*panel);
            panel->modelChanged(AppModel::Theme | AppModel::Delay | AppModel::Traces | AppModel::Snapshot);
            for (auto* child : panel->getChildren()) if (child->isVisible()) CHECK(panel->getLocalBounds().contains(child->getBounds()));
            if (panel->panelId() == "SPLNumbers") {
                for (auto* child : panel->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*>(child)) b->onClick();
                CHECK(calibrationRequested);
            }
            if (panel->panelId() == "DelayFinder") {
                for (auto* child : panel->getChildren()) if (auto* viewport = dynamic_cast<juce::Viewport*>(child)) {
                    auto* detail = dynamic_cast<juce::Label*>(viewport->getViewedComponent()); REQUIRE(detail);
                    CHECK(detail->getText().contains("High confidence (82%)")); CHECK(detail->getText().contains("Polarity: inverted"));
                    CHECK(detail->getText().contains("\nArrival 2")); CHECK(detail->getText().contains("\nArrival 3"));
                }
            }
            if (panel->panelId() == "TraceList") {
                for (auto* child : panel->getChildren()) if (auto* list = dynamic_cast<juce::ListBox*>(child)) {
                    auto* row = list->getComponentForRowNumber(0); REQUIRE(row);
                    juce::ToggleButton* toggle = nullptr;
                    for (auto* c : row->getChildren()) if (auto* b = dynamic_cast<juce::ToggleButton*>(c)) toggle = b;
                    REQUIRE(toggle); const auto previous = model.traces.at(0).visible;
                    toggle->onClick(); CHECK(model.traces.at(0).visible != previous);
                    toggle->onClick(); CHECK(model.traces.at(0).visible == previous);
                }
            }
            if (const auto* folder = std::getenv("FOH_READOUT_QA_DIR")) {
                juce::File dir(folder); dir.createDirectory();
                auto snapshot = panel->createComponentSnapshot(panel->getLocalBounds(), true, scale / 100.f);
                juce::Image image(juce::Image::RGB, snapshot.getWidth(), snapshot.getHeight(), true);
                juce::Graphics g(image); g.fillAll(foh::ui::col::cardBackground()); g.drawImageAt(snapshot, 0, 0);
                auto file = dir.getChildFile(juce::String(id) + (sunlight ? "-sunlight-" : "-dark-") + juce::String(scale) + ".png").createOutputStream(); REQUIRE(file);
                file->setPosition(0); juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *file)); file->truncate();
            }
            panel->setLookAndFeel(nullptr);
        }
    }
    themes.setTheme(originalTheme);
}

TEST_CASE("Trace slots keep their colour identity and pattern through appearance changes", "[app][traces][readouts]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    auto& themes = foh::theme::ThemeManager::get(); const auto original = themes.current().id;
    themes.setTheme("pro-dark");
    AppModel model; REQUIRE(model.traces.store("Synthetic", {}, 0));
    model.colourTrace(0, foh::ui::col::storedTrace(3));
    CHECK(model.traceSlotIndex(0) == 3);
    themes.setTheme("pro-sunlight");
    CHECK(model.traceColour(0) == foh::ui::theme().traceSlots[3].colour);
    CHECK(foh::ui::theme().traceSlots[3].dashCount == 4);
    themes.setTheme(original);
}

TEST_CASE("Painted measurement widgets expose current values to accessibility clients", "[app][accessibility]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    Panel host(model, "AccessibilityTest", "Accessibility test"); host.setSize(1000, 800);
    VisiblePanelWindow window(host);
    PlotComponent plot; host.addAndMakeVisible(plot); plot.setBounds(0, 0, 600, 300);
    plot.yLabel = "Magnitude (dB)"; plot.centreNote = "Connect mic and reference";
    auto* handler = plot.getAccessibilityHandler(); REQUIRE(handler);
    REQUIRE(handler->getValueInterface());
    CHECK(handler->getValueInterface()->getCurrentValueAsString().contains("Magnitude (dB)"));
    CHECK(handler->getValueInterface()->getCurrentValueAsString().contains("Connect mic and reference"));
    plot.centreNote = "Measuring";
    CHECK(handler->getValueInterface()->getCurrentValueAsString().contains("Measuring"));
    CHECK(plot.getWantsKeyboardFocus()); CHECK(plot.hasFocusOutline());

    SegmentedControl tabs({"Alignment", "Tuning", "Show"});
    host.addAndMakeVisible(tabs); tabs.setBounds(0, 300, 600, 28);
    tabs.disabled.setBit(1); int selected = -1; tabs.onChange = [&](int i) { selected = i; };
    CHECK(tabs.keyPressed(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(selected == 2);
    CHECK(tabs.getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString() == "Show");
    CHECK(tabs.keyPressed(juce::KeyPress(juce::KeyPress::leftKey)));
    CHECK(selected == 0);

    StatusPills pills; host.addAndMakeVisible(pills); pills.setBounds(0, 340, 1000, 28);
    pills.setPills({{"Mic: No signal", StatusPills::Caution, 1}});
    juce::Component* pill = nullptr;
    for (auto* c : pills.getChildren()) if (c->getTitle() == "Mic: No signal") pill = c;
    REQUIRE(pill); auto* pillHandler = pill->getAccessibilityHandler();
    REQUIRE(pillHandler->getValueInterface());
    CHECK(pillHandler->getValueInterface()->getCurrentValueAsString().contains("Caution"));
    pills.setPills({{"Mic: -20 dBFS", StatusPills::Good, 1}});
    CHECK(pillHandler->getValueInterface()->getCurrentValueAsString().contains("-20 dBFS. Good"));

    auto meters = PanelRegistry::standard().create("SignalMeters", model);
    host.addAndMakeVisible(*meters); meters->setBounds(0, 380, 600, 160);
    REQUIRE(meters->getAccessibilityHandler()->getValueInterface());
    CHECK(meters->getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString().contains("Mic: No signal"));
    model.micPeak = 0.1f; model.refPeak = 0.01f; meters->modelChanged(AppModel::Levels);
    auto levels = meters->getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString();
    CHECK(levels.contains("Mic: -20.0 dBFS, Good"));
    CHECK(levels.contains("Reference: -40.0 dBFS, Good"));
    auto coherence = PanelRegistry::standard().create("Coherence", model);
    host.addAndMakeVisible(*coherence); coherence->setBounds(0, 550, 600, 200);
    auto text = coherence->getAccessibilityHandler()->getValueInterface()->getCurrentValueAsString();
    CHECK(text.contains("Average")); CHECK(text.contains("Poor below 0.6"));
}

TEST_CASE("Accessible panel tabs survive rebuild and retain keyboard focus", "[app][accessibility][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    fohws::Workspace workspace;
    workspace.root = fohws::H({{0.7, fohws::LayoutNode::tabs({"TransferFunction", "Coherence", "DelayFinder"})},
                               {0.3, fohws::L("Inspector")}});
    Panel host(model, "TabTest", "Tab test"); host.setSize(1200, 700);
    WorkspaceView view(model, PanelRegistry::standard()); host.addAndMakeVisible(view);
    view.setBounds(host.getLocalBounds()); view.setWorkspace(&workspace);
    VisiblePanelWindow window(host);
    std::function<juce::Button*(juce::Component&, const juce::String&)> findTab = [&](auto& c, const auto& title) -> juce::Button* {
        if (auto* button = dynamic_cast<juce::Button*>(&c); button && button->getTitle() == title && button->isShowing()) return button;
        for (auto* child : c.getChildren()) if (auto* found = findTab(*child, title)) return found;
        return nullptr;
    };
    auto* tab = findTab(view, "Coherence"); REQUIRE(tab);
    REQUIRE(tab->getAccessibilityHandler()); CHECK(tab->hasFocusOutline());
    juce::Component::SafePointer<juce::Button> oldTab(tab);
    CHECK(tab->getAccessibilityHandler()->getActions().invoke(juce::AccessibilityActionType::press));
    // JUCE's accessible press posts Button::triggerClick; invoke() only reports
    // that the action exists. Fresh macOS CTest processes may spend the first
    // dispatch slice on native window events before delivering that message.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000.0;
    while (oldTab != nullptr && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    CHECK(oldTab == nullptr); CHECK(view.focusedPanel() == "Coherence");
    auto* selected = findTab(view, "Coherence"); REQUIRE(selected); CHECK(selected->hasKeyboardFocus(false));
    REQUIRE(window.get().getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(view.focusedPanel() == "DelayFinder");
    selected = findTab(view, "Delay finder"); REQUIRE(selected); CHECK(selected->hasKeyboardFocus(false));
    juce::KeyboardFocusTraverser traverser;
    auto order = traverser.getAllComponents(&view);
    auto divider = std::find_if(order.begin(), order.end(), [](auto* c) { return c->getTitle() == "Panel width divider"; });
    REQUIRE(divider != order.end()); CHECK((*divider)->hasFocusOutline());
    const double previousWidth = workspace.root.sizes[0];
    (*divider)->grabKeyboardFocus();
    REQUIRE(window.get().getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::rightKey)));
    CHECK(workspace.root.sizes[0] > previousWidth);
}
