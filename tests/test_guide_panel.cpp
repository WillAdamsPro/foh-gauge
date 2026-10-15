#include <catch2/catch_test_macros.hpp>
#include "Panels.h"
#include "Diagrams.h"
#include "FloatingPanels.h"
#include "WorkspaceView.h"
#include <stdexcept>
#include "ui_form_helpers.h"

std::unique_ptr<Panel> makeGuidePanel(AppModel&);

// Isolated UI model: no devices, analysis worker, disk writes or model timers.
// The guide must only present instructions, never apply hardware changes.
AppModel::AppModel() {
    juce::PropertiesFile::Options options;
    options.applicationName = "FOH synthetic tests " + juce::Uuid().toString();
    options.filenameSuffix = "settings";
    options.osxLibrarySubFolder = "Application Support";
    options.doNotSave = true;
    settings.setStorageParameters(options);
    settings.getUserSettings()->setValue("setupComplete", true);
}
AppModel::~AppModel() = default;
bool AppModel::calibrateSpl(double) { throw std::logic_error("Unexpected SPL calibration"); }
bool AppModel::micLevelReady() const {
    // Match the production readiness gate so synthetic devices can exercise live readouts.
    return micLevelValid && !paused && dm.getCurrentAudioDevice() && levelInputKey == splInputKey()
        && levelInputEpoch == engine.captureEpoch.load() && juce::Time::getMillisecondCounterHiRes() - levelTimeMs < 250;
}
void AppModel::setCalEnabled(bool on) { calOn = on && cal.ok(); notify(Cal); }
std::string AppModel::loopbackKey() const { return "synthetic-loopback"; }
bool AppModel::beginLoopback() { throw std::logic_error("Unexpected interface measurement"); }
void AppModel::clearLoopback() { throw std::logic_error("Unexpected interface calibration"); }
void AppModel::clearSpl() { throw std::logic_error("Unexpected SPL calibration"); }
foh::Json AppModel::splCalJson() const { return foh::Json::object(); }
static std::function<void()> guideDelay;
void AppModel::requestDelay(bool, std::function<void()> then) { guideDelay = std::move(then); }
void AppModel::lockDelayFromLast() { throw std::logic_error("Unexpected delay lock"); }
static std::function<void(AppModel&, const juce::String&, std::function<void(bool)>)> guideCapture;
void AppModel::beginCapture(const juce::String& name, std::function<void(bool)> then) {
    if (!guideCapture) throw std::logic_error("Unexpected capture");
    guideCapture(*this, name, std::move(then));
}
bool AppModel::storeTrace(const juce::String&) { throw std::logic_error("Unexpected capture"); }
const foh::Trace* AppModel::findTrace(const char* name) const {
    for (int i = traces.count() - 1; i >= 0; --i)
        if (traces.at(i).name == name) return &traces.at(i);
    return nullptr;
}

namespace {
void acceptGeneratorConfirmation() {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    auto* modals = juce::ModalComponentManager::getInstance();
    if (auto* alert = dynamic_cast<juce::AlertWindow*>(modals->getModalComponent(0))) {
        if (auto* start = alert->getButton("Start noise")) {
            start->triggerClick();
            juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        }
    }
}
fohws::Json findDelayStep() {
    auto state = fohws::Json::object(); state.set("guide", 1); state.set("step", 2); return state;
}
std::vector<juce::ToggleButton*> choices(Panel& panel) {
    std::vector<juce::ToggleButton*> rows;
    for (int i = 0; i < formCanvas(panel).getNumChildComponents(); ++i)
        if (auto* row = dynamic_cast<juce::ToggleButton*>(formCanvas(panel).getChildComponent(i))) rows.push_back(row);
    return rows;
}
bool labelContains(Panel& panel, const juce::String& text) {
    for (int i = 0; i < formCanvas(panel).getNumChildComponents(); ++i)
        if (auto* label = dynamic_cast<juce::Label*>(formCanvas(panel).getChildComponent(i)))
            // Instruction assertions also inspect copy available through More.
            if ((label->isVisible() || label->getComponentID() == "guide-details") && label->getText().contains(text)) return true;
    return false;
}
void storeSyntheticPair(AppModel& model) {
    foh::TFResult main, sub;
    // Equal-level overlap only at 80-120 Hz, with a coherent centre of 100 Hz.
    // Exact synthetic phase gives a 12.5 ms sub delay independently of the raw peak.
    for (double hz : {0.0, 80.0, 90.0, 100.0, 110.0, 120.0, 200.0}) {
        main.freq.push_back(hz); sub.freq.push_back(hz);
        main.H.push_back({1, 0}); sub.H.push_back(std::polar(1.0, -2 * juce::MathConstants<double>::pi * hz * 0.0125));
        main.magDb.push_back(0); sub.magDb.push_back(hz <= 120 ? 0 : -20);
        main.coherence.push_back(1); sub.coherence.push_back(1);
    }
    REQUIRE(model.traces.store("Main", main, 0));
    REQUIRE(model.traces.store("Sub", sub, 0));
    model.delay.confidence = 1;
    model.delay.arrivals.push_back({984, 20.5, 0, 0, true});
    model.delayWasSub = true;
}
}

TEST_CASE("W2 presents selectable 12.5 ms primary and 2.5/22.5 ms alternatives at 100 Hz", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    AppModel model;
    storeSyntheticPair(model);
    auto panel = makeGuidePanel(model);
    panel->setSize(260, 660);
    panel->loadState(findDelayStep());
    auto rows = choices(*panel);
    REQUIRE(rows.size() == 4);
    CHECK(rows[0]->getButtonText() == "Primary: Mains +12.50 ms");
    CHECK(rows[1]->getButtonText() == "One cycle earlier: Mains +2.50 ms");
    CHECK(rows[2]->getButtonText() == "One cycle later: Mains +22.50 ms");
    CHECK(rows[3]->getButtonText().contains("Mains +7.50 ms"));
    CHECK(rows[0]->getToggleState());
    CHECK(rows[0]->isVisible());
    for (size_t i = 1; i < rows.size(); ++i) CHECK_FALSE(rows[i]->isVisible());
    CHECK(labelContains(*panel, "Best sum through the crossover"));
    for (size_t selected = 0; selected < rows.size(); ++selected) {
        panel->loadState(findDelayStep());
        CHECK(rows[0]->getToggleState());
        for (auto* child : formCanvas(*panel).getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(child))
                if (b->getButtonText() == juce::String::fromUTF8("Other options ▸")) b->onClick();
        CHECK(labelContains(*panel, "Raw impulse peak: 20.50 ms (hint only)"));
        rows[selected]->setToggleState(true, juce::sendNotification);
        for (size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i]->isVisible()); CHECK(rows[i]->isEnabled());
            CHECK(rows[i]->getToggleState() == (i == selected));
            CHECK(formCanvas(*panel).getLocalBounds().contains(rows[i]->getBounds()));
        }
        // Resizing must preserve the selected option.
        panel->setSize(260, 661 + (int) selected);
        juce::TextButton* use = nullptr;
        for (int i = 0; i < formCanvas(*panel).getNumChildComponents(); ++i)
            if (auto* button = dynamic_cast<juce::TextButton*>(formCanvas(*panel).getChildComponent(i)))
                if (button->getButtonText() == "Lock delay") use = button;
        REQUIRE(use != nullptr);
        use->onClick();
        CHECK(panel->saveState()["step"].asInt() == 3);
        const char* amounts[] = {"12.50", "2.50", "22.50", "7.50"};
        CHECK(labelContains(*panel, juce::String("mains ") + amounts[selected] + " ms"));
        CHECK(labelContains(*panel, "Flip sub polarity") == (selected == 3));
        for (auto* row : rows) CHECK_FALSE(row->isVisible());
    }
}

TEST_CASE("W2 disables delay choices without captured traces", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    AppModel model;
    auto panel = makeGuidePanel(model);
    panel->setSize(260, 660);
    panel->loadState(findDelayStep());
    for (auto* row : choices(*panel)) CHECK_FALSE(row->isEnabled());
    CHECK(labelContains(*panel, "Store mains and sub traces first"));
}

namespace {
juce::TextButton& button(Panel& panel, const char* text) {
    for (int i = 0; i < formCanvas(panel).getNumChildComponents(); ++i)
        if (auto* b = dynamic_cast<juce::TextButton*>(formCanvas(panel).getChildComponent(i)))
            if (b->getButtonText() == text) return *b;
    throw std::logic_error("Missing guide button");
}
juce::TextEditor& noteEditor(Panel& panel) {
    for (int i = 0; i < formCanvas(panel).getNumChildComponents(); ++i)
        if (auto* e = dynamic_cast<juce::TextEditor*>(formCanvas(panel).getChildComponent(i))) return *e;
    throw std::logic_error("Missing verification note");
}
struct CaptureStub {
    bool settled = true;
    int calls = 0;
    std::function<void(bool)> pending;
    juce::String name;
    CaptureStub() {
        guideCapture = [this](AppModel&, const juce::String& n, std::function<void(bool)> then) {
            ++calls; name = n; pending = std::move(then);
        };
    }
    void finish(AppModel& m, double magnitude = 0) {
        foh::TFResult tf;
        tf.freq = {100, 1000}; tf.magDb = {magnitude, magnitude}; tf.phaseDeg = {0, 0}; tf.coherence = {1, 1};
        REQUIRE(m.traces.store(name.toStdString(), tf, m.refDelayMs));
        auto then = std::move(pending); then(settled);
    }
    ~CaptureStub() { guideCapture = {}; }
};
}

TEST_CASE("Every W1-W5 frame requires manual Apply and ends in fresh Verify with a verdict note", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int guide = 0; guide < 5; ++guide) {
        CAPTURE(guide);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 660);
        auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 100);
        // Restoring Apply/Verify must restart at Store, since the baseline is not restored.
        panel->loadState(state);
        const int beforeStep = panel->saveState()["step"].asInt();
        button(*panel, "Measure before change").onClick();
        CHECK_FALSE(button(*panel, "Measure before change").isEnabled());
        capture.finish(model);
        CHECK(panel->saveState()["step"].asInt() == beforeStep + 1);
        CHECK(labelContains(*panel, "FOH Gauge never applies it"));
        CHECK(capture.calls == 1); // Apply only acknowledges a user's external DSP change.
        button(*panel, guide == 4 ? "DSP state confirmed" : "I entered the change").onClick();
        CHECK(panel->saveState()["step"].asInt() == beforeStep + 2);
        CHECK(labelContains(*panel, "Verify against the stored trace"));
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        CHECK_FALSE(button(*panel, "Fail").isEnabled());
        button(*panel, "Re-measure to verify").onClick();
        CHECK(capture.calls == 2);
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        capture.finish(model, guide == 4 ? 0 : 3);
        CHECK(labelContains(*panel, guide == 4 ? "0.00 dB across 2 coherent bins" : "3.00 dB across 2 coherent bins"));
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        noteEditor(*panel).setText("Improved overlap", false); noteEditor(*panel).onTextChange();
        REQUIRE(button(*panel, "Pass").isEnabled());
        button(*panel, "Pass").onClick(); CHECK(labelContains(*panel, "Pass: Improved overlap"));
        button(*panel, "Fail").onClick(); CHECK(labelContains(*panel, "Fail: Improved overlap"));
        CHECK(panel->saveState()["verificationNote"].asString() == "Fail: Improved overlap");
        CHECK(panel->saveState()["step"].asInt() == beforeStep + 2); // Verify is the final step.
        button(*panel, "Re-measure to verify").onClick();
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        capture.settled = false; capture.finish(model);
        CHECK_FALSE(button(*panel, "Fail").isEnabled());
        CHECK(labelContains(*panel, "No settled measurement"));
        CHECK(model.engine.gen.load() == AudioEngine::Off);
        CHECK(model.lockedDelayMs == 0);
    }
}

TEST_CASE("Verify rejects reference-delay changes and stale completion after guide navigation", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 660);
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100); panel->loadState(state);
    const int storeStep = panel->saveState()["step"].asInt();
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    button(*panel, "I entered the change").onClick();
    button(*panel, "Re-measure to verify").onClick(); model.refDelayMs = 5; capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == storeStep);
    CHECK(labelContains(*panel, "Guide measurement interrupted"));
    button(*panel, "Measure before change").onClick();
    state.set("guide", 4); state.set("step", 0); panel->loadState(state); capture.finish(model);
    CHECK(panel->saveState()["guide"].asInt() == 4);
    CHECK(panel->saveState()["step"].asInt() == 0);
}

TEST_CASE("Pending Store and Verify completions expire when a guide is destroyed", "[app][guide][lifetime]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int guide = 0; guide < 5; ++guide) {
        for (bool verify : {false, true}) {
            AppModel model; CaptureStub capture;
            auto panel = makeGuidePanel(model); panel->setSize(260, 780);
            auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 100);
            panel->loadState(state);
            button(*panel, "Measure before change").onClick();
            if (verify) {
                capture.finish(model);
                button(*panel, guide == 4 ? "DSP state confirmed" : "I entered the change").onClick();
                button(*panel, "Re-measure to verify").onClick();
            }
            REQUIRE(capture.pending);
            panel.reset();
            CHECK_FALSE(model.overlayBuilder);
            // A replacement guide must remain untouched by its predecessor's callback.
            auto replacement = makeGuidePanel(model);
            const auto replacementState = replacement->saveState().dump();
            capture.finish(model);
            CHECK(replacement->saveState().dump() == replacementState);
            CHECK(model.engine.gen.load() == AudioEngine::Off);
        }
    }
}

TEST_CASE("Guide requires a settled baseline and rejects captures with no comparable coherent bins", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 660);
    auto state = fohws::Json::object(); state.set("guide", 4); state.set("step", 100); panel->loadState(state);
    const int storeStep = panel->saveState()["step"].asInt();
    capture.settled = false;
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == storeStep);
    CHECK(labelContains(*panel, "No settled measurement"));
    capture.settled = true;
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    button(*panel, "DSP state confirmed").onClick();
    button(*panel, "Re-measure to verify").onClick();
    foh::TFResult lowCoherence;
    lowCoherence.freq = {100, 1000}; lowCoherence.magDb = {0, 0}; lowCoherence.coherence = {0.1, 0.1};
    REQUIRE(model.traces.store(capture.name.toStdString(), lowCoherence, 0));
    auto then = std::move(capture.pending); then(true);
    CHECK(labelContains(*panel, "No comparable coherent bins"));
    noteEditor(*panel).setText("Cannot judge", false); noteEditor(*panel).onTextChange();
    CHECK_FALSE(button(*panel, "Pass").isEnabled());
    CHECK_FALSE(button(*panel, "Fail").isEnabled());
    for (int i = 0; i < formCanvas(*panel).getNumChildComponents(); ++i) {
        auto* child = formCanvas(*panel).getChildComponent(i);
        if (child->isVisible()) CHECK(formCanvas(*panel).getLocalBounds().contains(child->getBounds()));
    }
}

TEST_CASE("W4 and W5 checklists advance manually without changing audio settings", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI gui;
    const char* actions[2][3] = {
        {"Calibration checked", "Alignment checked", "Ready to store"},
        {"Signal path checked", "Response checked", "Ready to verify"}
    };
    const char* instructions[2][3] = {
        {"Calibrate SPL first", "Align before EQ", "Tune one change at a time"},
        {"Five-minute check: signal path", "Check response and timing", "Symptoms: likely causes"}
    };
    for (int guide = 3; guide <= 4; ++guide) {
        CAPTURE(guide);
        AppModel model; CaptureStub capture;
        model.refDelayMs = model.lockedDelayMs = 4.8;
        auto panel = makeGuidePanel(model); panel->setSize(260, 660);
        auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 0);
        panel->loadState(state);
        for (int step = 0; step < 3; ++step) {
            CAPTURE(step);
            CHECK(labelContains(*panel, instructions[guide - 3][step]));
            for (int i = 0; i < formCanvas(*panel).getNumChildComponents(); ++i) {
                auto* child = formCanvas(*panel).getChildComponent(i);
                if (child->isVisible()) CHECK(formCanvas(*panel).getLocalBounds().contains(child->getBounds()));
                if (auto* label = dynamic_cast<juce::Label*>(child)) {
                    if (label->isVisible() && label->getText().length() > 150) {
                        juce::GlyphArrangement layout;
                        layout.addJustifiedText(label->getFont(), label->getText(), 0, label->getFont().getAscent(),
                            (float) (label->getWidth() - 10), juce::Justification::left);
                        CHECK(layout.getBoundingBox(0, -1, true).getBottom() <= label->getHeight() - 2);
                    }
                }
            }
            auto& advance = button(*panel, actions[guide - 3][step]);
            REQUIRE(advance.isEnabled()); advance.onClick();
            CHECK(panel->saveState()["step"].asInt() == step + 1);
            CHECK(capture.calls == 0);
            CHECK(model.engine.gen.load() == AudioEngine::Off);
            CHECK(model.refDelayMs == 4.8);
            CHECK(model.lockedDelayMs == 4.8);
        }
        REQUIRE(button(*panel, "Measure before change").isEnabled());
        button(*panel, "Back").onClick();
        CHECK(panel->saveState()["step"].asInt() == 2);
        CHECK(labelContains(*panel, instructions[guide - 3][2]));
    }
}

namespace {
MicPlacementDiagram& diagram(Panel& panel) {
    for (int i = 0; i < formCanvas(panel).getNumChildComponents(); ++i)
        if (auto* d = dynamic_cast<MicPlacementDiagram*>(formCanvas(panel).getChildComponent(i))) return *d;
    throw std::logic_error("Missing mic placement diagram");
}
}

TEST_CASE("Mic moves in W1-W3 show the appropriate vector diagram", "[app][guide][diagrams]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    auto panel = makeGuidePanel(model);
    using P = MicPlacementDiagram::Placement;
    const P expected[] = {P::OnAxis, P::SubMainCrossover, P::Seam};
    for (int guide = 0; guide < 5; ++guide) {
        CAPTURE(guide);
        auto state = fohws::Json::object(); state.set("guide", guide);
        state.set("step", guide == 0 ? 1 : 0);
        panel->loadState(state);
        auto& d = diagram(*panel);
        CHECK(d.isVisible() == (guide < 3));
        CHECK(d.getPlacement() == (guide < 3 ? expected[guide] : P::None));
        for (int width : {260, 400}) {
            panel->setSize(width, panel->minSize().h);
            if (d.isVisible()) {
                CHECK(formCanvas(*panel).getLocalBounds().contains(d.getBounds()));
                CHECK(d.getWidth() > 0);
                CHECK(d.getHeight() == 104);
                CHECK(d.getDescription().contains("Mic"));
                // Exercise the paint path at both narrow and expanded inspector widths.
                auto image = d.createComponentSnapshot(d.getLocalBounds());
                CHECK(image.isValid());
            }
            for (int i = 0; i < formCanvas(*panel).getNumChildComponents(); ++i) {
                auto* child = formCanvas(*panel).getChildComponent(i);
                if (child->isVisible()) CHECK(formCanvas(*panel).getLocalBounds().contains(child->getBounds()));
            }
        }
    }
    // Connect prescribes routing only; switching back must hide the old diagram.
    auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 0);
    panel->loadState(state);
    CHECK_FALSE(diagram(*panel).isVisible());
}

TEST_CASE("Placement stays fixed through Store Apply and Verify", "[app][guide][diagrams]") {
    juce::ScopedJuceInitialiser_GUI gui;
    using P = MicPlacementDiagram::Placement;
    const P expected[] = {P::OnAxis, P::SubMainCrossover, P::Seam};
    for (int guide = 0; guide < 3; ++guide) {
        CAPTURE(guide);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, panel->minSize().h);
        auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 100);
        panel->loadState(state);
        auto checkDiagram = [&] {
            auto& d = diagram(*panel);
            CHECK(d.isVisible()); CHECK(d.getPlacement() == expected[guide]);
            CHECK(formCanvas(*panel).getLocalBounds().contains(d.getBounds()));
            for (int i = 0; i < formCanvas(*panel).getNumChildComponents(); ++i) {
                auto* child = formCanvas(*panel).getChildComponent(i);
                if (child->isVisible() && child != &d) CHECK_FALSE(d.getBounds().intersects(child->getBounds()));
            }
        };
        checkDiagram();
        button(*panel, "Measure before change").onClick(); capture.finish(model); checkDiagram();
        button(*panel, "I entered the change").onClick(); checkDiagram();
        button(*panel, "Re-measure to verify").onClick(); capture.finish(model); checkDiagram();
    }
}

TEST_CASE("Hotplug during guided Verify invalidates the baseline and verdict", "[app][guide][hotplug]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 660);
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100); panel->loadState(state);
    const int storeStep = panel->saveState()["step"].asInt();
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    button(*panel, "I entered the change").onClick();
    button(*panel, "Re-measure to verify").onClick();
    model.engine.audioDeviceStopped();
    model.notify(AppModel::Capture | AppModel::Settings);
    CHECK(panel->saveState()["step"].asInt() == storeStep);
    CHECK(labelContains(*panel, "Setup"));
    // Even a delayed successful completion cannot restore the invalid verdict.
    capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == storeStep);
    CHECK_FALSE(button(*panel, "Pass").isEnabled());
    CHECK_FALSE(button(*panel, "Fail").isEnabled());
}

TEST_CASE("W6 checks each isolated box with fresh sweep impulse direction", "[app][guide][polarity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    auto panel = makeGuidePanel(model); panel->setSize(260, panel->minSize().h);
    auto state = fohws::Json::object(); state.set("guide", 5); state.set("step", 0);
    panel->loadState(state);
    REQUIRE(panel->saveState()["guide"].asInt() == 5);
    CHECK(labelContains(*panel, "Mute every other box"));
    button(*panel, "Box isolated").onClick();
    // A previous box's result must never be accepted as a fresh measurement.
    model.sweep.response.impulse = {0, 1, 0};
    model.sweep.response.arrivals.push_back({});
    auto measure = [&](const char* action, double sign) {
        button(*panel, action).onClick(); acceptGeneratorConfirmation();
        CHECK_FALSE(button(*panel, action).isEnabled());
        model.notify(AppModel::Snapshot);
        CHECK_FALSE(button(*panel, action).isEnabled());
        model.sweepSeq = model.nextGuideSweepId;
        model.sweepCapturing = true; model.notify(AppModel::Snapshot);
        model.sweepCapturing = false;
        model.sweep.response.impulse = {0, sign, -0.2 * sign};
        model.notify(AppModel::Snapshot);
    };
    measure("Measure box sweep", -1);
    CHECK(labelContains(*panel, "Inverted (negative)"));
    REQUIRE(panel->saveState()["step"].asInt() == 2);
    CHECK(model.engine.gen.load() == AudioEngine::Off);
    button(*panel, "Polarity state confirmed").onClick();
    CHECK_FALSE(button(*panel, "Pass").isEnabled());
    measure("Verify box sweep", 1);
    CHECK(labelContains(*panel, "Before: Inverted (negative); now: Normal (positive)"));
    noteEditor(*panel).setText("Matches expected driver direction", false);
    noteEditor(*panel).onTextChange();
    REQUIRE(button(*panel, "Pass").isEnabled());
    button(*panel, "Pass").onClick();
    CHECK(panel->saveState()["step"].asInt() == 4);
    button(*panel, "Check next box").onClick();
    CHECK(panel->saveState()["step"].asInt() == 0);
    button(*panel, "Box isolated").onClick();
    measure("Measure box sweep", 0);
    CHECK(panel->saveState()["step"].asInt() == 1);
    CHECK(labelContains(*panel, "Inconclusive impulse"));
    CHECK(button(*panel, "Measure box sweep").isEnabled());
    measure("Measure box sweep", 1);
    CHECK(labelContains(*panel, "This box: Normal (positive)"));
    button(*panel, "Polarity state confirmed").onClick();
    model.engine.captureEpoch.fetch_add(1);
    button(*panel, "Verify box sweep").onClick(); acceptGeneratorConfirmation();
    CHECK(panel->saveState()["step"].asInt() == 1);
    CHECK(labelContains(*panel, "Audio routing changed"));
    // Restoring Apply/Verify cannot reuse a non-persisted baseline.
    state.set("step", 3); panel->loadState(state);
    CHECK(panel->saveState()["step"].asInt() == 1);
    for (auto* child : formCanvas(*panel).getChildren())
        if (child->isVisible()) CHECK(formCanvas(*panel).getLocalBounds().contains(child->getBounds()));
}

TEST_CASE("All guide steps retain readable content under window and scale stress", "[app][guide][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    storeSyntheticPair(model);
    auto panel = makeGuidePanel(model);
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f, 2.f}) {
        panel->setTransform(juce::AffineTransform::scale(scale));
        for (auto size : {juce::Point<int>(0, 0), {1, 1}, {120, 90}, {260, 660}, {4000, 3000}}) {
            panel->setSize((int) (size.x / scale), (int) (size.y / scale));
            for (int guide = 0; guide < 5; ++guide) {
                auto state = fohws::Json::object(); state.set("guide", guide);
                for (int step = 0; step < (guide == 0 ? 7 : 6); ++step) {
                    CAPTURE(scale, size.x, size.y, guide, step);
                    state.set("step", step); panel->loadState(state);
                    auto& canvas = formCanvas(*panel);
                    for (auto* child : canvas.getChildren()) {
                        if (!child->isVisible()) continue;
                        CHECK(child->getWidth() > 0); CHECK(child->getHeight() > 0);
                        CHECK(canvas.getLocalBounds().contains(child->getBounds()));
                        if (auto* label = dynamic_cast<juce::Label*>(child)) {
                            juce::GlyphArrangement text;
                            text.addJustifiedText(label->getFont(), label->getText(), 0, label->getFont().getAscent(),
                                (float) juce::jmax(1, label->getWidth() - 10), juce::Justification::left);
                            CHECK(text.getBoundingBox(0, -1, true).getHeight() <= label->getHeight());
                        }
                    }
                    // Painting the canvas exercises guide chrome even when the host is empty.
                    if (scale == 2.f && size.x == 120) CHECK(canvas.createComponentSnapshot(canvas.getLocalBounds()).isValid());
                }
            }
        }
    }
}

TEST_CASE("Guide destruction expires captures and queued actions on repeated reopen", "[app][guide][lifetime][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    for (int i = 0; i < 30; ++i) {
        auto panel = makeGuidePanel(model);
        auto state = fohws::Json::object(); state.set("guide", i % 5); state.set("step", 100);
        panel->loadState(state);
        button(*panel, "Measure before change").onClick();
        button(*panel, "Back").triggerClick();
        panel.reset();
        CHECK_FALSE(model.overlayBuilder);
        REQUIRE(capture.pending);
        capture.finish(model);
        model.notify(AppModel::Capture | AppModel::Rig | AppModel::Theme);
        juce::Timer::callPendingTimersSynchronously();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    CHECK(capture.calls == 30);
}

TEST_CASE("Diagram painting handles zero tiny and very large bounds", "[app][diagrams][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    MicPlacementDiagram diagram;
    juce::Image image(juce::Image::ARGB, 512, 512, true);
    for (auto placement : {MicPlacementDiagram::Placement::None, MicPlacementDiagram::Placement::OnAxis,
                           MicPlacementDiagram::Placement::Seam, MicPlacementDiagram::Placement::SubMainCrossover}) {
        diagram.setPlacement(placement);
        for (auto size : {juce::Point<int>(0, 0), {0, 104}, {232, 0}, {1, 1}, {1, 3000}, {4000, 1}, {4000, 3000}}) {
            diagram.setSize(size.x, size.y);
            juce::Graphics graphics(image);
            diagram.paint(graphics);
        }
    }
    CHECK(image.isValid());
}

TEST_CASE("Floating guide recovers a removed display and releases its peer on repeated close", "[app][guide][workspace][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    const std::vector<fohws::DisplayInfo> connected {
        {"main", {0, 25, 1024, 700}, true}, {"removed", {1024, 0, 2560, 1440}, false}
    };
    const int peers = juce::Desktop::getInstance().getNumComponents();
    for (int i = 0; i < 20; ++i) {
        auto panel = makeGuidePanel(model);
        fohws::FloatingPanel state {"GuideInspector", {1200, 40, 1200, 1000}, "removed"};
        CHECK(fohws::clampToDisplays(state, connected) == fohws::Rect{1200, 40, 1200, 1000});
        CHECK(fohws::clampToDisplays(state, {connected.front()}) == fohws::Rect{0, 25, 1024, 700});
        {
            FloatingPanelWindow window(*panel, state);
            window.setBounds(40, 40, 280, 240);
            window.parentSizeChanged();
            CHECK(window.getContentComponent() == panel.get());
            auto& canvas = formCanvas(*panel);
            CHECK(canvas.getHeight() >= 780);
            for (auto* child : canvas.getChildren())
                if (child->isVisible()) CHECK(canvas.getLocalBounds().contains(child->getBounds()));
        }
        CHECK(panel->getParentComponent() == nullptr);
        panel.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
    }
}

TEST_CASE("Verify controls stay reachable in small scaled guide windows", "[app][guide][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int guide = 0; guide < 5; ++guide) {
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model);
        auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 100);
        panel->loadState(state);
        button(*panel, "Measure before change").onClick(); capture.finish(model);
        button(*panel, guide == 4 ? "DSP state confirmed" : "I entered the change").onClick();
        for (float scale : {1.f, 1.5f, 2.f}) {
            panel->setTransform(juce::AffineTransform::scale(scale));
            for (auto size : {juce::Point<int>(1, 1), {120, 90}, {4000, 3000}}) {
                panel->setSize((int) (size.x / scale), (int) (size.y / scale));
                auto& canvas = formCanvas(*panel);
                for (auto* child : canvas.getChildren()) {
                    if (!child->isVisible()) continue;
                    CHECK(child->getWidth() > 0); CHECK(child->getHeight() > 0);
                    CHECK(canvas.getLocalBounds().contains(child->getBounds()));
                }
                CHECK(noteEditor(*panel).isVisible());
                CHECK(button(*panel, "Pass").isVisible()); CHECK(button(*panel, "Fail").isVisible());
                auto* viewport = dynamic_cast<juce::Viewport*>(canvas.getParentComponent()->getParentComponent());
                REQUIRE(viewport != nullptr);
                viewport->setViewPosition(4000, 4000);
                if (viewport->getMaximumVisibleWidth() >= 40 && viewport->getMaximumVisibleHeight() >= 40)
                    CHECK(viewport->getViewArea().intersects(button(*panel, "Re-measure to verify").getBounds()));
            }
        }
    }
}

namespace {
void prepareVerify(Panel& panel, AppModel& model, CaptureStub& capture) {
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100);
    panel.loadState(state);
    button(panel, "Measure before change").onClick(); capture.finish(model);
    button(panel, "I entered the change").onClick();
}
void cannotPass(Panel& panel) {
    noteEditor(panel).setText("Synthetic edge case", false); noteEditor(panel).onTextChange();
    CHECK_FALSE(button(panel, "Pass").isEnabled());
    button(panel, "Pass").onClick();
    CHECK_FALSE(labelContains(panel, "Pass:"));
}
}

TEST_CASE("Verify needs a stored reference even after deletion during capture", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    SECTION("Restored Verify has no baseline") {
        auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100);
        panel->loadState(state);
        CHECK(labelContains(*panel, "Store a new before trace"));
        CHECK(capture.calls == 0);
        cannotPass(*panel);
    }
    SECTION("Before trace removed before Verify") {
        prepareVerify(*panel, model, capture);
        model.removeTrace(0);
        CHECK(labelContains(*panel, "Stored reference missing"));
        CHECK(labelContains(*panel, "Store a new before trace"));
        cannotPass(*panel);
    }
    SECTION("Before trace removed while Verify is pending") {
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick();
        model.removeTrace(0); capture.finish(model);
        CHECK(labelContains(*panel, "Guide measurement interrupted"));
        cannotPass(*panel);
    }
    SECTION("Invalid Store result cannot enable Apply") {
        auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100); panel->loadState(state);
        button(*panel, "Measure before change").onClick();
        foh::TFResult malformed; malformed.freq = {100, 1000}; malformed.magDb = {0}; malformed.coherence = {1, 1};
        REQUIRE(model.traces.store(capture.name.toStdString(), malformed, 0));
        auto then = std::move(capture.pending); then(true);
        CHECK(button(*panel, "Measure before change").isVisible());
        CHECK(labelContains(*panel, "Invalid before trace"));
        cannotPass(*panel);
    }
}

TEST_CASE("Guide rejects input lifecycle changes before and during capture", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int change = 0; change < 4; ++change) {
        for (bool during : {false, true}) {
            CAPTURE(change, during);
            AppModel model; CaptureStub capture;
            auto panel = makeGuidePanel(model); panel->setSize(260, 780);
            prepareVerify(*panel, model, capture);
            if (during) button(*panel, "Re-measure to verify").onClick();
            if (change == 0) model.engine.audioDeviceStopped();
            if (change == 1) model.engine.audioDeviceError("synthetic disconnect");
            if (change == 2) model.engine.captureEpoch.fetch_add(1); // device/channel restart
            if (change == 3) model.engine.sampleRate.store(96000); // even without an epoch update
            if (during) capture.finish(model);
            else button(*panel, "Re-measure to verify").onClick();
            CHECK(labelContains(*panel, during ? "Guide measurement interrupted" : "Store a new before trace"));
            CHECK(capture.calls == (during ? 2 : 1));
            cannotPass(*panel);
        }
    }
    SECTION("Device change during Store cannot become the baseline") {
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 100); panel->loadState(state);
        button(*panel, "Measure before change").onClick();
        model.engine.captureEpoch.fetch_add(1); capture.finish(model);
        CHECK(labelContains(*panel, "Guide measurement interrupted"));
        CHECK(button(*panel, "Measure before change").isVisible());
    }
    SECTION("Device change retires the current recorded verdict") {
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick(); capture.finish(model);
        noteEditor(*panel).setText("Synthetic comparison", false); noteEditor(*panel).onTextChange();
        button(*panel, "Pass").onClick(); REQUIRE(labelContains(*panel, "Pass:"));
        model.engine.captureEpoch.fetch_add(1); model.notify(AppModel::Settings);
        CHECK(panel->saveState()["verificationNote"].asString().empty());
        cannotPass(*panel);
    }
}

TEST_CASE("Verify rejects silence clipping and invalid raw samples even with a coherent result", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    const float bad[] {0, 1, -1, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), std::numeric_limits<float>::denorm_min()};
    for (float sample : bad) for (int channel : {0, 1}) {
        CAPTURE(sample, channel);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick();
        float mic[32], ref[32], a[32], b[32];
        float* outputs[] {a, b};
        std::fill_n(mic, 32, 0.1f); std::fill_n(ref, 32, 0.1f);
        std::fill_n(channel == 0 ? mic : ref, 32, sample);
        const float* inputs[] {mic, ref};
        model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 32, {});
        // Later healthy input and meter resets must not erase the fault.
        std::fill_n(mic, 32, 0.1f); std::fill_n(ref, 32, 0.1f);
        model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 32, {});
        model.engine.micPeak.exchange(0); model.engine.refPeak.exchange(0);
        capture.finish(model);
        CHECK(labelContains(*panel, "Silence, clipping or invalid input"));
        cannotPass(*panel);
        button(*panel, "Re-measure to verify").onClick(); capture.finish(model);
        CHECK(labelContains(*panel, "Mean change:")); // fault belongs only to the prior capture
    }
}

TEST_CASE("Verify rejects partial frequency grids malformed vectors and nonfinite traces", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int edge = 0; edge < 8; ++edge) {
        CAPTURE(edge);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick();
        foh::TFResult tf; tf.freq = {100, 1000}; tf.magDb = {0, 0}; tf.coherence = {1, 1}; tf.phaseDeg = {0, 0};
        if (edge == 0) tf.freq[1] = 2000; // one matching bin must not enable a verdict
        if (edge == 1) { tf.freq.push_back(2000); tf.magDb.push_back(0); tf.coherence.push_back(1); tf.phaseDeg.push_back(0); }
        if (edge == 2) tf.fftSize = 8192;
        if (edge == 3) tf.sampleRate = 96000;
        if (edge == 4) tf.magDb.pop_back();
        if (edge == 5) tf.magDb[1] = std::numeric_limits<double>::quiet_NaN();
        if (edge == 6) tf.phaseDeg[1] = std::numeric_limits<double>::infinity();
        if (edge == 7) tf.coherence[1] = std::numeric_limits<double>::quiet_NaN();
        REQUIRE(model.traces.store(capture.name.toStdString(), tf, 0));
        auto then = std::move(capture.pending); then(true);
        CHECK(labelContains(*panel, edge == 3 ? "Stored and live sample rates differ" : "Frequency resolution mismatch or invalid trace"));
        cannotPass(*panel);
    }
}

TEST_CASE("Workspace and Show presentation changes retire pending guide work", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (bool show : {false, true}) {
        CAPTURE(show);
        AppModel model; CaptureStub capture;
        auto& registry = PanelRegistry::standard();
        fohws::Workspace first, second;
        first.root = fohws::LayoutNode::leaf("GuideInspector"); second.root = fohws::LayoutNode::leaf("RTA");
        WorkspaceView view(model, registry); view.setSize(400, 900); view.setVisible(true); view.setWorkspace(&first);
        auto* panel = view.panel("GuideInspector"); REQUIRE(panel);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick();
        // Switch repeatedly while retaining the old completion, including returning
        // to the very same step. The generation must not become valid again.
        for (int i = 0; i < 5; ++i) {
            if (show) { view.setShowMode(true); view.setShowMode(false); }
            else { view.setWorkspace(&second); view.setWorkspace(&first); }
        }
        capture.finish(model, 3);
        if (show) view.setShowMode(false); else view.setWorkspace(&first);
        CHECK_FALSE(labelContains(*panel, "Mean change:"));
        cannotPass(*panel);
        CHECK((labelContains(*panel, "interrupted") || labelContains(*panel, "Store a new before trace")));
    }
    SECTION("Delay callback cannot advance a different guide") {
        AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 2); panel->loadState(state);
        button(*panel, "Lock delay").onClick(); REQUIRE(guideDelay);
        state.set("guide", 4); state.set("step", 0); panel->loadState(state);
        auto then = std::move(guideDelay); then(); // a live callback would try to lock the delay
        CHECK(panel->saveState()["step"].asInt() == 0);
        CHECK(labelContains(*panel, "interrupted"));
    }
    SECTION("Dropped delay completion after device restart releases the guide buttons") {
        AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 2); panel->loadState(state);
        button(*panel, "Lock delay").onClick(); REQUIRE(guideDelay);
        CHECK_FALSE(button(*panel, "Lock delay").isEnabled());
        model.engine.audioDeviceStopped(); model.notify(AppModel::Settings | AppModel::Capture);
        auto then = std::move(guideDelay); then();
        CHECK(button(*panel, "Lock delay").isEnabled());
        CHECK(panel->saveState()["step"].asInt() == 2);
        CHECK(labelContains(*panel, "interrupted"));
    }
    SECTION("Queued delay completion checks device state before the next model notification") {
        AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 2); panel->loadState(state);
        button(*panel, "Lock delay").onClick(); REQUIRE(guideDelay);
        model.engine.audioDeviceError("synthetic disconnect");
        model.delay.confidence = 1; model.delay.arrivals.push_back({48, 1, 0, 0, false});
        auto then = std::move(guideDelay); then(); // must not invoke lockDelayFromLast
        CHECK(panel->saveState()["step"].asInt() == 2);
        CHECK(labelContains(*panel, "interrupted"));
    }
    SECTION("Destroyed guide ignores a queued capture completion") {
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick(); panel.reset();
        capture.finish(model);
        CHECK_FALSE(model.overlay.active);
    }
}

TEST_CASE("Guide Back and session reload retire pending and duplicated completions", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (bool reload : {false, true}) {
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        button(*panel, "Re-measure to verify").onClick();
        if (reload) model.notify(AppModel::Session); else button(*panel, "Back").onClick();
        const int step = panel->saveState()["step"].asInt();
        capture.finish(model, 3);
        CHECK(panel->saveState()["step"].asInt() == step);
        CHECK_FALSE(labelContains(*panel, "Mean change:"));
        cannotPass(*panel);
    }
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 3); panel->loadState(state);
    button(*panel, "Measure before change").onClick();
    auto duplicate = capture.pending; capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == 4);
    duplicate(true); CHECK(panel->saveState()["step"].asInt() == 4);
    CHECK_FALSE(button(*panel, "Pass").isEnabled());
}

TEST_CASE("W2 failed or absent sub captures cannot reuse a previous run", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (bool missing : {false, true}) {
        AppModel model; CaptureStub capture; storeSyntheticPair(model);
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 1); state.set("step", 1); panel->loadState(state);
        button(*panel, "Store sub trace").onClick();
        CHECK(model.findTrace("Sub") == nullptr);
        if (missing) { auto then = std::move(capture.pending); then(true); }
        else { capture.settled = false; capture.finish(model); }
        CHECK(panel->saveState()["step"].asInt() == 1);
        CHECK(labelContains(*panel, "No settled measurement"));
    }
}

TEST_CASE("W6 Back hiding session reload and rig edits discard in-flight box sweeps", "[app][guide][edges][polarity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int navigation = 0; navigation < 4; ++navigation) {
        AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        panel->setVisible(true);
        auto state = fohws::Json::object(); state.set("guide", 5); state.set("step", 1); panel->loadState(state);
        button(*panel, "Measure box sweep").onClick(); acceptGeneratorConfirmation();
        model.sweepSeq = model.nextGuideSweepId;
        model.sweepCapturing = true; model.notify(AppModel::Snapshot);
        if (navigation == 0) button(*panel, "Back").onClick();
        if (navigation == 1) panel->setVisible(false);
        if (navigation == 2) model.notify(AppModel::Session);
        if (navigation == 3) { model.rigProfile = foh::RigProfile{}; model.notify(AppModel::Rig); }
        const int step = panel->saveState()["step"].asInt();
        model.sweepCapturing = false; model.sweep.response.impulse = {0, 1, 0};
        model.sweep.response.arrivals.push_back({}); model.notify(AppModel::Snapshot);
        CHECK(panel->saveState()["step"].asInt() == step);
        CHECK_FALSE(labelContains(*panel, "This box: Normal"));
        cannotPass(*panel);
    }
}

TEST_CASE("W6 verdict requires current unpaused audio and is consumed before next box", "[app][guide][edges][polarity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int change = 0; change < 5; ++change) {
        AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 5); state.set("step", 1); panel->loadState(state);
        auto sweep = [&](const char* action) {
            button(*panel, action).onClick(); acceptGeneratorConfirmation();
            model.sweepSeq = model.nextGuideSweepId;
            model.sweepCapturing = true; model.notify(AppModel::Snapshot);
            model.sweepCapturing = false; model.sweep.response.impulse = {0, 1, 0};
            model.sweep.response.arrivals = {{}}; model.notify(AppModel::Snapshot);
        };
        sweep("Measure box sweep"); button(*panel, "Polarity state confirmed").onClick();
        sweep("Verify box sweep");
        noteEditor(*panel).setText("Synthetic box matches", false); noteEditor(*panel).onTextChange();
        REQUIRE(button(*panel, "Pass").isEnabled());
        if (change == 0) model.paused = true;
        if (change == 1) model.engine.deviceFailed = true;
        if (change == 2) model.engine.sampleRate = 96000;
        if (change == 3) model.refDelayMs = 5;
        button(*panel, "Pass").onClick();
        if (change < 4) CHECK(panel->saveState()["step"].asInt() == 3);
        else {
            CHECK(panel->saveState()["step"].asInt() == 4);
            CHECK_FALSE(button(*panel, "Pass").isEnabled());
            button(*panel, "Check next box").onClick();
            CHECK(panel->saveState()["step"].asInt() == 0);
        }
    }
}

TEST_CASE("W6 rapid re-entry cannot claim the previous queued sweep", "[app][guide][edges][polarity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 5); state.set("step", 1); panel->loadState(state);
    button(*panel, "Measure box sweep").onClick(); acceptGeneratorConfirmation(); const auto retired = model.nextGuideSweepId;
    button(*panel, "Back").onClick(); button(*panel, "Box isolated").onClick();
    button(*panel, "Measure box sweep").onClick(); acceptGeneratorConfirmation(); REQUIRE(model.nextGuideSweepId != retired);
    model.sweepSeq = retired; model.sweepCapturing = true; model.notify(AppModel::Snapshot);
    model.sweepCapturing = false; model.sweep.response.impulse = {0, 1, 0};
    model.sweep.response.arrivals = {{}}; model.notify(AppModel::Snapshot);
    CHECK(panel->saveState()["step"].asInt() == 1);
    CHECK_FALSE(labelContains(*panel, "This box: Normal"));
    model.sweepSeq = model.nextGuideSweepId; model.sweepCapturing = true; model.notify(AppModel::Snapshot);
    model.sweepCapturing = false; model.notify(AppModel::Snapshot);
    CHECK(panel->saveState()["step"].asInt() == 2);
}

TEST_CASE("W2 rejects incompatible timing and malformed stored captures", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int change = 0; change < 4; ++change) {
        AppModel model; storeSyntheticPair(model);
        if (change == 0) model.traces.at(1).refDelayMs = 1;
        if (change == 1) model.refDelayMs = 1;
        if (change == 2) model.traces.at(1).tf.magDb.clear();
        if (change == 3) model.traces.at(1).tf.sampleRate = 96000;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780); panel->loadState(findDelayStep());
        button(*panel, "Lock delay").onClick();
        CHECK(panel->saveState()["step"].asInt() == 2);
        CHECK(labelContains(*panel, "Store mains and sub traces again"));
        for (auto* row : choices(*panel)) CHECK_FALSE(row->isEnabled());
    }
}

TEST_CASE("W2 sub completion tolerates a mains capture losing its measurement", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture; storeSyntheticPair(model);
    auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 1); state.set("step", 1); panel->loadState(state);
    button(*panel, "Store sub trace").onClick();
    model.traces.at(0).tf.magDb.clear();
    capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == 2);
    CHECK(labelContains(*panel, "Store mains and sub traces again"));
    button(*panel, "Lock delay").onClick(); CHECK(panel->saveState()["step"].asInt() == 2);
}

TEST_CASE("W6 ambiguous impulse releases pending sweep and permits retry", "[app][guide][edges][polarity]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 5); state.set("step", 1); panel->loadState(state);
    auto complete = [&](std::vector<double> impulse) {
        button(*panel, "Measure box sweep").onClick(); acceptGeneratorConfirmation();
        model.sweepSeq = model.nextGuideSweepId;
        model.sweepCapturing = true; model.notify(AppModel::Snapshot);
        model.sweepCapturing = false; model.sweep.response.impulse = std::move(impulse);
        model.sweep.response.arrivals = {{}}; model.notify(AppModel::Snapshot);
    };
    std::vector<double> ambiguous(512); ambiguous[48] = 0.8; ambiguous[240] = -0.7;
    complete(ambiguous);
    CHECK(panel->saveState()["step"].asInt() == 1);
    CHECK(labelContains(*panel, "Inconclusive impulse"));
    REQUIRE(button(*panel, "Measure box sweep").isEnabled());
    cannotPass(*panel);
    std::vector<double> inverted(512, 0.4); inverted[48] -= 0.8;
    complete(inverted);
    CHECK(panel->saveState()["step"].asInt() == 2);
    CHECK(labelContains(*panel, "This box: Inverted"));
}

TEST_CASE("Rig edits and Skip retire measurements even when the selected W guide remains listed", "[app][guide][rig][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int change = 0; change < 3; ++change) {
        for (int guide = 0; guide < 5; ++guide) {
            CAPTURE(change, guide);
            AppModel model; CaptureStub capture;
            model.rigProfile = foh::RigProfile{};
            model.rigProfile->subCount = model.rigProfile->frontFillCount = 1;
            if (change == 1) model.rigProfile->control = foh::RigProfile::Control::Individual;
            auto panel = makeGuidePanel(model); panel->setSize(260, 780);
            auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", 100);
            panel->loadState(state);
            button(*panel, "Measure before change").onClick(); capture.finish(model);
            button(*panel, guide == 4 ? "DSP state confirmed" : "I entered the change").onClick();
            button(*panel, "Re-measure to verify").onClick();
            if (change == 0) model.rigProfile->control = foh::RigProfile::Control::Individual;
            if (change == 1) model.rigProfile->control = foh::RigProfile::Control::Grouped;
            if (change == 2) model.rigProfile.reset(); // Skip restores the default list.
            model.notify(AppModel::Rig);
            CHECK(panel->saveState()["guide"].asInt() == guide);
            CHECK(panel->saveState()["step"].asInt() == 0);
            capture.finish(model, 3); // A queued completion from the old answers.
            CHECK(panel->saveState()["step"].asInt() == 0);
            CHECK_FALSE(labelContains(*panel, "Mean change:"));
            cannotPass(*panel);
            // Returning to Store starts a fresh run instead of staying disabled.
            panel->loadState(state);
            CHECK(button(*panel, "Measure before change").isEnabled());
            button(*panel, "Measure before change").onClick(); capture.finish(model);
            CHECK(panel->saveState()["step"].asInt() == (guide == 0 ? 5 : 4));
        }
    }
}

TEST_CASE("Rig edits retire early delay completions without leaving the guide busy", "[app][guide][rig][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 2);
    panel->loadState(state);
    button(*panel, "Lock delay").onClick();
    auto stale = std::move(guideDelay); REQUIRE(stale);
    model.rigProfile = foh::RigProfile{}; model.notify(AppModel::Rig);
    stale();
    CHECK(panel->saveState()["step"].asInt() == 0);
    CHECK(button(*panel, "Open Setup").isEnabled());
    CHECK_FALSE(labelContains(*panel, "No lock."));
}

TEST_CASE("Guide summaries and disclosures fit both themes at 100 and 200 percent", "[app][guide][polish]") {
    juce::ScopedJuceInitialiser_GUI gui;
    FohLookAndFeel look;
    AppModel model; CaptureStub capture; storeSyntheticPair(model);
    auto panel = makeGuidePanel(model); panel->setLookAndFeel(&look);
    auto& themes = foh::theme::ThemeManager::get();
    auto snapshot = [&](const juce::String& name, float scale) {
        if (const char* output = std::getenv("FOH_GUIDE_SNAPSHOTS")) {
            auto& canvas = formCanvas(*panel);
            auto content = canvas.createComponentSnapshot(canvas.getLocalBounds(), true, scale);
            juce::Image image(juce::Image::RGB, content.getWidth(), content.getHeight(), true);
            juce::Graphics graphics(image); graphics.fillAll(foh::ui::col::cardBackground());
            graphics.drawImageAt(content, 0, 0);
            auto stream = juce::File(output).getChildFile(name + ".png").createOutputStream(); REQUIRE(stream);
            stream->setPosition(0); stream->truncate();
            juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream));
        }
    };
    auto checkLayout = [&] {
        auto& canvas = formCanvas(*panel);
        std::vector<juce::Component*> visible;
        for (auto* child : canvas.getChildren()) {
            if (!child->isVisible()) continue;
            CHECK(canvas.getLocalBounds().contains(child->getBounds()));
            for (auto* other : visible) CHECK_FALSE(child->getBounds().intersects(other->getBounds()));
            visible.push_back(child);
            if (auto* label = dynamic_cast<juce::Label*>(child)) {
                juce::GlyphArrangement text;
                text.addJustifiedText(label->getFont(), label->getText(), 0, label->getFont().getAscent(),
                    (float) (label->getWidth() - 10), juce::Justification::left);
                CHECK(text.getBoundingBox(0, -1, true).getHeight() <= label->getHeight());
                if (label->getComponentID() == "guide-summary") {
                    CHECK(label->getText().length() <= 140);
                    CHECK(text.getBoundingBox(0, -1, true).getHeight() <= 2 * label->getFont().getHeight());
                }
            }
        }
    };
    for (const char* theme : {"pro-dark", "pro-sunlight"}) {
        themes.setTheme(theme); look.applyTheme(); panel->sendLookAndFeelChange(); model.notify(AppModel::Theme);
        for (float scale : {1.f, 2.f}) {
            panel->setTransform(juce::AffineTransform::scale(scale));
            // 300 logical pixels at each UI scale, with scrolling for smaller hosts.
            panel->setSize(300, 780);
            for (int guide = 0; guide < 6; ++guide) {
                const int lastRestorable = guide == 0 ? 4 : guide == 5 ? 1 : 3;
                for (int step = 0; step <= lastRestorable; ++step) {
                    CAPTURE(theme, scale, guide, step);
                    auto state = fohws::Json::object(); state.set("guide", guide); state.set("step", step); panel->loadState(state);
                    auto* details = formCanvas(*panel).findChildWithID("guide-details"); REQUIRE(details);
                    CHECK_FALSE(details->isVisible()); checkLayout();
                    button(*panel, "More ▸").onClick(); CHECK(details->isVisible()); checkLayout();
                    button(*panel, "More ▾").onClick(); CHECK_FALSE(details->isVisible());
                    if (guide == 1 && step == 2) {
                        auto rows = choices(*panel); REQUIRE(rows.size() == 4);
                        CHECK(rows[0]->isVisible()); CHECK(rows[0]->getToggleState());
                        CHECK(labelContains(*panel, "Recommended"));
                        CHECK(labelContains(*panel, "Best sum through the crossover"));
                        snapshot(juce::String(theme) + "-" + juce::String(scale, 0) + "-default", scale);
                        button(*panel, "Other options ▸").onClick(); checkLayout();
                        rows[2]->setToggleState(true, juce::sendNotification);
                        panel->setSize(301, 780); CHECK(rows[2]->getToggleState()); panel->setSize(300, 780);
                        snapshot(juce::String(theme) + "-" + juce::String(scale, 0) + "-expanded", scale);
                        button(*panel, "Other options ▾").onClick(); CHECK(rows[0]->getToggleState());
                        for (size_t i = 1; i < rows.size(); ++i) CHECK_FALSE(rows[i]->isVisible());
                    }
                    if (guide == 5 && step == 1) {
                        auto sweep = [&](const char* action) {
                            button(*panel, action).onClick(); acceptGeneratorConfirmation();
                            model.sweepSeq = model.nextGuideSweepId;
                            model.sweepCapturing = true; model.notify(AppModel::Snapshot);
                            model.sweepCapturing = false; model.sweep.response.impulse = {0, 1, 0};
                            model.sweep.response.arrivals = {{}}; model.notify(AppModel::Snapshot);
                        };
                        sweep("Measure box sweep"); checkLayout();
                        button(*panel, "More ▸").onClick(); checkLayout();
                        button(*panel, "Polarity state confirmed").onClick(); checkLayout();
                        button(*panel, "More ▸").onClick(); checkLayout();
                        sweep("Verify box sweep"); noteEditor(*panel).setText("Matches", false); noteEditor(*panel).onTextChange();
                        button(*panel, "Pass").onClick(); checkLayout();
                        button(*panel, "More ▸").onClick(); checkLayout();
                    }
                    if (guide < 5 && step == lastRestorable) {
                        button(*panel, "Measure before change").onClick(); capture.finish(model); checkLayout();
                        button(*panel, "More ▸").onClick(); checkLayout();
                        button(*panel, guide == 4 ? "DSP state confirmed" : "I entered the change").onClick(); checkLayout();
                        button(*panel, "More ▸").onClick(); checkLayout();
                    }
                }
            }
        }
    }
    panel->setLookAndFeel(nullptr); themes.setTheme("pro-dark");
}

TEST_CASE("Apply rechecks the stored reference before acknowledging a DSP change", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int edge = 0; edge < 5; ++edge) {
        CAPTURE(edge);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 3); panel->loadState(state);
        button(*panel, "Measure before change").onClick(); capture.finish(model);
        REQUIRE(panel->saveState()["step"].asInt() == 4);
        // Mutate before any model notification, as a queued click can arrive first.
        if (edge == 0) model.traces.remove(0);
        if (edge == 1) model.engine.sampleRate = 96000;
        if (edge == 2) model.traces.at(0).tf.sampleRate = 96000;
        if (edge == 3) model.traces.at(0).tf.magDb.clear();
        if (edge == 4) model.refDelayMs = 1;
        button(*panel, "I entered the change").onClick();
        CHECK(panel->saveState()["step"].asInt() == 3);
        CHECK(labelContains(*panel, "Store a new before trace"));
        CHECK(capture.calls == 1);
        CHECK(button(*panel, "Measure before change").isEnabled());
        cannotPass(*panel);
    }
}

TEST_CASE("Store rejects a trace from a different live sample rate", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    model.engine.sampleRate = 96000;
    auto panel = makeGuidePanel(model); panel->setSize(260, 780);
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 3); panel->loadState(state);
    button(*panel, "Measure before change").onClick(); capture.finish(model); // synthetic trace is 48 kHz
    CHECK(panel->saveState()["step"].asInt() == 3);
    CHECK(labelContains(*panel, "Stored and live sample rates differ"));
    CHECK(button(*panel, "Measure before change").isEnabled());
    cannotPass(*panel);
}

TEST_CASE("Empty missing silent and unsettled guide completions release the action for retry", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (bool verify : {false, true}) for (int edge = 0; edge < 4; ++edge) {
        CAPTURE(verify, edge);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 3); panel->loadState(state);
        if (verify) prepareVerify(*panel, model, capture);
        const char* action = verify ? "Re-measure to verify" : "Measure before change";
        button(*panel, action).onClick();
        if (edge == 0) { foh::TFResult empty; REQUIRE(model.traces.store(capture.name.toStdString(), empty, 0)); }
        if (edge == 2) {
            float silence[32] {}, a[32], b[32];
            const float* inputs[] {silence, silence};
            float* outputs[] {a, b};
            const auto epoch = model.engine.captureEpoch.load();
            const auto faults = model.engine.signalFaults.load();
            // Keep both configured outputs available: missing outputs are a
            // routing interruption, which must not bypass the silence check.
            model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 32, {});
            REQUIRE(model.engine.state.load() == AudioEngine::Ready);
            REQUIRE(model.engine.captureEpoch.load() == epoch);
            REQUIRE(model.engine.signalFaults.load() == faults + 1);
        }
        if (edge >= 2) { capture.settled = edge != 3; capture.finish(model); }
        else { auto then = std::move(capture.pending); then(true); }
        CHECK(labelContains(*panel, edge == 2 ? "Silence, clipping or invalid input" : "No settled measurement"));
        CHECK(button(*panel, action).isEnabled());
        cannotPass(*panel);
        capture.settled = true;
        button(*panel, action).onClick();
        auto duplicate = capture.pending;
        capture.finish(model);
        const int step = panel->saveState()["step"].asInt();
        duplicate(true); CHECK(panel->saveState()["step"].asInt() == step);
        CHECK(labelContains(*panel, verify ? "Mean change:" : "Before trace stored"));
        CHECK(model.engine.gen.load() == AudioEngine::Off);
    }
}

TEST_CASE("Verify rechecks stored metadata before capture completion and verdict", "[app][guide][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int when = 0; when < 3; ++when) {
        CAPTURE(when);
        AppModel model; CaptureStub capture;
        auto panel = makeGuidePanel(model); panel->setSize(260, 780);
        prepareVerify(*panel, model, capture);
        if (when > 0) button(*panel, "Re-measure to verify").onClick();
        if (when == 2) capture.finish(model);
        model.traces.at(0).tf.sampleRate = 96000;
        if (when == 0) button(*panel, "Re-measure to verify").onClick();
        if (when == 1) capture.finish(model);
        cannotPass(*panel);
        if (when == 2) model.notify(AppModel::Traces);
        CHECK(labelContains(*panel, "Store a new before trace"));
        CHECK(button(*panel, "Measure before change").isEnabled());
        CHECK(capture.calls == (when == 0 ? 1 : 2));
    }
}
