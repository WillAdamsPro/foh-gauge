#include <catch2/catch_test_macros.hpp>
// W4 tuning, W5 pre-doors and W6 polarity are always listed after the rig-tailored W1-W3 guides.
#include "EasySetup.h"
#include "SetupPage.h"
#include "Panels.h"
#include <stdexcept>
#include "ui_form_helpers.h"

std::unique_ptr<Panel> makeGuidePanel(AppModel&);
namespace {
juce::TextButton& button(juce::Component& p, const char* text) {
    for (auto* child : formCanvas(p).getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) if (b->getButtonText() == text) return *b;
    throw std::runtime_error("Missing questionnaire button");
}
juce::ComboBox& guideBox(Panel& panel) {
    for (auto* child : formCanvas(panel).getChildren()) if (auto* b = dynamic_cast<juce::ComboBox*>(child)) return *b;
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
    CHECK(box.getNumItems() == 6);
    button(page, "Rig questionnaire").onClick(); CHECK(questionnaire->isVisible());
    for (auto* child : questionnaire->getChildren())
        if (auto* e = dynamic_cast<juce::TextEditor*>(child)) e->setText("0");
    button(*questionnaire, "Skip").onClick(); CHECK_FALSE(questionnaire->isVisible());
    CHECK_FALSE(model.rigProfile); CHECK(box.getNumItems() == 6);
}
TEST_CASE("Questionnaire applies drafts, filters guides and restores default guides on Skip", "[app][rig]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; EasySetup questionnaire(model); questionnaire.setSize(260, 520);
    for (auto* child : formCanvas(questionnaire).getChildren()) CHECK(formCanvas(questionnaire).getLocalBounds().contains(child->getBounds()));
    int closed = 0; questionnaire.onClose = [&] { ++closed; };
    auto panel = makeGuidePanel(model); auto& box = guideBox(*panel);
    button(questionnaire, "Use these answers").onClick();
    REQUIRE(model.rigProfile); CHECK(closed == 1);
    CHECK(box.getNumItems() == 4); CHECK(box.getItemId(0) == 1);
    CHECK(panel->saveState()["guide"].asInt() == 0);
    auto hidden = foh::Json::object(); hidden.set("guide", 1); hidden.set("step", 2);
    panel->loadState(hidden); CHECK(panel->saveState()["guide"].asInt() == 0); CHECK(panel->saveState()["step"].asInt() == 0);
    questionnaire.begin();
    for (auto* child : formCanvas(questionnaire).getChildren()) {
        if (auto* t = dynamic_cast<juce::ToggleButton*>(child)) t->setToggleState(true, juce::dontSendNotification);
        if (auto* e = dynamic_cast<juce::TextEditor*>(child)) e->setText("3");
        if (auto* c = dynamic_cast<juce::ComboBox*>(child)) c->setSelectedId(2, juce::dontSendNotification);
    }
    CHECK_FALSE(model.rigProfile->subs); CHECK(box.getNumItems() == 4);
    button(questionnaire, "Skip").onClick(); CHECK(closed == 2);
    CHECK_FALSE(model.rigProfile); CHECK(box.getNumItems() == 6);
    button(questionnaire, "Use these answers").onClick(); CHECK(closed == 3);
    CHECK(model.rigProfile->subs); CHECK(model.rigProfile->frontFills);
    CHECK(model.rigProfile->clusters == 3); CHECK(model.rigProfile->cabinetsPerCluster == 3);
    CHECK(model.rigProfile->control == foh::RigProfile::Control::Individual);
    CHECK(model.rigProfile->subArrangement == foh::RigProfile::SubArrangement::LeftRight);
    CHECK(box.getNumItems() == 6); CHECK(box.getItemId(1) == 2); CHECK(box.getItemId(2) == 3);
    // Invalid drafts never change the applied profile or close the questionnaire.
    for (auto* child : formCanvas(questionnaire).getChildren())
        if (auto* e = dynamic_cast<juce::TextEditor*>(child)) e->setText("0");
    button(questionnaire, "Use these answers").onClick(); CHECK(closed == 3); CHECK(model.rigProfile->clusters == 3);
    // Loading an older session reintroduces the default flow.
    model.rigProfile.reset(); model.notify(AppModel::Rig); CHECK(box.getNumItems() == 6);
    // A panel created after session restoration uses the profile immediately.
    panel.reset();
    foh::RigProfile fillsOnly; fillsOnly.frontFills = true; model.rigProfile = fillsOnly;
    panel = makeGuidePanel(model);
    CHECK(guideBox(*panel).getNumItems() == 5);
    CHECK(guideBox(*panel).getItemId(0) == 1); CHECK(guideBox(*panel).getItemId(1) == 3);
}

TEST_CASE("Questionnaire remains reachable at tiny and large sizes and 100-200 percent scale", "[app][rig][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    EasySetup questionnaire(model);
    auto& canvas = formCanvas(questionnaire);
    auto* viewport = dynamic_cast<juce::Viewport*>(canvas.getParentComponent()->getParentComponent());
    REQUIRE(viewport != nullptr);
    for (float scale : {1.f, 1.25f, 1.5f, 1.75f, 2.f}) {
        questionnaire.setTransform(juce::AffineTransform::scale(scale));
        for (auto size : {juce::Point<int>(0, 0), {1, 1}, {120, 90}, {260, 520}, {4000, 3000}}) {
            CAPTURE(scale, size.x, size.y);
            questionnaire.setSize((int) (size.x / scale), (int) (size.y / scale));
            questionnaire.begin();
            for (auto* child : canvas.getChildren()) {
                CHECK(child->getWidth() > 0);
                CHECK(child->getHeight() >= 20);
                CHECK(canvas.getLocalBounds().contains(child->getBounds()));
            }
            viewport->setViewPosition(4000, 4000);
            if (viewport->getMaximumVisibleWidth() >= 40 && viewport->getMaximumVisibleHeight() >= 40)
                CHECK(viewport->getViewArea().intersects(button(questionnaire, "Use these answers").getBounds()));
            questionnaire.begin(); CHECK(viewport->getViewPosition() == juce::Point<int>(0, 0));
        }
    }
}

TEST_CASE("Repeated questionnaire close and destruction discards queued button callbacks", "[app][rig][lifetime][stress]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    int closed = 0;
    for (int i = 0; i < 30; ++i) {
        auto questionnaire = std::make_unique<EasySetup>(model);
        questionnaire->onClose = [&] { ++closed; };
        questionnaire->setSize(120, 90);
        button(*questionnaire, "Skip").onClick();
        questionnaire->begin();
        button(*questionnaire, "Use these answers").triggerClick();
        questionnaire.reset();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    }
    CHECK(closed == 30);
    CHECK_FALSE(model.rigProfile);
}

TEST_CASE("Applying questionnaire answers tolerates destruction during model notification", "[app][rig][lifetime]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model;
    auto questionnaire = std::make_unique<EasySetup>(model);
    struct CloseOnRig : AppModel::Listener {
        std::unique_ptr<EasySetup>& questionnaire;
        explicit CloseOnRig(std::unique_ptr<EasySetup>& q) : questionnaire(q) {}
        void modelChanged(int what) override { if (what & AppModel::Rig) questionnaire.reset(); }
    } listener(questionnaire);
    int closed = 0; questionnaire->onClose = [&] { ++closed; };
    model.addListener(&listener);
    // Keep the callback alive independently of its button, as JUCE does for queued clicks.
    auto apply = button(*questionnaire, "Use these answers").onClick;
    apply();
    model.removeListener(&listener);
    CHECK(questionnaire == nullptr);
    CHECK(model.rigProfile.has_value());
    CHECK(closed == 0);
}
