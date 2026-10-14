#include <catch2/catch_test_macros.hpp>
#include "SetupPage.h"
#include <stdexcept>

namespace {
juce::TextButton& button(juce::Component& parent, const char* text) {
    for (int i = 0; i < parent.getNumChildComponents(); ++i)
        if (auto* b = dynamic_cast<juce::TextButton*>(parent.getChildComponent(i)))
            if (b->getButtonText() == text) return *b;
    throw std::runtime_error("Missing wizard button");
}
juce::ComboBox& choice(RoutingWizard& wizard) {
    for (int i = 0; i < wizard.getNumChildComponents(); ++i)
        if (auto* b = dynamic_cast<juce::ComboBox*>(wizard.getChildComponent(i))) return *b;
    throw std::runtime_error("Missing channel selector");
}
void select(RoutingWizard& wizard, int physicalIndex) {
    choice(wizard).setSelectedId(physicalIndex + 2, juce::sendNotificationSync);
}
}

TEST_CASE("Optional routing walkthrough applies physical channels only on Finish", "[app][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AudioEngine engine;
    RoutingWizard wizard(engine);
    wizard.setSize(260, 420);
    for (int i = 0; i < wizard.getNumChildComponents(); ++i)
        CHECK(wizard.getLocalBounds().contains(wizard.getChildComponent(i)->getBounds()));
    CHECK_FALSE(button(wizard, "Back").getBounds().intersects(button(wizard, "Skip").getBounds()));
    CHECK_FALSE(button(wizard, "Skip").getBounds().intersects(button(wizard, "Next").getBounds()));
    wizard.setChannels({"In 1", "In 2", "In 3", "In 4"}, {"Out 1", "Out 2", "Out 3", "Out 4"});
    int applied = 0, closed = 0;
    wizard.onApplied = [&] { ++applied; }; wizard.onClose = [&] { ++closed; };
    CHECK_FALSE(button(wizard, "Back").isEnabled());
    select(wizard, 2); button(wizard, "Next").onClick();
    select(wizard, 3); button(wizard, "Back").onClick();
    CHECK(choice(wizard).getSelectedId() == 4);
    button(wizard, "Next").onClick(); CHECK(choice(wizard).getSelectedId() == 5);
    button(wizard, "Next").onClick(); select(wizard, 2);
    button(wizard, "Next").onClick(); select(wizard, 3);
    CHECK(engine.micCh.load() == 0); CHECK(engine.refCh.load() == 1);
    CHECK(applied == 0); CHECK(closed == 0);
    button(wizard, "Finish").onClick();
    CHECK(engine.micCh.load() == 2); CHECK(engine.refCh.load() == 3);
    CHECK(engine.outB.load() == 2); CHECK(engine.outA.load() == 3);
    CHECK(applied == 1); CHECK(closed == 1);
    // The callback uses these exact indices, rather than a separate wizard map.
    const float a[32] {0.1f}, b[32] {0.2f}, mic[32] {0.3f}, ref[32] {0.4f};
    const float* inputs[] {a, b, mic, ref};
    float out0[32] {}, out1[32] {}, out2[32] {}, out3[32] {};
    float* outputs[] {out0, out1, out2, out3};
    // A channel-map change restarts capture; acknowledge it as the worker would.
    engine.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 4, 1, {});
    engine.analysisEpoch.store(engine.captureEpoch.load());
    engine.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 4, 1, {});
    float capturedMic = 0, capturedRef = 0;
    REQUIRE(engine.micRing.pop(&capturedMic, 1) == 1);
    REQUIRE(engine.refRing.pop(&capturedRef, 1) == 1);
    CHECK(capturedMic == mic[0]); CHECK(capturedRef == ref[0]);
    engine.playChime();
    engine.audioDeviceIOCallbackWithContext(inputs, 4, outputs, 4, 32, {});
    CHECK(out2[16] == 0); CHECK(out3[16] != 0);
    CHECK(out0[16] == 0); CHECK(out1[16] == 0);
}

TEST_CASE("Skip discards draft choices from every routing step", "[app][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    int applied = 0, closed = 0;
    wizard.onApplied = [&] { ++applied; }; wizard.onClose = [&] { ++closed; };
    for (int step = 0; step < 4; ++step) {
        wizard.begin();
        for (int i = 0; i < step; ++i) button(wizard, "Next").onClick();
        select(wizard, -1); button(wizard, "Skip").onClick();
        CHECK(engine.micCh.load() == 0); CHECK(engine.refCh.load() == 1);
        CHECK(engine.outA.load() == 0); CHECK(engine.outB.load() == 1);
    }
    CHECK(applied == 0); CHECK(closed == 4);
    wizard.begin();
    CHECK_FALSE(button(wizard, "Back").isEnabled());
    CHECK(choice(wizard).getSelectedId() == 2);
    select(wizard, 1); button(wizard, "Next").onClick();
    select(wizard, 0); button(wizard, "Back").onClick();
    CHECK(choice(wizard).getSelectedId() == 3);
    select(wizard, -1); button(wizard, "Next").onClick();
    CHECK(choice(wizard).getSelectedId() == 2);
    button(wizard, "Skip").onClick(); wizard.begin();
    CHECK(choice(wizard).getSelectedId() == 2);
    CHECK_FALSE(button(wizard, "Back").isEnabled());
    CHECK(button(wizard, "Test connection").isEnabled());
    CHECK(applied == 0); CHECK(engine.micCh.load() == 0); CHECK(engine.refCh.load() == 1);
}

TEST_CASE("Routing handles missing channels and Setup opens it only on request", "[app][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; SetupPage setup(model);
    setup.setSize(260, 460);
    RoutingWizard* wizard = nullptr;
    for (int i = 0; i < setup.getNumChildComponents(); ++i)
        if (auto* w = dynamic_cast<RoutingWizard*>(setup.getChildComponent(i))) wizard = w;
    REQUIRE(wizard != nullptr); CHECK_FALSE(wizard->isVisible());
    CHECK(setup.getLocalBounds().contains(button(setup, "Easy Setup").getBounds()));
    button(setup, "Easy Setup").onClick(); CHECK(wizard->isVisible());
    CHECK(choice(*wizard).getSelectedId() == 1);
    for (int i = 0; i < 3; ++i) button(*wizard, "Next").onClick();
    button(*wizard, "Finish").onClick(); CHECK_FALSE(wizard->isVisible());
    CHECK(model.engine.micCh.load() == -1); CHECK(model.engine.refCh.load() == -1);
    CHECK(model.engine.outA.load() == -1); CHECK(model.engine.outB.load() == -1);
}

TEST_CASE("Wizard suggests known interface defaults without applying them", "[app][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AudioEngine engine; engine.micCh = 3; engine.refCh = 2;
    RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2", "In 3", "In 4"}, {"Out 1", "Out 2"});
    wizard.setDevices("BEHRINGER UMC404HD", "UMC404HD"); wizard.begin();
    CHECK(choice(wizard).getSelectedId() == 2);
    CHECK(engine.micCh.load() == 3);
    button(wizard, "Next").onClick(); CHECK(choice(wizard).getSelectedId() == 3);
    button(wizard, "Next").onClick(); CHECK(choice(wizard).getSelectedId() == 3);
    button(wizard, "Next").onClick(); CHECK(choice(wizard).getSelectedId() == 2);
    button(wizard, "Finish").onClick();
    CHECK(engine.micCh.load() == 0); CHECK(engine.refCh.load() == 1);
    CHECK(engine.outA.load() == 0); CHECK(engine.outB.load() == 1);
    engine.micCh = 3;
    wizard.setDevices("Unknown USB", "Unknown USB"); wizard.begin();
    CHECK(choice(wizard).getSelectedId() == 5);
    // A known input paired with an unrelated output must also remain manual.
    wizard.setDevices("UMC404HD", "Unknown USB"); wizard.begin();
    CHECK(choice(wizard).getSelectedId() == 5);
}

TEST_CASE("Each wizard connection reports pass or swapped inputs without applying the draft", "[app][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    auto statusContains = [&](const char* text) {
        for (int i = 0; i < wizard.getNumChildComponents(); ++i)
            if (auto* label = dynamic_cast<juce::Label*>(wizard.getChildComponent(i)))
                if (label->getText().contains(text)) return true;
        return false;
    };
    std::vector<float> signal(48000, 0.1f), silence(48000), a(48000), b(48000);
    float* outputs[] {a.data(), b.data()};
    for (int step = 0; step < 4; ++step) {
        const int expected = step == 0 || step == 3 ? 0 : 1;
        for (bool swapped : {false, true}) {
            const int active = swapped ? 1 - expected : expected;
            const float* inputs[] {active == 0 ? signal.data() : silence.data(), active == 1 ? signal.data() : silence.data()};
            button(wizard, "Test connection").onClick();
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 48000, {});
            const char* expectedText = swapped ? "swap inputs" : "Pass:";
            for (int attempt = 0; attempt < 50 && !statusContains(expectedText); ++attempt) {
                juce::Thread::sleep(10);
                juce::Timer::callPendingTimersSynchronously();
            }
            INFO("step " << step << ", swapped " << swapped);
            CHECK(statusContains(expectedText));
        }
        CHECK(engine.micCh.load() == 0); CHECK(engine.refCh.load() == 1);
        if (step < 3) button(wizard, "Next").onClick();
    }
    button(wizard, "Back").onClick();
    CHECK(statusContains("swap inputs")); // Per-step result survives navigation.
    button(wizard, "Test connection").onClick();
    button(wizard, "Skip").onClick();
    CHECK(engine.connectionResult() == foh::ConnectionResult::pending);
}

TEST_CASE("Routing Finish is single use and survives destruction by its Apply listener", "[app][routing][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine; RoutingWizard wizard(engine);
    int applied = 0, closed = 0;
    wizard.onApplied = [&] { ++applied; }; wizard.onClose = [&] { ++closed; };
    for (int i = 0; i < 3; ++i) button(wizard, "Next").onClick();
    button(wizard, "Finish").onClick(); button(wizard, "Finish").onClick();
    button(wizard, "Skip").onClick(); button(wizard, "Test connection").onClick();
    CHECK(applied == 1); CHECK(closed == 1);
    wizard.begin(); button(wizard, "Skip").onClick(); CHECK(closed == 2);
    auto owned = std::make_unique<RoutingWizard>(engine);
    owned->onApplied = [&] { owned.reset(); };
    for (int i = 0; i < 3; ++i) button(*owned, "Next").onClick();
    auto finish = button(*owned, "Finish").onClick; finish(); CHECK_FALSE(owned);
}

TEST_CASE("Routing navigation and hiding retire connection tests before re-entry", "[app][routing][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    for (bool hide : {false, true}) {
        wizard.begin(); wizard.setVisible(true);
        button(wizard, "Next").onClick(); button(wizard, "Test connection").onClick();
        if (hide) wizard.setVisible(false); else button(wizard, "Back").onClick();
        CHECK(engine.connectionResult() == foh::ConnectionResult::pending);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
        juce::Timer::callPendingTimersSynchronously();
        wizard.begin(); CHECK(button(wizard, "Test connection").isEnabled());
        CHECK(choice(wizard).getSelectedId() == 2);
    }
}

TEST_CASE("Wizard detection edges report failure and allow retry", "[app][routing][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    wizard.setDevices("Unknown USB", "Unknown USB"); wizard.begin();
    auto contains = [&](const char* text) {
        for (auto* child : wizard.getChildren())
            if (auto* label = dynamic_cast<juce::Label*>(child))
                if (label->getText().contains(text)) return true;
        return false;
    };
    auto waitFor = [&](const char* text) {
        for (int i = 0; i < 50 && !contains(text); ++i) {
            juce::Thread::sleep(10); juce::Timer::callPendingTimersSynchronously();
        }
        CHECK(contains(text)); CHECK(button(wizard, "Test connection").isEnabled());
    };
    CHECK(contains("Unknown interface: choose channels manually."));
    std::vector<float> signal(48000, 0.1f), quiet(48000), clipped(48000, 1.f), low(48000, 0.0001f);
    std::vector<float> a(48000), b(48000); float* outputs[] {a.data(), b.data()};
    for (int edge = 0; edge < 8; ++edge) {
        button(wizard, "Test connection").onClick();
        const float* inputs[] {signal.data(), quiet.data()};
        if (edge == 0) inputs[1] = signal.data();
        if (edge == 1) inputs[0] = quiet.data();
        if (edge == 2) inputs[0] = low.data();
        if (edge == 3) inputs[0] = clipped.data();
        if (edge < 4) engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 48000, {});
        if (edge == 4) engine.audioDeviceStopped();
        if (edge == 5) engine.audioDeviceError("Synthetic unplug");
        if (edge == 6) wizard.setDevices("Replacement USB", "Replacement USB");
        if (edge == 7) wizard.setChannels({"Replacement 1", "Replacement 2"}, {"Out 1", "Out 2"});
        waitFor("Fail:");
        engine.deviceFailed = false; engine.state = AudioEngine::Ready;
        button(wizard, "Test connection").onClick();
        const float* good[] {signal.data(), quiet.data()};
        engine.audioDeviceIOCallbackWithContext(good, 2, outputs, 2, 48000, {});
        waitFor("Pass:");
    }
}

TEST_CASE("Back and Skip stop active routing playback and reject stale results", "[app][routing][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    float signal[256], a[256] {}, b[256] {}; std::fill(std::begin(signal), std::end(signal), 0.1f);
    const float* inputs[] {signal, signal}; float* outputs[] {a, b};
    for (bool skip : {false, true}) {
        wizard.begin(); button(wizard, "Next").onClick();
        button(wizard, "Test connection").onClick();
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, {});
        button(wizard, skip ? "Skip" : "Back").onClick();
        for (int i = 0; i < 200; ++i) {
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, {});
            CHECK(std::all_of(std::begin(a), std::end(a), [](float v) { return v == 0; }));
            CHECK(std::all_of(std::begin(b), std::end(b), [](float v) { return v == 0; }));
        }
        CHECK(engine.connectionResult() == foh::ConnectionResult::pending);
        wizard.begin(); CHECK(button(wizard, "Test connection").isEnabled());
    }
}

TEST_CASE("Re-entering manual routing clamps stale engine channel choices", "[app][routing][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AudioEngine engine; RoutingWizard wizard(engine);
    wizard.setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    engine.micCh = 7; engine.refCh = 8; engine.outB = 9; engine.outA = 10;
    wizard.begin();
    for (int step = 0; step < 4; ++step) {
        CHECK(choice(wizard).getSelectedId() == 1);
        if (step < 3) button(wizard, "Next").onClick();
    }
    button(wizard, "Finish").onClick();
    CHECK(engine.micCh == -1); CHECK(engine.refCh == -1);
    CHECK(engine.outA == -1); CHECK(engine.outB == -1);
}
