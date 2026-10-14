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
    const float a[] {0.1f}, b[] {0.2f}, mic[] {0.3f}, ref[] {0.4f};
    const float* inputs[] {a, b, mic, ref};
    engine.audioDeviceIOCallbackWithContext(inputs, 4, nullptr, 0, 1, {});
    float capturedMic = 0, capturedRef = 0;
    REQUIRE(engine.micRing.pop(&capturedMic, 1) == 1);
    REQUIRE(engine.refRing.pop(&capturedRef, 1) == 1);
    CHECK(capturedMic == mic[0]); CHECK(capturedRef == ref[0]);
    float out0[32] {}, out1[32] {}, out2[32] {}, out3[32] {};
    float* outputs[] {out0, out1, out2, out3};
    engine.playChime();
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 4, 32, {});
    CHECK(out2[16] != 0); CHECK(out3[16] == out2[16]);
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
