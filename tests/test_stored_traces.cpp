#include <catch2/catch_test_macros.hpp>
#include "ui_panel_window.h"
#include <catch2/catch_approx.hpp>
#include "Panels.h"
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif

namespace {
foh::TFResult syntheticTrace() {
    foh::TFResult t;
    t.freq = {0, 100, 1000, 10000};
    t.H.assign(4, {1, 0}); t.magDb.assign(4, 0);
    t.phaseDeg.assign(4, 0); t.coherence.assign(4, 1);
    return t;
}
template<class T> std::vector<T*> children(Panel& panel) {
    std::vector<T*> result;
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* c = dynamic_cast<T*>(panel.getChildComponent(i))) result.push_back(c);
    return result;
}
}

TEST_CASE("Stored trace edits preserve measurements and sum source identity", "[app][traces]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    auto tf = syntheticTrace();
    for (auto name : {"First", "Second", "Third"}) REQUIRE(model.traces.store(name, tf, 3));
    model.renameTrace(1, " Renamed "); model.colourTrace(1, juce::Colours::cyan); model.toggleTrace(1);
    CHECK(model.traces.at(1).name == "Renamed");
    CHECK(model.traceColour(1) == juce::Colours::cyan);
    CHECK_FALSE(model.traces.at(1).visible);
    CHECK(model.traces.at(1).tf.H == tf.H);
    CHECK(model.traces.at(1).refDelayMs == 3);
    model.renameTrace(1, "  "); CHECK(model.traces.at(1).name == "Renamed");
    model.selectSum(1, 1);
    auto sum = model.sumPreview();
    REQUIRE(sum.magDb.size() == 4);
    CHECK(sum.magDb[2] == Catch::Approx(6.02).margin(0.01));
    CHECK(sum.phaseDeg[2] == Catch::Approx(0));
    model.removeTrace(0);
    CHECK(model.sumA == 0); CHECK(model.sumB == 0);
    CHECK(model.traceColour(0) == juce::Colours::cyan);
    CHECK(model.sumPreview().magDb[2] == Catch::Approx(6.02).margin(0.01));
    model.removeTrace(0);
    CHECK(model.sumPreview().freq.empty()); CHECK(model.sumA == -1);
    model.toggleTrace(-1); model.renameTrace(20, "Invalid"); model.removeTrace(20);
    CHECK(model.traces.count() == 1);
    model.clearTraces(); CHECK(model.traces.count() == 0); CHECK(model.sumB == -1);
}

TEST_CASE("Virtual sum preview rejects incompatible or incomplete stored data", "[app][traces]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    auto tf = syntheticTrace();
    REQUIRE(model.traces.store("A", tf, 0)); REQUIRE(model.traces.store("B", tf, 0));
    model.selectSum(0, 1);
    model.traces.at(1).tf.freq[2] = 2000;
    CHECK(model.sumPreview().freq.empty());
    model.traces.at(1).tf = tf; model.traces.at(1).tf.phaseDeg.clear();
    CHECK(model.sumPreview().freq.empty());
    model.selectSum(-1, 0); CHECK(model.sumPreview().freq.empty());
}

TEST_CASE("Trace manager controls edit traces and display a +6.02 dB self sum", "[app][traces][ui]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    REQUIRE(model.traces.store("Synthetic", syntheticTrace(), 0));
    auto panel = PanelRegistry::standard().create("TraceList", model);
    panel->setSize(300, 420);
    auto lists = children<juce::ListBox>(*panel); REQUIRE(lists.size() == 1);
    lists[0]->selectRow(0);
    auto editors = children<juce::TextEditor>(*panel); REQUIRE(editors.size() == 1);
    editors[0]->setText("Edited"); editors[0]->onReturnKey();
    CHECK(model.traces.at(0).name == "Edited");
    auto combos = children<juce::ComboBox>(*panel); REQUIRE(combos.size() == 3);
    combos[0]->setSelectedId(1, juce::dontSendNotification); combos[0]->onChange();
    CHECK(model.traceColour(0) == juce::Colour::fromHSV(0, 0.7f, 0.9f, 1));
    auto magnitude = PanelRegistry::standard().create("TransferFunction", model);
    auto phase = PanelRegistry::standard().create("Phase", model);
    // Plot panels refresh only while showing, as they do in a workspace.
    magnitude->setSize(600, 300); phase->setSize(600, 300);
    VisiblePanelWindow magnitudeWindow(*magnitude), phaseWindow(*phase);
    model.notify(AppModel::Traces);
    for (auto* chart : {magnitude.get(), phase.get()}) {
        auto chartPlots = children<PlotComponent>(*chart); REQUIRE(chartPlots.size() == 1);
        REQUIRE(chartPlots[0]->series.size() == 2);
        CHECK(chartPlots[0]->series[0].label == "Edited");
        CHECK(chartPlots[0]->series[0].c == model.traceColour(0));
    }
    model.toggleTrace(0);
    CHECK(children<PlotComponent>(*magnitude)[0]->series.size() == 1);
    CHECK(children<PlotComponent>(*phase)[0]->series.size() == 1);
    model.toggleTrace(0);
    CHECK(children<PlotComponent>(*magnitude)[0]->series.size() == 2);
    CHECK(children<PlotComponent>(*phase)[0]->series.size() == 2);
    combos[1]->setSelectedId(1, juce::dontSendNotification); combos[1]->onChange();
    combos[2]->setSelectedId(1, juce::dontSendNotification); combos[2]->onChange();
    auto plots = children<PlotComponent>(*panel); REQUIRE(plots.size() == 1);
    REQUIRE(plots[0]->series.size() == 1);
    CHECK(plots[0]->note == "1000 Hz: +6.02 dB");
    for (int i = 0; i < panel->getNumChildComponents(); ++i)
        CHECK(panel->getLocalBounds().contains(panel->getChildComponent(i)->getBounds()));
    auto buttons = children<juce::TextButton>(*panel); REQUIRE(buttons.size() == 2);
    buttons[1]->onClick();
    CHECK(model.traces.count() == 0); CHECK(plots[0]->series.empty());
    CHECK_FALSE(editors[0]->isEnabled());
}

TEST_CASE("Damaged saved targets clear plot overlays and tolerance bands", "[app][files][target]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    // Standalone panels need the same alert factory as the main app shell.
    struct AppLookAndFeel {
        juce::LookAndFeel* previous = &juce::LookAndFeel::getDefaultLookAndFeel();
        FohLookAndFeel theme;
        AppLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel(&theme); }
        ~AppLookAndFeel() { juce::LookAndFeel::setDefaultLookAndFeel(previous); }
    } lookAndFeel;
    // Observe and dismiss each asynchronous error notice before panel teardown.
    struct DismissErrors : juce::Timer {
        int notices = 0;
        DismissErrors() { startTimer(10); }
        void timerCallback() override {
            auto* modal = juce::ModalComponentManager::getInstance()->getModalComponent(0);
            if (auto* alert = dynamic_cast<juce::AlertWindow*>(modal)) {
                ++notices;
#if JUCE_MAC
                auto* peer = alert->getPeer();
                CHECK(peer != nullptr);
                if (peer) {
                    NSWindow* native = [(NSView*) peer->getNativeHandle() window];
                    CHECK(native != nil);
                    CHECK(native.animationBehavior == NSWindowAnimationBehaviorNone);
                }
#endif
                alert->exitModalState(0);
            }
        }
    } dismissErrors;
    AppModel model;
    for (auto id : {"TransferFunction", "RTA"}) {
        auto panel = PanelRegistry::standard().create(id, model); REQUIRE(panel);
        panel->setSize(600, 300);
        VisiblePanelWindow window(*panel);
        auto state = foh::Json::object(); state.set("targetChoice", 4);
        state.set("targetText", "20 0\n1000 1"); state.set("targetShown", true); state.set("targetBand", true);
        panel->loadState(state);
        auto plots = children<PlotComponent>(*panel); REQUIRE(plots.size() == 1);
        REQUIRE_FALSE(plots[0]->toleranceBands.empty());
        for (const auto* bad : {"", "20 0\n1000 1\n2000 2e", "20 0\n1000 nan"}) {
            state.set("targetText", bad); panel->loadState(state);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
            CHECK(plots[0]->toleranceBands.empty());
            CHECK_FALSE(panel->saveState()["targetShown"].asBool());
            CHECK(panel->saveState()["targetChoice"].asInt() == 1);
            CHECK_FALSE(panel->saveState().has("targetText"));
            for (const auto& series : plots[0]->series) CHECK(series.label != "Imported target");
            state.set("targetText", "20 0\n1000 1"); panel->loadState(state);
            CHECK_FALSE(plots[0]->toleranceBands.empty());
        }
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    CHECK(dismissErrors.notices == 6);
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
}
