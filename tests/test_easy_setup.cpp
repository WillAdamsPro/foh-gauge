#include <catch2/catch_test_macros.hpp>
// W4 tuning, W5 pre-doors and W6 polarity are always listed after the rig-tailored W1-W3 guides.
#include "EasySetup.h"
#include "SetupPage.h"
#include "Panels.h"
#include "Diagrams.h"
#include <stdexcept>
#include "ui_form_helpers.h"

std::unique_ptr<Panel> makeGuidePanel(AppModel&);
namespace {
juce::TextButton& button(juce::Component& p, const char* text) {
    for (auto* child : formCanvas(p).getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) if (b->getButtonText() == text) return *b;
    throw std::runtime_error("Missing questionnaire button");
}
juce::Slider& slider(EasySetup& q, const char* name) {
    for (auto* c : formCanvas(q).getChildren()) if (auto* e = dynamic_cast<juce::Slider*>(c); e && e->getName() == name) return *e;
    throw std::runtime_error("Missing stepper");
}
void draft(EasySetup& q) {
    slider(q, "Default cabinets").setValue(3, juce::sendNotificationSync);
    slider(q, "Clusters").setValue(3, juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    button(q, "Apply default to all clusters").onClick();
    slider(q, "Front fill count").setValue(3, juce::sendNotificationSync);
    slider(q, "Sub count").setValue(3, juce::sendNotificationSync);
    for (auto* c : formCanvas(q).getChildren()) if (auto* box = dynamic_cast<juce::ComboBox*>(c)) box->setSelectedId(2, juce::sendNotificationSync);
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
    for (auto* child : formCanvas(*questionnaire).getChildren())
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
    button(questionnaire, "Apply").onClick();
    REQUIRE(model.rigProfile); CHECK(closed == 1);
    CHECK(box.getNumItems() == 4); CHECK(box.getItemId(0) == 1);
    CHECK(panel->saveState()["guide"].asInt() == 0);
    auto hidden = foh::Json::object(); hidden.set("guide", 1); hidden.set("step", 2);
    panel->loadState(hidden); CHECK(panel->saveState()["guide"].asInt() == 0); CHECK(panel->saveState()["step"].asInt() == 0);
    questionnaire.begin();
    draft(questionnaire);
    CHECK_FALSE(model.rigProfile->subCount); CHECK(box.getNumItems() == 4);
    button(questionnaire, "Skip").onClick(); CHECK(closed == 2);
    CHECK_FALSE(model.rigProfile); CHECK(box.getNumItems() == 6);
    questionnaire.begin();
    draft(questionnaire);
    button(questionnaire, "Apply").onClick(); CHECK(closed == 3);
    CHECK(model.rigProfile->subCount); CHECK(model.rigProfile->frontFillCount);
    CHECK(model.rigProfile->clusterCabinets == std::vector<int>{3, 3, 3});
    CHECK(model.rigProfile->control == foh::RigProfile::Control::Individual);
    CHECK(model.rigProfile->subArrangement == foh::RigProfile::SubArrangement::LeftRight);
    CHECK(box.getNumItems() == 6); CHECK(box.getItemId(1) == 2); CHECK(box.getItemId(2) == 3);
    // Loading an older session reintroduces the default flow.
    model.rigProfile.reset(); model.notify(AppModel::Rig); CHECK(box.getNumItems() == 6);
    // A panel created after session restoration uses the profile immediately.
    panel.reset();
    foh::RigProfile fillsOnly; fillsOnly.frontFillCount = 1; model.rigProfile = fillsOnly;
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
                CHECK(viewport->getViewArea().intersects(button(questionnaire, "Apply").getBounds()));
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
        button(*questionnaire, "Apply").triggerClick();
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
    auto apply = button(*questionnaire, "Apply").onClick;
    apply();
    model.removeListener(&listener);
    CHECK(questionnaire == nullptr);
    CHECK(model.rigProfile.has_value());
    CHECK(closed == 0);
}

TEST_CASE("Questionnaire completion is single use and Skip tolerates listener destruction", "[app][rig][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; EasySetup questionnaire(model);
    int closed = 0; questionnaire.onClose = [&] { ++closed; };
    button(questionnaire, "Apply").onClick();
    button(questionnaire, "Apply").onClick();
    button(questionnaire, "Skip").onClick();
    CHECK(closed == 1); CHECK(model.rigProfile.has_value());
    questionnaire.begin(); button(questionnaire, "Skip").onClick();
    button(questionnaire, "Apply").onClick();
    CHECK(closed == 2); CHECK_FALSE(model.rigProfile);
    auto owned = std::make_unique<EasySetup>(model);
    struct Destroy : AppModel::Listener {
        std::unique_ptr<EasySetup>& owned;
        explicit Destroy(std::unique_ptr<EasySetup>& q) : owned(q) {}
        void modelChanged(int w) override { if (w & AppModel::Rig) owned.reset(); }
    } listener(owned);
    model.addListener(&listener);
    auto skip = button(*owned, "Skip").onClick; skip();
    model.removeListener(&listener); CHECK_FALSE(owned);
}

TEST_CASE("Session reload closes Setup drafts before queued Apply or Finish", "[app][rig][routing][session][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; SetupPage setup(model);
    EasySetup* questionnaire = nullptr; RoutingWizard* wizard = nullptr;
    for (auto* child : setup.getChildren()) {
        if (auto* q = dynamic_cast<EasySetup*>(child)) questionnaire = q;
        if (auto* w = dynamic_cast<RoutingWizard*>(child)) wizard = w;
    }
    REQUIRE(questionnaire); REQUIRE(wizard);
    button(setup, "Rig questionnaire").onClick();
    model.notify(AppModel::Session);
    CHECK_FALSE(questionnaire->isVisible());
    button(*questionnaire, "Apply").onClick(); CHECK_FALSE(model.rigProfile);
    button(setup, "Easy Setup").onClick();
    wizard->setChannels({"In 1", "In 2"}, {"Out 1", "Out 2"});
    // Routing controls remain reachable through their scrollable canvas.
    auto clickWizard = [&](const char* text) {
        for (auto* child : formCanvas(*wizard).getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*>(child); b && b->getButtonText() == text) { b->onClick(); return; }
        FAIL("Missing routing button");
    };
    for (int i = 0; i < 3; ++i) clickWizard("Next");
    model.notify(AppModel::Session); model.engine.micCh = 1;
    clickWizard("Finish"); CHECK(model.engine.micCh.load() == 1); CHECK_FALSE(wizard->isVisible());
}

TEST_CASE("Main count steppers bound edits immediately and Skip discards the draft", "[app][rig][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (const char* field : {"Clusters", "Default cabinets"}) {
        AppModel model;
        EasySetup questionnaire(model);
        auto& count = slider(questionnaire, field);
        CHECK(count.getSliderStyle() == juce::Slider::IncDecButtons);
        CHECK(count.getMinimum() == 1); CHECK(count.getMaximum() == 64); CHECK(count.getInterval() == 1);
        CHECK(count.getValue() == (juce::String(field) == "Clusters" ? 2 : 1));
        for (double invalid : {-10., 0., 65., 999.}) {
            count.setValue(invalid, juce::sendNotificationSync);
            CHECK(count.getValue() == juce::jlimit(1., 64., invalid));
            CHECK_FALSE(model.rigProfile);
        }
        // The editable text box also clamps out-of-range entries and rejects empty text.
        for (const char* input : {"", "0", "999"}) {
            count.showTextBox();
            std::function<juce::TextEditor*(juce::Component&)> findEditor = [&](juce::Component& c) -> juce::TextEditor* {
                if (auto* e = dynamic_cast<juce::TextEditor*>(&c)) return e;
                for (auto* child : c.getChildren()) if (auto* e = findEditor(*child)) return e;
                return nullptr;
            };
            auto* text = findEditor(count); REQUIRE(text);
            text->setText(input); count.hideTextBox(false);
            CHECK(count.getValue() >= 1); CHECK(count.getValue() <= 64);
        }
        button(questionnaire, "Skip").onClick();
        questionnaire.begin();
        CHECK(count.getValue() == (juce::String(field) == "Clusters" ? 2 : 1));
        button(questionnaire, "Apply").onClick();
        REQUIRE(model.rigProfile); CHECK(model.rigProfile->valid());
    }
}

TEST_CASE("Per-cluster steppers and auxiliary grouping apply restore and guide asymmetric rigs", "[app][rig]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int fills : {0, 1, 12}) for (int subs : {0, 1, 32, 64}) {
        AppModel model; EasySetup q(model); q.setSize(260, 520);
        slider(q, "Default cabinets").setValue(3, juce::sendNotificationSync); slider(q, "Clusters").setValue(3, juce::sendNotificationSync);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        button(q, "Apply default to all clusters").onClick();
        CHECK(slider(q, "L cabinets").getValue() == 3); CHECK(slider(q, "C cabinets").getValue() == 3);
        slider(q, "L cabinets").setValue(2, juce::sendNotificationSync);
        slider(q, "Front fill count").setValue(fills, juce::sendNotificationSync);
        slider(q, "Sub count").setValue(subs, juce::sendNotificationSync);
        button(q, "Apply").onClick(); REQUIRE(model.rigProfile);
        CHECK(model.rigProfile->clusterCabinets == std::vector<int>{2, 3, 3});
        CHECK(model.rigProfile->frontFillCount == fills); CHECK(model.rigProfile->subCount == subs);
        auto panel = makeGuidePanel(model); panel->setSize(300, 650);
        guideBox(*panel).setSelectedId(1, juce::sendNotificationSync);
        CHECK(guideBox(*panel).getNumItems() == 4 + (fills > 0) + (subs > 0));
        bool tailored = false;
        for (auto* c : formCanvas(*panel).getChildren()) if (auto* label = dynamic_cast<juce::Label*>(c))
            tailored |= label->getText().contains("L=2, C=3, R=3");
        CHECK(tailored);
        auto state = foh::Json::object(); state.set("guide", 0); state.set("step", 1); panel->loadState(state);
        bool arrayPlacement = false;
        for (auto* c : formCanvas(*panel).getChildren()) if (auto* diagram = dynamic_cast<MicPlacementDiagram*>(c))
            arrayPlacement |= diagram->getDescription().contains("where the cabinets combine");
        CHECK(arrayPlacement);
        state.set("guide", 3); panel->loadState(state);
        juce::String tuning;
        for (auto* c : formCanvas(*panel).getChildren()) if (auto* label = dynamic_cast<juce::Label*>(c)) tuning += label->getText();
        CHECK(tuning.contains("sub-to-mains alignment") == (subs > 0)); CHECK(tuning.contains("fill alignment") == (fills > 0));
        CHECK(tuning.contains("L=2, C=3, R=3"));
        q.begin(); CHECK(slider(q, "L cabinets").getValue() == 2); CHECK(slider(q, "R cabinets").getValue() == 3);
        CHECK(slider(q, "Front fill count").getValue() == fills); CHECK(slider(q, "Sub count").getValue() == subs);
        for (auto* child : formCanvas(q).getChildren()) CHECK(formCanvas(q).getLocalBounds().contains(child->getBounds()));
    }
}
TEST_CASE("Cluster additions preserve overrides and grouped counts clamp to cabinet counts", "[app][rig]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; model.rigProfile = foh::RigProfile{};
    model.rigProfile->clusterCabinets = {2, 3, 3}; model.rigProfile->frontFillCount = 12;
    model.rigProfile->fillControl = foh::RigProfile::Control::Grouped; model.rigProfile->fillGroups = 4;
    model.rigProfile->subCount = 24; model.rigProfile->subGroups = 6;
    EasySetup q(model);
    CHECK(slider(q, "Fill groups").isEnabled()); CHECK(slider(q, "Fill groups").getValue() == 4);
    CHECK(slider(q, "Sub groups").getValue() == 6);
    slider(q, "Default cabinets").setValue(5, juce::sendNotificationSync); slider(q, "Clusters").setValue(4, juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    CHECK(slider(q, "Cluster 1 cabinets").getValue() == 2); CHECK(slider(q, "Cluster 2 cabinets").getValue() == 3);
    CHECK(slider(q, "Cluster 4 cabinets").getValue() == 5);
    slider(q, "Front fill count").setValue(1, juce::sendNotificationSync);
    slider(q, "Sub count").setValue(0, juce::sendNotificationSync);
    CHECK(slider(q, "Fill groups").getValue() == 1); CHECK_FALSE(slider(q, "Fill groups").isEnabled());
    CHECK_FALSE(slider(q, "Sub groups").isEnabled());
    button(q, "Apply").onClick(); REQUIRE(model.rigProfile);
    CHECK(model.rigProfile->clusterCabinets == std::vector<int>{2, 3, 3, 5});
    CHECK(model.rigProfile->fillRoutes() == 1); CHECK(model.rigProfile->subRoutes() == 0);
}

TEST_CASE("Applied setup summary matches the visible guides in both themes and scales", "[app][rig][layout]") {
    juce::ScopedJuceInitialiser_GUI gui;
    auto& themes = foh::theme::ThemeManager::get();
    const auto previousTheme = themes.current().id;
    for (auto theme : {"pro-dark", "pro-sunlight"}) for (int scale : {100, 200}) {
        CAPTURE(theme, scale);
        themes.setTheme(theme);
        FohLookAndFeel look;
        AppModel model; SetupPage setup(model); setup.setLookAndFeel(&look);
        setup.setSize(480, 580); setup.setVisible(true);
        setup.setTransform(juce::AffineTransform::scale(scale / 100.f));
        EasySetup* questionnaire = nullptr;
        for (auto* child : setup.getChildren()) if (auto* q = dynamic_cast<EasySetup*>(child)) questionnaire = q;
        REQUIRE(questionnaire);
        auto panel = makeGuidePanel(model);
        auto save = [&](juce::Component& c, const char* name) {
            if (auto* folder = std::getenv("FOH_SETUP_QA_DIR")) {
                juce::File dir(folder); REQUIRE(dir.createDirectory().wasOk());
                auto snapshot = c.createComponentSnapshot(c.getLocalBounds(), true, scale / 100.f);
                juce::Image image(juce::Image::RGB, snapshot.getWidth(), snapshot.getHeight(), true);
                juce::Graphics graphics(image); graphics.fillAll(foh::ui::col::background());
                graphics.drawImageAt(snapshot, 0, 0);
                auto stream = dir.getChildFile(juce::String(name) + "-" + theme + "-" + juce::String(scale) + ".png").createOutputStream();
                REQUIRE(stream); stream->setPosition(0);
                juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream)); stream->truncate();
            }
        };
        auto checkLayout = [](juce::Component& c) {
            const auto& children = c.getChildren();
            for (auto* child : children) {
                if (!child->isVisible() || child->getBounds().isEmpty()) continue;
                CHECK(c.getLocalBounds().contains(child->getBounds()));
                for (auto* other : children)
                    if (other != child && other->isVisible()) CHECK_FALSE(child->getBounds().intersects(other->getBounds()));
                if (auto* label = dynamic_cast<juce::Label*>(child); label && label->getText().isNotEmpty()) {
                    juce::GlyphArrangement text;
                    text.addJustifiedText(label->getFont(), label->getText(), 0, label->getFont().getAscent(),
                                          (float)(label->getWidth() - 10), juce::Justification::left);
                    CHECK(text.getBoundingBox(0, -1, true).getHeight() <= label->getHeight() - 2);
                }
            }
        };
        for (bool subs : {false, true}) for (bool fills : {false, true}) {
            button(setup, "Rig questionnaire").onClick();
            slider(*questionnaire, "Sub count").setValue(subs ? 1 : 0, juce::sendNotificationSync);
            slider(*questionnaire, "Front fill count").setValue(fills ? 1 : 0, juce::sendNotificationSync);
            checkLayout(formCanvas(*questionnaire));
            if (subs && fills) save(formCanvas(*questionnaire), "questionnaire");
            button(*questionnaire, "Apply").onClick();
            CHECK_FALSE(questionnaire->isVisible());
            auto* summary = dynamic_cast<juce::Label*>(formCanvas(setup).findChildWithID("setup-guide-summary")); REQUIRE(summary);
            juce::StringArray names;
            auto& guides = guideBox(*panel);
            for (int i = 0; i < guides.getNumItems(); ++i) names.add(guides.getItemText(i));
            CHECK(summary->getText() == "You'll see: " + names.joinIntoString(", ") + ".");
            CHECK(summary->isVisible());
            auto* viewport = dynamic_cast<juce::Viewport*>(formCanvas(setup).getParentComponent()->getParentComponent());
            REQUIRE(viewport); CHECK(viewport->getViewArea().contains(summary->getBounds()));
            checkLayout(formCanvas(setup));
            if (subs && fills) save(formCanvas(setup), "setup-applied");
        }
        RoutingWizard routing(model.engine); routing.setLookAndFeel(&look); routing.setSize(400, 520);
        routing.setChannels({"Input 1", "Input 2"}, {"Output 1", "Output 2"});
        routing.begin(); routing.setRigProfile(model.rigProfile);
        for (int step = 1; step <= 4; ++step) {
            checkLayout(formCanvas(routing));
            save(formCanvas(routing), ("routing-" + std::to_string(step)).c_str());
            if (step < 4) button(routing, "Next").onClick();
        }
        routing.setLookAndFeel(nullptr);
        setup.setLookAndFeel(nullptr);
    }
    themes.setTheme(previousTheme);
}
