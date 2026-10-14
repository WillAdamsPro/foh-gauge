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
    REQUIRE(main.floatingCount() == 1);
    main.floating(0)->onClose();
    REQUIRE(main.readSession(session));
    // The isolated model has no devices; dismiss the expected fallback notice.
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
