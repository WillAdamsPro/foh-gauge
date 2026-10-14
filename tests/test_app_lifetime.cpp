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
    AppModel& state() { return model; }
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
    drainMessages(); // Allow the asynchronous notice to enter modal state before dismissing it.
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

TEST_CASE("Bad session opens leave the running app state intact and show a notice", "[app][session][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder.getChildFile("workspaces"));
    auto& model = main.state();
    model.lockedDelayMs = 4.25;
    model.rigProfile = foh::RigProfile{};
    model.rigProfile->clusters = 3;
    const auto folder = temporary.folder.getChildFile("Synthetic.fohsession");
    REQUIRE(main.writeSession(folder));
    const auto priorWorkspace = fohws::toJson(main.workspace());
    auto manifest = folder.getChildFile("session.json");
    SECTION("truncated") { REQUIRE(manifest.replaceWithText("{")); }
    SECTION("wrong type") {
        auto j = foh::Json::parse(manifest.loadFileAsString().toStdString());
        auto d = j["devices"]; d.set("input", 3); j.set("devices", d);
        REQUIRE(manifest.replaceWithText(juce::String(j.dump())));
    }
    SECTION("unknown version") {
        auto j = foh::Json::parse(manifest.loadFileAsString().toStdString()); j.set("version", 999);
        REQUIRE(manifest.replaceWithText(juce::String(j.dump())));
    }
    SECTION("invalid workspace") {
        auto j = foh::Json::parse(manifest.loadFileAsString().toStdString()); j.set("workspace", foh::Json::object());
        REQUIRE(manifest.replaceWithText(juce::String(j.dump())));
    }
    model.engine.gen = AudioEngine::Pink;
    REQUIRE_FALSE(main.readSession(folder));
    CHECK(fohws::toJson(main.workspace()) == priorWorkspace);
    CHECK(model.lockedDelayMs == 4.25); CHECK(model.engine.gen.load() == AudioEngine::Pink);
    REQUIRE(model.rigProfile); CHECK(model.rigProfile->clusters == 3);
    drainMessages();
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0);
    drainMessages(); // Allow the asynchronous notice to enter modal state before dismissing it.
    juce::ModalComponentManager::getInstance()->cancelAllModalComponents(); drainMessages();
}

TEST_CASE("Session traces colours and delays survive a different live sample rate", "[app][session][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder.getChildFile("workspaces"));
    auto& model = main.state();
    model.dm.removeAudioCallback(&model.engine); model.dm.closeAudioDevice();
    model.engine.deviceFailed = false;
    model.analysis.start();
    foh::TFResult tf; tf.sampleRate = 96000; tf.fftSize = 8; tf.averages = 16; tf.blankThreshold = 0.6;
    tf.freq = {0, 12000}; tf.magDb = {0, 2}; tf.phaseDeg = {0, -90}; tf.coherence = {1, 0.8};
    tf.H = {{1, 0}, {0, -1}}; tf.blanked = {0, 0};
    REQUIRE(model.traces.store("Synthetic", tf, 2.5));
    model.colourTrace(0, juce::Colours::cyan); model.toggleTrace(0); model.lockedDelayMs = 2.5;
    const auto folder = temporary.folder.getChildFile("Synthetic.fohsession");
    REQUIRE(main.writeSession(folder));
    auto manifestFile = folder.getChildFile("session.json");
    auto manifest = foh::Json::parse(manifestFile.loadFileAsString().toStdString());
    auto devices = manifest["devices"]; devices.set("sampleRate", 96000); devices.set("type", "Synthetic missing device");
    manifest.set("devices", devices); REQUIRE(manifestFile.replaceWithText(juce::String(manifest.dump())));
    model.clearTraces(); model.lockedDelayMs = 0;
    model.engine.sampleRate = 48000;
    // Model the epoch transition from a synchronous device setup, before its UI timer.
    model.engine.captureEpoch.fetch_add(1);
    model.multiPosition = tf; model.positionCount = 2; model.positionMask = 3;
    REQUIRE(main.readSession(folder));
    CHECK(main.workspace().id == "setup");
    CHECK(model.positionCount == 0); CHECK(model.positionMask == 0); CHECK(model.multiPosition.freq.empty());
    CHECK(model.engine.gen.load() == AudioEngine::Off);
    REQUIRE(model.traces.count() == 1); CHECK(model.traces.at(0).tf.sampleRate == 96000);
    CHECK(model.traces.at(0).tf.freq == tf.freq); CHECK(model.traces.at(0).tf.H == tf.H);
    CHECK(model.traces.at(0).refDelayMs == 2.5); CHECK_FALSE(model.traces.at(0).visible);
    CHECK(model.traceColour(0) == juce::Colours::cyan);
    drainMessages(); // Allow the asynchronous notice to enter modal state before dismissing it.
    juce::ModalComponentManager::getInstance()->cancelAllModalComponents(); drainMessages();
    CHECK(model.lockedDelayMs == 2.5);
    // The analysis command uses the running 48 kHz rate, not the saved 96 kHz.
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 3000;
    double delaySamples = 0;
    while (delaySamples != 120 && juce::Time::getMillisecondCounterHiRes() < deadline) {
        delaySamples = model.analysis.latest().refDelaySamples;
        juce::Thread::sleep(5);
    }
    CHECK(delaySamples == 120);
}
