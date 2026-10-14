#include <catch2/catch_test_macros.hpp>
#include "EasySetup.h"
#include "SetupPage.h"
#include "Panels.h"
#include <stdexcept>

std::unique_ptr<Panel> makeGuidePanel(AppModel&);
namespace {
juce::TextButton& button(juce::Component& p, const char* text) {
    for (auto* child : p.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) if (b->getButtonText() == text) return *b;
    throw std::runtime_error("Missing questionnaire button");
}
juce::ComboBox& guideBox(Panel& panel) {
    for (auto* child : panel.getChildren()) if (auto* b = dynamic_cast<juce::ComboBox*>(child)) return *b;
    throw std::runtime_error("Missing guide selector");
}
}
TEST_CASE("Questionnaire is optional in Setup and Skip preserves the default flow", "[app][rig]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    SetupPage page(model); page.setSize(520, 580);
    EasySetup* questionnaire = nullptr;
    for (auto* child : page.getChildren()) if (auto* q = dynamic_cast<EasySetup*>(child)) questionnaire = q;
    REQUIRE(questionnaire != nullptr); CHECK_FALSE(questionnaire->isVisible());
    auto panel = makeGuidePanel(model); auto& box = guideBox(*panel);
    CHECK(box.getNumItems() == 3);
    button(page, "Rig questionnaire").onClick(); CHECK(questionnaire->isVisible());
    button(*questionnaire, "Skip").onClick(); CHECK_FALSE(questionnaire->isVisible());
    CHECK_FALSE(model.rigProfile); CHECK(box.getNumItems() == 3);
}
TEST_CASE("Questionnaire applies drafts, filters guides and preserves saved profiles on Skip", "[app][rig]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; EasySetup questionnaire(model); questionnaire.setSize(260, 520);
    for (auto* child : questionnaire.getChildren()) CHECK(questionnaire.getLocalBounds().contains(child->getBounds()));
    int closed = 0; questionnaire.onClose = [&] { ++closed; };
    auto panel = makeGuidePanel(model); auto& box = guideBox(*panel);
    button(questionnaire, "Use these answers").onClick();
    REQUIRE(model.rigProfile); CHECK(closed == 1);
    CHECK(box.getNumItems() == 1); CHECK(box.getItemId(0) == 1);
    CHECK(panel->saveState()["guide"].asInt() == 0);
    auto hidden = foh::Json::object(); hidden.set("guide", 1); hidden.set("step", 2);
    panel->loadState(hidden); CHECK(panel->saveState()["guide"].asInt() == 0); CHECK(panel->saveState()["step"].asInt() == 0);
    questionnaire.begin();
    for (auto* child : questionnaire.getChildren()) {
        if (auto* t = dynamic_cast<juce::ToggleButton*>(child)) t->setToggleState(true, juce::dontSendNotification);
        if (auto* e = dynamic_cast<juce::TextEditor*>(child)) e->setText("3");
        if (auto* c = dynamic_cast<juce::ComboBox*>(child)) c->setSelectedId(2, juce::dontSendNotification);
    }
    CHECK_FALSE(model.rigProfile->subs); CHECK(box.getNumItems() == 1);
    button(questionnaire, "Skip").onClick(); CHECK(closed == 2);
    CHECK_FALSE(model.rigProfile->subs); CHECK(box.getNumItems() == 1);
    button(questionnaire, "Use these answers").onClick(); CHECK(closed == 3);
    CHECK(model.rigProfile->subs); CHECK(model.rigProfile->frontFills);
    CHECK(model.rigProfile->clusters == 3); CHECK(model.rigProfile->cabinetsPerCluster == 3);
    CHECK(model.rigProfile->control == foh::RigProfile::Control::Individual);
    CHECK(model.rigProfile->subArrangement == foh::RigProfile::SubArrangement::LeftRight);
    CHECK(box.getNumItems() == 3); CHECK(box.getItemId(1) == 2); CHECK(box.getItemId(2) == 3);
    // Invalid drafts never change the applied profile or close the questionnaire.
    for (auto* child : questionnaire.getChildren())
        if (auto* e = dynamic_cast<juce::TextEditor*>(child)) e->setText("0");
    button(questionnaire, "Use these answers").onClick(); CHECK(closed == 3); CHECK(model.rigProfile->clusters == 3);
    // Loading an older session reintroduces the default flow.
    model.rigProfile.reset(); model.notify(AppModel::Rig); CHECK(box.getNumItems() == 3);
    // A panel created after session restoration uses the profile immediately.
    panel.reset();
    foh::RigProfile fillsOnly; fillsOnly.frontFills = true; model.rigProfile = fillsOnly;
    panel = makeGuidePanel(model);
    CHECK(guideBox(*panel).getNumItems() == 2);
    CHECK(guideBox(*panel).getItemId(0) == 1); CHECK(guideBox(*panel).getItemId(1) == 3);
}
