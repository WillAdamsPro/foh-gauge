#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "WorkspaceView.h"

namespace {
template<class T> T* find(juce::Component& root) {
    for (int i = 0; i < root.getNumChildComponents(); ++i) {
        auto* child = root.getChildComponent(i);
        if (auto* match = dynamic_cast<T*>(child)) return match;
        if (auto* match = find<T>(*child)) return match;
    }
    return nullptr;
}
fohws::Json imported(const char* name, const char* text) {
    auto state = fohws::Json::object();
    state.set("targetChoice", 4); state.set("targetName", name); state.set("targetText", text);
    state.set("targetShown", true); state.set("targetBand", true); state.set("targetOffset", 0);
    return state;
}
void paint(PlotComponent& plot) {
    juce::Image image(juce::Image::ARGB, 640, 320, true);
    juce::Graphics graphics(image);
    plot.setSize(640, 320); plot.paint(graphics);
}
void checkTarget(Panel& panel, bool shown, bool band) {
    auto* plot = find<PlotComponent>(panel); REQUIRE(plot);
    CHECK(plot->toleranceBands.size() == (shown && band ? 1 : 0));
    int targets = 0;
    for (const auto& series : plot->series) {
        if (series.label == "Live") continue;
        ++targets; REQUIRE(series.f.size() == series.v.size());
        for (auto f : series.f) CHECK(std::isfinite(f));
        for (auto v : series.v) CHECK(std::isfinite(v));
    }
    CHECK(targets == (shown ? 1 : 0));
    for (const auto& tolerance : plot->toleranceBands) {
        REQUIRE(tolerance.lower.size() == tolerance.upper.size());
        for (size_t i = 0; i < tolerance.lower.size(); ++i) {
            CHECK(std::isfinite(tolerance.lower[i])); CHECK(std::isfinite(tolerance.upper[i]));
            CHECK(tolerance.upper[i] - tolerance.lower[i] == Catch::Approx(6));
        }
    }
    paint(*plot);
}
}

TEST_CASE("Target restore refreshes hidden panels and replaces imported names", "[app][target]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    for (auto id : {"RTA", "TransferFunction"}) {
        auto panel = PanelRegistry::standard().create(id, model);
        panel->setSize(800, 500); panel->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        panel->setVisible(true);
        panel->loadState(imported("First synthetic", "20 0\n1000 1\n20000 0"));
        checkTarget(*panel, true, true);
        panel->setVisible(false);
        panel->loadState(imported("Second synthetic", "20 2\n1000 3\n20000 2"));
        CHECK(panel->saveState()["targetName"].asString() == "Second synthetic");
        panel->setVisible(true); // No audio snapshot is required to refresh targets.
        auto* plot = find<PlotComponent>(*panel); REQUIRE(plot);
        REQUIRE(plot->series.size() == 2);
        CHECK(plot->series.back().label == "Second synthetic");
        CHECK(plot->series.back().v.front() == 2);
        panel->setVisible(false);
        auto state = panel->saveState(); state.set("targetShown", false); panel->loadState(state);
        panel->setVisible(true); checkTarget(*panel, false, true);
    }
}

TEST_CASE("RTA and TF target controls survive rapid simultaneous toggles", "[app][target][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    auto rta = PanelRegistry::standard().create("RTA", model);
    auto tf = PanelRegistry::standard().create("TransferFunction", model);
    for (auto* panel : {rta.get(), tf.get()}) {
        panel->setSize(800, 500); panel->addToDesktop(juce::ComponentPeer::windowIsTemporary); panel->setVisible(true);
    }
    for (int iteration = 0; iteration < 128; ++iteration) {
        for (auto* panel : {rta.get(), tf.get()}) {
            auto state = imported("Synthetic", iteration % 2 ? "20 0\n1000 1\n20000 0" : "Tolerance: 0\nUseTolerance: 1\n20 0\n1000 1\n20000 0");
            panel->loadState(state);
            // Choose all three built-ins and the imported target through the real control.
            auto* toggle = find<juce::ToggleButton>(*panel); REQUIRE(toggle);
            auto* controls = toggle->getParentComponent();
            auto* choice = find<juce::ComboBox>(*controls); REQUIRE(choice);
            for (int id = 1; id <= 4; ++id) {
                choice->setSelectedId(id, juce::dontSendNotification); choice->onChange();
                for (int child = 0; child < controls->getNumChildComponents(); ++child)
                    if (auto* button = dynamic_cast<juce::ToggleButton*>(controls->getChildComponent(child))) {
                        button->setToggleState(true, juce::dontSendNotification); button->onClick();
                    }
                checkTarget(*panel, true, true);
                for (int child = 0; child < controls->getNumChildComponents(); ++child)
                    if (auto* button = dynamic_cast<juce::ToggleButton*>(controls->getChildComponent(child))) {
                        if (button == toggle) continue;
                        button->setToggleState(false, juce::dontSendNotification); button->onClick();
                        checkTarget(*panel, true, false);
                        button->setToggleState(true, juce::dontSendNotification); button->onClick();
                    }
                toggle->setToggleState(false, juce::dontSendNotification); toggle->onClick();
                checkTarget(*panel, false, true);
                toggle->setToggleState(true, juce::dontSendNotification); toggle->onClick();
            }
        }
        // Each panel keeps its own toggles; toggling TF cannot alter RTA.
        checkTarget(*rta, true, true); checkTarget(*tf, true, true);
    }
}

TEST_CASE("Target overlays refresh across workspace and Show mode transitions", "[app][target][workspace][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    WorkspaceView view(model, PanelRegistry::standard());
    view.setSize(1600, 1000); view.addToDesktop(juce::ComponentPeer::windowIsTemporary); view.setVisible(true);
    auto workspaces = fohws::builtInPresets();
    // A synthetic two-panel layout keeps both target panels visible together.
    for (int i : {1, 2}) {
        workspaces[(size_t)i].root = fohws::LayoutNode::split(true, {{0.5, fohws::LayoutNode::leaf("RTA")},
            {0.5, fohws::LayoutNode::leaf("TransferFunction")}});
        for (auto id : {"RTA", "TransferFunction"})
            workspaces[(size_t)i].panelState.set(id, imported(i == 1 ? "Workspace A" : "Workspace B",
                i == 1 ? "20 0\n1000 1\n20000 0" : "20 2\n1000 3\n20000 2"));
    }
    for (int i = 0; i < 64; ++i) {
        const int index = 1 + i % 2;
        view.setWorkspace(&workspaces[(size_t)index]);
        view.setShowMode(true); view.setShowMode(false);
        for (auto id : {"RTA", "TransferFunction"}) {
            auto* panel = view.panel(id, false); REQUIRE(panel); REQUIRE(panel->isShowing());
            checkTarget(*panel, true, true);
            CHECK(find<PlotComponent>(*panel)->series.back().label == (index == 1 ? "Workspace A" : "Workspace B"));
            CHECK(find<PlotComponent>(*panel)->series.back().v.front() == (index == 1 ? 0 : 2));
        }
    }
}

TEST_CASE("Targets coexist with stored guide and multi-mic overlays without accumulating", "[app][target][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    foh::TFResult trace; trace.fftSize = 1024; trace.sampleRate = 48000; trace.averages = 4;
    trace.magDb.assign(513, 0); trace.phaseDeg.assign(513, 0); trace.coherence.assign(513, 1);
    trace.H.assign(513, {1, 0}); trace.freq.resize(513);
    for (size_t i = 0; i < trace.freq.size(); ++i) trace.freq[i] = i * 48000.0 / 1024;
    model.tf = trace; model.multiPosition = trace; model.positionCount = 2;
    REQUIRE(model.traces.store("Synthetic stored", trace, 0));
    PlotComponent::Series guide; guide.f = {20, 1000, 20000}; guide.v = {0, 1, 0}; guide.label = "Synthetic guide";
    model.overlay.mag = {guide};
    auto panel = PanelRegistry::standard().create("TransferFunction", model);
    panel->setSize(800, 500); panel->addToDesktop(juce::ComponentPeer::windowIsTemporary); panel->setVisible(true);
    panel->loadState(imported("Synthetic target", "20 0\n1000 1\n20000 0"));
    auto* plot = find<PlotComponent>(*panel); REQUIRE(plot);
    for (int i = 0; i < 128; ++i) {
        model.overlay.active = i % 2 != 0;
        model.notify(AppModel::Overlay);
        REQUIRE(plot->series.size() == (model.overlay.active ? 3 : 4));
        CHECK(plot->series.front().label == (model.overlay.active ? "Synthetic guide" : "Synthetic stored"));
        CHECK(plot->series[plot->series.size() - 2].label == "Multi-mic average");
        CHECK(plot->series.back().label == "Synthetic target");
        CHECK(plot->toleranceBands.size() == 1);
        paint(*plot);
    }
}
