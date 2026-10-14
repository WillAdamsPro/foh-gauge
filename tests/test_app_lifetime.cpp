#include <functional>
#include <catch2/catch_test_macros.hpp>
#include "MainComponent.h"

namespace {
struct TemporaryWorkspaces {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("foh-lifetime", "", false);
    ~TemporaryWorkspaces() { folder.deleteRecursively(); }
};
class TestMain : public MainComponent {
public:
    explicit TestMain(const juce::File& folder) : MainComponent(folder) {}
    using MainComponent::saveWorkspaceAs;
    using MainComponent::setShowMode;
    bool inShowMode() const { return showMode; }
    AppModel& appModel() { return model; }
    juce::TextButton& expertToggle() { return expertBtn; }
    void showWorkspace() { setup.setVisible(false); view.setVisible(true); openFloatingWindows(); }
    FloatingPanelWindow* floating(int index) { return floats[index]; }
    int floatingCount() const { return floats.size(); }
    fohws::Workspace& workspace() { return mgr.at(current); }
    WorkspaceView& workspaceView() { return view; }
};
void drainMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }
}

TEST_CASE("Save Workspace As releases borrowed state before the manager grows", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.selectWorkspace("rta-show");
    main.showWorkspace();
    main.floatPanel("RTA");
    REQUIRE(main.floatingCount() == 1);
    // Cross several vector capacity boundaries while preserving floating state.
    for (int i = 0; i < 20; ++i) {
        main.saveWorkspaceAs("Synthetic " + juce::String(i));
        CHECK(main.workspaceView().workspace() == &main.workspace());
        CHECK(main.workspace().name == "Synthetic " + std::to_string(i));
        REQUIRE(main.floatingCount() == 1);
        main.floating(0)->setBounds(40 + i, 40, 360, 260);
    }
    main.dockPanel("RTA");
    CHECK(main.floatingCount() == 0);
    CHECK(main.workspace().root.contains("RTA"));
}

TEST_CASE("Queued floating actions expire on workspace replacement and destruction", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    auto main = std::make_unique<TestMain>(temporary.folder);
    main->selectWorkspace("rta-show");
    main->showWorkspace();
    main->floatPanel("RTA");
    REQUIRE(main->floatingCount() == 1);
    main->floating(0)->onDock();
    main->selectWorkspace("tuning");
    const auto state = fohws::toJson(main->workspace()).dump();
    drainMessages();
    CHECK(fohws::toJson(main->workspace()).dump() == state);
    main->selectWorkspace("rta-show");
    main->floatPanel("RTA");
    REQUIRE(main->floatingCount() == 1);
    main->floating(0)->onClose();
    main.reset();
    drainMessages();
}

TEST_CASE("Embedded font references are released with the look and feel", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::Typeface::Ptr regular, semibold, bold;
    {
        FohLookAndFeel look;
        regular = look.regular; semibold = look.semibold; bold = look.bold;
        REQUIRE(regular != nullptr);
        REQUIRE(semibold != nullptr);
        REQUIRE(bold != nullptr);
    }
    CHECK(regular->getReferenceCount() == 1);
    CHECK(semibold->getReferenceCount() == 1);
    CHECK(bold->getReferenceCount() == 1);
}

TEST_CASE("Session replacement destroys windows before replacing their borrowed state", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder.getChildFile("workspaces"));
    main.selectWorkspace("rta-show");
    main.showWorkspace();
    main.floatPanel("RTA");
    const auto session = temporary.folder.getChildFile("Synthetic.fohsession");
    REQUIRE(main.writeSession(session));
    SECTION("Older session without rig answers") {}
    SECTION("Invalid optional rig answers still restore the session") {
        auto manifestFile = session.getChildFile("session.json");
        auto manifest = foh::Json::parse(manifestFile.loadFileAsString().toStdString());
        auto rig = foh::RigProfile{}.toJson(); rig.set("clusters", -1);
        manifest.set("rigProfile", rig);
        REQUIRE(manifestFile.replaceWithText(juce::String(manifest.dump())));
    }
    REQUIRE(main.floatingCount() == 1);
    main.floating(0)->onClose();
    REQUIRE(main.readSession(session));
    // Session open must return before its asynchronous device/rig fallback notice
    // is dismissed, including when JUCE modal loops are enabled.
    juce::ModalComponentManager::getInstance()->cancelAllModalComponents();
    drainMessages();
    main.selectWorkspace("rta-show");
    REQUIRE(main.floatingCount() == 1);
    CHECK(main.workspace().floating.size() == 1);
    main.floating(0)->setBounds(60, 60, 360, 260);
    main.dockPanel("RTA");
    CHECK(main.workspace().root.contains("RTA"));
}

TEST_CASE("Setup unsubscribes device changes and wizard cancels timers on destruction", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    {
        SetupPage setup(model);
        model.dm.sendChangeMessage();
    }
    model.notify(AppModel::Settings);
    drainMessages();
    float output[64] {}, loopback[64] {};
    float* outputs[] { output, loopback };
    {
        RoutingWizard wizard(model.engine);
        wizard.setChannels({"Synthetic mic", "Synthetic reference"}, {"Synthetic output", "Synthetic loopback"});
        bool started = false;
        for (auto* child : wizard.getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child))
                if (button->getButtonText() == "Test connection") { button->onClick(); started = true; }
        REQUIRE(started);
        model.engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 64, {});
        CHECK(output[16] != 0);
    }
    // A pending timer must never call back into the destroyed wizard.
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    drainMessages();
    model.engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 64, {});
    for (const auto sample : output) CHECK(sample == 0);
    for (const auto sample : loopback) CHECK(sample == 0);
    CHECK(model.engine.connectionResult() == foh::ConnectionResult::pending);
}

TEST_CASE("Expert toggle reveals every panel globally without changing saved layouts", "[app][expert]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.showWorkspace();
    CHECK_FALSE(main.appModel().expertMode());
    CHECK_FALSE(main.expertToggle().getToggleState());
    auto* guide = main.workspaceView().panel("GuideInspector");
    auto state = fohws::Json::object(); state.set("guide", 2); state.set("step", 1);
    guide->loadState(state);
    const auto guideState = guide->saveState().dump();
    const auto original = main.workspace().root;
    main.expertToggle().setToggleState(true, juce::dontSendNotification);
    main.expertToggle().onClick();
    CHECK(main.appModel().expertMode());
    CHECK(main.workspace().root == original);
    CHECK(main.workspaceView().panel("GuideInspector") == guide);
    CHECK(guide->saveState().dump() == guideState);
    for (int i = 0; i < 4; ++i) {
        main.selectWorkspace(i);
        CHECK(main.expertToggle().getToggleState());
        for (const auto& entry : PanelRegistry::standard().all())
            CHECK(main.workspaceView().panel(entry.id, false) != nullptr);
    }
    main.setShowMode(true);
    main.expertToggle().setToggleState(false, juce::dontSendNotification);
    main.expertToggle().onClick();
    main.expertToggle().setToggleState(true, juce::dontSendNotification);
    main.expertToggle().onClick();
    CHECK_FALSE(main.inShowMode());
    main.selectWorkspace("alignment");
    main.appModel().setExpertMode(false);
    CHECK_FALSE(main.expertToggle().getToggleState());
    CHECK(main.workspace().root == original);
}

TEST_CASE("Expert exposes selectable charts and settings and keeps guide navigation working", "[app][expert]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    fohws::Workspace workspace;
    workspace.root = fohws::L("TransferFunction");
    WorkspaceView view(model, PanelRegistry::standard());
    view.setSize(1200, 800); view.setVisible(true);
    view.setWorkspace(&workspace);
    view.toggleMaximise("TransferFunction");
    model.setExpertMode(true);
    view.refreshPresentation();
    int index = 0;
    for (const auto& entry : PanelRegistry::standard().all()) {
        view.activateTab({}, index++);
        auto* panel = view.panel(entry.id, false);
        REQUIRE(panel != nullptr);
        CHECK(panel->getParentComponent() != nullptr);
        CHECK(panel->isVisible());
        if (entry.id == "GuideInspector") {
            auto state = fohws::Json::object(); state.set("guide", 0); state.set("step", 1);
            panel->loadState(state);
            const int before = panel->saveState()["step"].asInt();
            bool advanced = false;
            // Guide controls live inside a scrolling form, so search the whole subtree.
            std::function<void(juce::Component&)> press = [&](juce::Component& c) {
                for (auto* child : c.getChildren()) {
                    if (auto* button = dynamic_cast<juce::TextButton*>(child))
                        if (!advanced && button->isVisible() && button->getButtonText() == "Start pink noise") {
                            button->onClick(); advanced = true;
                        }
                    press(*child);
                }
            };
            press(*panel);
            CHECK(advanced);
            CHECK(panel->saveState()["step"].asInt() == before + 1);
        }
    }
    CHECK(workspace.root == fohws::L("TransferFunction"));
    model.setExpertMode(false); view.rebuild();
    CHECK(view.panel("TransferFunction")->getParentComponent() != nullptr);
    CHECK(view.panel("GuideInspector")->getParentComponent() == nullptr);
}

TEST_CASE("Expert preference defaults off and persists both values across reload", "[app][expert]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    juce::PropertiesFile::Options options;
    options.applicationName = "FOH synthetic expert";
    options.filenameSuffix = "settings";
    options.folderName = temporary.folder.getFullPathName();
    options.osxLibrarySubFolder = "Application Support";
    // An absolute folder keeps this test entirely outside the user's preferences.
    for (bool enabled : {true, false}) {
        AppModel model;
        model.settings.closeFiles(); model.settings.setStorageParameters(options);
        if (enabled) CHECK_FALSE(model.expertMode());
        else CHECK(model.expertMode());
        model.setExpertMode(enabled);
        model.settings.closeFiles();
        model.settings.setStorageParameters(options);
        CHECK(model.expertMode() == enabled);
    }
}
