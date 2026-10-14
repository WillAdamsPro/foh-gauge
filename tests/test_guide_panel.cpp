#include <catch2/catch_test_macros.hpp>
#include "Panels.h"
#include <stdexcept>

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
void AppModel::timerCallback() {}
foh::SplCal AppModel::splCal() const { return {}; }
bool AppModel::calibrateSpl(double) { throw std::logic_error("Unexpected SPL calibration"); }
bool AppModel::micLevelReady() const { return false; }
void AppModel::loadCalFile(const juce::File&) { throw std::logic_error("Unexpected calibration file load"); }
void AppModel::setCalEnabled(bool on) { calOn = on && cal.ok(); notify(Cal); }
std::string AppModel::loopbackKey() const { return "synthetic-loopback"; }
bool AppModel::beginLoopback() { throw std::logic_error("Unexpected interface measurement"); }
void AppModel::clearLoopback() { throw std::logic_error("Unexpected interface calibration"); }
void AppModel::clearSpl() { throw std::logic_error("Unexpected SPL calibration"); }
foh::Json AppModel::splCalJson() const { return foh::Json::object(); }
void AppModel::loadSplCalJson(const foh::Json&) {}
void AppModel::requestDelay(bool, std::function<void()>) { throw std::logic_error("Unexpected delay request"); }
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
fohws::Json findDelayStep() {
    auto state = fohws::Json::object(); state.set("guide", 1); state.set("step", 2); return state;
}
std::vector<juce::ToggleButton*> choices(Panel& panel) {
    std::vector<juce::ToggleButton*> rows;
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* row = dynamic_cast<juce::ToggleButton*>(panel.getChildComponent(i))) rows.push_back(row);
    return rows;
}
bool labelContains(Panel& panel, const juce::String& text) {
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* label = dynamic_cast<juce::Label*>(panel.getChildComponent(i)))
            if (label->isVisible() && label->getText().contains(text)) return true;
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
    CHECK(rows[1]->getButtonText() == "-1 period: Mains +2.50 ms");
    CHECK(rows[2]->getButtonText() == "+1 period: Mains +22.50 ms");
    CHECK(rows[3]->getButtonText().contains("Mains +7.50 ms"));
    CHECK(rows[0]->getToggleState());
    CHECK(labelContains(*panel, "Raw impulse peak: 20.50 ms (hint only)"));
    for (size_t selected = 0; selected < rows.size(); ++selected) {
        panel->loadState(findDelayStep());
        rows[selected]->setToggleState(true, juce::sendNotification);
        for (size_t i = 0; i < rows.size(); ++i) {
            CHECK(rows[i]->isVisible()); CHECK(rows[i]->isEnabled());
            CHECK(rows[i]->getToggleState() == (i == selected));
            CHECK(panel->getLocalBounds().contains(rows[i]->getBounds()));
        }
        // Resizing must preserve the selected option.
        panel->setSize(260, 661 + (int) selected);
        juce::TextButton* use = nullptr;
        for (int i = 0; i < panel->getNumChildComponents(); ++i)
            if (auto* button = dynamic_cast<juce::TextButton*>(panel->getChildComponent(i)))
                if (button->getButtonText() == "Use this delay") use = button;
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
    CHECK(labelContains(*panel, "Capture mains and sub first"));
}

namespace {
juce::TextButton& button(Panel& panel, const char* text) {
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* b = dynamic_cast<juce::TextButton*>(panel.getChildComponent(i)))
            if (b->getButtonText() == text) return *b;
    throw std::logic_error("Missing guide button");
}
juce::TextEditor& noteEditor(Panel& panel) {
    for (int i = 0; i < panel.getNumChildComponents(); ++i)
        if (auto* e = dynamic_cast<juce::TextEditor*>(panel.getChildComponent(i))) return *e;
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
        button(*panel, "I entered the change").onClick();
        CHECK(panel->saveState()["step"].asInt() == beforeStep + 2);
        CHECK(labelContains(*panel, "Verify against the stored trace"));
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        CHECK_FALSE(button(*panel, "Fail").isEnabled());
        button(*panel, "Re-measure to verify").onClick();
        CHECK(capture.calls == 2);
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        capture.finish(model, 3);
        CHECK(labelContains(*panel, "3.00 dB across 2 coherent bins"));
        CHECK_FALSE(button(*panel, "Pass").isEnabled());
        noteEditor(*panel).setText("Improved overlap", false); noteEditor(*panel).onTextChange();
        REQUIRE(button(*panel, "Pass").isEnabled());
        button(*panel, "Pass").onClick(); CHECK(labelContains(*panel, "PASS: Improved overlap"));
        button(*panel, "Fail").onClick(); CHECK(labelContains(*panel, "FAIL: Improved overlap"));
        CHECK(panel->saveState()["verificationNote"].asString() == "FAIL: Improved overlap");
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
    auto state = fohws::Json::object(); state.set("guide", 3); state.set("step", 0); panel->loadState(state);
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    button(*panel, "I entered the change").onClick();
    button(*panel, "Re-measure to verify").onClick(); model.refDelayMs = 5; capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == 0);
    CHECK(labelContains(*panel, "Reference delay or routing changed"));
    button(*panel, "Measure before change").onClick();
    state.set("guide", 4); panel->loadState(state); capture.finish(model);
    CHECK(panel->saveState()["guide"].asInt() == 4);
    CHECK(panel->saveState()["step"].asInt() == 0);
}

TEST_CASE("Guide requires a settled baseline and rejects captures with no comparable coherent bins", "[app][guide]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; CaptureStub capture;
    auto panel = makeGuidePanel(model); panel->setSize(260, 660);
    auto state = fohws::Json::object(); state.set("guide", 4); state.set("step", 0); panel->loadState(state);
    capture.settled = false;
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    CHECK(panel->saveState()["step"].asInt() == 0);
    CHECK(labelContains(*panel, "No settled measurement"));
    capture.settled = true;
    button(*panel, "Measure before change").onClick(); capture.finish(model);
    button(*panel, "I entered the change").onClick();
    button(*panel, "Re-measure to verify").onClick();
    foh::TFResult lowCoherence;
    lowCoherence.freq = {100, 1000}; lowCoherence.magDb = {0, 0}; lowCoherence.coherence = {0.1, 0.1};
    REQUIRE(model.traces.store(capture.name.toStdString(), lowCoherence, 0));
    auto then = std::move(capture.pending); then(true);
    CHECK(labelContains(*panel, "No comparable coherent bins"));
    noteEditor(*panel).setText("Cannot judge", false); noteEditor(*panel).onTextChange();
    CHECK_FALSE(button(*panel, "Pass").isEnabled());
    CHECK_FALSE(button(*panel, "Fail").isEnabled());
    for (int i = 0; i < panel->getNumChildComponents(); ++i) {
        auto* child = panel->getChildComponent(i);
        if (child->isVisible()) CHECK(panel->getLocalBounds().contains(child->getBounds()));
    }
}
