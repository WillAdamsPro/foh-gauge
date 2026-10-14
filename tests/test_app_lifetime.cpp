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

TEST_CASE("Session notices expire with their owner before and after asynchronous display", "[app][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    for (bool display : {false, true}) {
        auto main = std::make_unique<TestMain>(temporary.folder);
        REQUIRE_FALSE(main->readSession(temporary.folder.getChildFile("Missing.fohsession")));
        if (display) {
            drainMessages();
            REQUIRE(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 1);
        }
        main.reset();
        drainMessages();
        CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
    }
}

TEST_CASE("Merged measurement panels repeatedly release listeners and pending UI work", "[app][lifetime][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    model.tf.freq = {0, 100, 1000, 10000};
    model.tf.H.assign(4, {1, 0}); model.tf.magDb.assign(4, 0);
    model.tf.phaseDeg.assign(4, 0); model.tf.coherence.assign(4, 1);
    model.sweep.response.impulse.assign(24000, 0);
    model.sweep.response.impulse[230] = 1;
    foh::Arrival arrival {}; arrival.delaySamples = 230; arrival.delayMs = 230.0 / 48;
    model.sweep.response.arrivals.push_back(arrival);
    model.sweep.etcDb.assign(24000, -120); model.sweep.etcDb[230] = 0;
    for (int pass = 0; pass < 20; ++pass) {
        REQUIRE(model.traces.store("Synthetic", model.tf, 0));
        model.selectSum(0, 0);
        std::vector<std::unique_ptr<Panel>> panels;
        for (auto id : {"SPLNumbers", "TraceList", "Impulse", "TransferFunction", "Phase", "ShowMonitoring"}) {
            auto panel = PanelRegistry::standard().create(id, model);
            REQUIRE(panel != nullptr);
            panel->setSize(600, 420);
            for (auto* child : panel->getChildren()) {
                if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
                    combo->setSelectedId(1, juce::sendNotificationAsync);
                if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child)) {
                    toggle->setToggleState(true, juce::dontSendNotification);
                    if (toggle->onClick) toggle->onClick();
                }
            }
            panel->modelChanged(AppModel::Snapshot | AppModel::Traces | AppModel::Theme);
            auto image = panel->createComponentSnapshot(panel->getLocalBounds());
            CHECK(image.isValid());
            panels.push_back(std::move(panel));
        }
        model.clearTraces();
        panels.clear(); // ComboBox notifications are still queued.
        drainMessages();
        model.notify(AppModel::Snapshot | AppModel::Traces | AppModel::Settings);
    }
}
