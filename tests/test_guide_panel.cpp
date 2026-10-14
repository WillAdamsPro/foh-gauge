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
void AppModel::clearSpl() { throw std::logic_error("Unexpected SPL calibration"); }
foh::Json AppModel::splCalJson() const { return foh::Json::object(); }
void AppModel::loadSplCalJson(const foh::Json&) {}
void AppModel::requestDelay(bool, std::function<void()>) { throw std::logic_error("Unexpected delay request"); }
void AppModel::lockDelayFromLast() { throw std::logic_error("Unexpected delay lock"); }
void AppModel::beginCapture(const juce::String&, std::function<void(bool)>) { throw std::logic_error("Unexpected capture"); }
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
