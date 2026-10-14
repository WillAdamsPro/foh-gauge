#include <functional>
#include <catch2/catch_test_macros.hpp>
#include "MainComponent.h"
#include <thread>

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
    AppModel& state() { return model; }
    void showWorkspace() { setup.setVisible(false); view.setVisible(true); openFloatingWindows(); }
    FloatingPanelWindow* floating(int index) { return floats[index]; }
    int floatingCount() const { return floats.size(); }
    fohws::Workspace& workspace() { return mgr.at(current); }
    WorkspaceView& workspaceView() { return view; }
    int selectedWorkspace() const { return current; }
    void clickSwitcher(int index) { switcher.setSelected(index, true); }
    bool commandWorkspace(int index) {
        const juce::KeyPress key('1' + index, juce::ModifierKeys::commandModifier, 0);
        return presetCommands.commandManager().invokeDirectly(
            presetCommands.keyMappings()->findCommandForKeyPress(key), false);
    }
};
void drainMessages() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }
}

TEST_CASE("Docking accepts an ID borrowed from the floating state being erased", "[app][lifetime][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.setShowMode(false); main.selectWorkspace("tuning"); main.showWorkspace();
    main.floatPanel("RTA"); main.floatPanel("TransferFunction");
    REQUIRE(main.floatingCount() == 2);
    // Erasing the first element moves the next panel's ID into its storage.
    main.dockPanel(main.workspace().floating.front().panel);
    CHECK(main.workspace().root.contains("RTA"));
    CHECK_FALSE(main.workspace().root.contains("TransferFunction"));
    REQUIRE(main.floatingCount() == 1);
    CHECK(main.floating(0)->panel.panelId() == "TransferFunction");
    // Erasing the last element also destroys the borrowed string itself.
    main.dockPanel(main.workspace().floating.front().panel);
    CHECK(main.workspace().root.contains("TransferFunction"));
    CHECK(main.floatingCount() == 0);
}

TEST_CASE("Rapid floating close and recreate expires actions for the old window", "[app][lifetime][regression][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    {
        TestMain main(temporary.folder);
        main.setShowMode(false); main.selectWorkspace("tuning"); main.showWorkspace();
        auto* panel = main.workspaceView().panel("RTA");
        for (int cycle = 0; cycle < 24; ++cycle) {
            main.floatPanel("RTA");
            REQUIRE(main.floatingCount() == 1);
            juce::Component::SafePointer<FloatingPanelWindow> old = main.floating(0);
            main.floating(0)->closeButtonPressed();
            main.floating(0)->onDock();
            main.dockPanel("RTA");
            CHECK(old == nullptr);
            main.floatPanel("RTA");
            drainMessages();
            REQUIRE(main.floatingCount() == 1);
            CHECK(main.floating(0)->getContentComponent() == panel);
            CHECK_FALSE(main.workspace().root.contains("RTA"));
            main.dockPanel("RTA");
            CHECK(main.workspace().root.contains("RTA"));
        }
    }
    drainMessages();
    CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
}

TEST_CASE("Show mode releases multiple floats and restores missing-display state on Escape", "[app][lifetime][show][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    {
        TestMain main(temporary.folder);
        main.setShowMode(false); main.selectWorkspace("rta-show"); main.showWorkspace();
        for (const auto* id : {"RTA", "SPLNumbers", "ShowMonitoring"}) main.floatPanel(id);
        REQUIRE(main.floatingCount() == 3);
        auto* monitoring = main.workspaceView().panel("ShowMonitoring");
        std::vector<juce::Component::SafePointer<FloatingPanelWindow>> old;
        for (int i = 0; i < main.floatingCount(); ++i) {
            old.emplace_back(main.floating(i));
            main.floating(i)->setPinned(true);
            main.floating(i)->onClose(); main.floating(i)->onDock();
        }
        // All windows must be gone before their borrowed states can be edited.
        main.setShowMode(true);
        for (const auto& window : old) CHECK(window == nullptr);
        CHECK(main.floatingCount() == 0);
        CHECK(monitoring->getParentComponent() != nullptr);
        const auto root = main.workspace().root;
        for (auto& state : main.workspace().floating) {
            state.displayId = "synthetic-disconnected-display";
            state.bounds = {-10000, -10000, 700, 400};
        }
        const auto saved = fohws::toJson(main.workspace()).dump();
        for (int modifiers : {int(juce::ModifierKeys::ctrlModifier),
             juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier}) {
            CHECK_FALSE(main.keyPressed(juce::KeyPress(juce::KeyPress::tabKey, modifiers, 0)));
            CHECK(main.selectedWorkspace() == 3);
        }
        drainMessages();
        CHECK(fohws::toJson(main.workspace()).dump() == saved);
        REQUIRE(main.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
        REQUIRE(main.floatingCount() == 3);
        CHECK(main.workspace().root == root);
        const auto displays = currentDisplays();
        REQUIRE_FALSE(displays.empty());
        for (int i = 0; i < main.floatingCount(); ++i) {
            auto* window = main.floating(i);
            const auto& state = main.workspace().floating[(size_t) i];
            CHECK(state.displayId != "synthetic-disconnected-display");
            CHECK(state.alwaysOnTop);
            CHECK(window->getContentComponent() == &window->panel);
            CHECK(window->panel.getParentComponent() == window);
            window->parentSizeChanged();
            auto copy = state;
            CHECK(fohws::clampToDisplays(copy, displays,
                foh::theme::ws::floatMinWidth, foh::theme::ws::floatMinHeight) == state.bounds);
        }
        CHECK(main.workspaceView().panel("ShowMonitoring") == monitoring);
    }
    drainMessages();
    CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
    WorkspaceManager relaunched(temporary.folder);
    REQUIRE(relaunched.at(3).floating.size() == 3);
    for (const auto& state : relaunched.at(3).floating)
        CHECK(state.displayId != "synthetic-disconnected-display");
}

TEST_CASE("Workspace switching and presentation changes preserve live analysis and window ownership", "[app][lifetime][stress][concurrent]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    {
        TestMain main(temporary.folder);
        main.setShowMode(false);
        main.showWorkspace();
        auto& model = main.appModel();
        // Stop hardware I/O before introducing a single synthetic producer.
        model.dm.removeAudioCallback(&model.engine);
        model.dm.closeAudioDevice();
        model.engine.micCh = 0; model.engine.refCh = 1;
        model.engine.sampleRate = 48000;
        model.engine.deviceFailed = false;
        model.engine.state = AudioEngine::Ready;
        // fohapp_tests uses the hardware-free AppModel stand-in from
        // test_guide_panel.cpp. Run the real worker and deliver its snapshots
        // explicitly rather than relying on that stand-in's inert timer.
        model.analysis.start();
        auto deliverSnapshot = [&] {
            const auto& snapshot = model.analysis.latest();
            model.tf = snapshot.tf; model.rtaDb = snapshot.rtaDb;
            model.rtaBands = snapshot.rtaBands; model.conv = snapshot.convergence;
            model.notify(AppModel::Snapshot | AppModel::Capture);
        };
        struct Producer {
            AudioEngine& engine;
            std::atomic<bool> done {false};
            std::atomic<int> blocks {0};
            std::thread worker;
            explicit Producer(AudioEngine& e) : engine(e), worker([this] {
                float signal[1024];
                foh::PinkNoise noise(70); noise.process(signal, 1024);
                const float* inputs[] {signal, signal};
                while (!done.load()) {
                    if (engine.micRing.available() < 4096 && engine.refRing.available() < 4096) {
                        engine.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, 1024, {});
                        ++blocks;
                    }
                    // Test pacing only; the production callback remains RT-safe.
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }) {}
            ~Producer() { done = true; worker.join(); }
        } producer(model.engine);
        auto waitFor = [&](auto predicate) {
            const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000;
            while (!predicate() && juce::Time::getMillisecondCounterHiRes() < deadline) {
                drainMessages(); deliverSnapshot();
            }
            return predicate();
        };
        REQUIRE(waitFor([&] { return model.tf.averages > 0; }));
        const auto initialBlocks = producer.blocks.load();
        std::map<std::string, Panel*> identities;
        const char* floatingPanels[] {"SignalMeters", "TransferFunction", "RTA", "SPLNumbers"};
        for (int cycle = 0; cycle < 48; ++cycle) {
            const int index = cycle % 4;
            INFO("switch " << cycle);
            switch (cycle % 3) {
                case 0: REQUIRE(main.commandWorkspace(index)); break;
                case 1: main.clickSwitcher(index); break;
                default:
                    REQUIRE(main.keyPressed(juce::KeyPress(juce::KeyPress::tabKey,
                        juce::ModifierKeys::ctrlModifier, 0)));
                    break;
            }
            REQUIRE(main.selectedWorkspace() == index);
            auto* panel = main.workspaceView().panel(floatingPanels[index]);
            if (identities.count(floatingPanels[index])) CHECK(panel == identities.at(floatingPanels[index]));
            else identities.emplace(floatingPanels[index], panel);
            main.floatPanel(floatingPanels[index]);
            REQUIRE(main.floatingCount() == 1);
            CHECK(main.floating(0)->getContentComponent() == panel);
            CHECK(panel->getParentComponent() == main.floating(0));
            main.floating(0)->setPinned(cycle % 2 == 0);
            main.floating(0)->parentSizeChanged();
            main.setUiScale((cycle % 2 == 0) ? 200 : 100);
            if (cycle % 4 == 0) {
                model.capture.active = true;
                model.capture.name = "Synthetic switch capture";
                REQUIRE(model.analysis.send(foh::Command::resetAverages(cycle + 1)));
                model.setAverages(16);
            }
            if (cycle % 4 == 3) {
                juce::Component::SafePointer<FloatingPanelWindow> old = main.floating(0);
                // Queued docking must expire when Show mode destroys its window.
                main.floating(0)->onDock();
                main.setShowMode(true);
                CHECK(old == nullptr);
                CHECK(main.floatingCount() == 0);
                const auto saved = fohws::toJson(main.workspace()).dump();
                CHECK_FALSE(main.keyPressed(juce::KeyPress(juce::KeyPress::tabKey,
                    juce::ModifierKeys::ctrlModifier, 0)));
                drainMessages();
                CHECK(fohws::toJson(main.workspace()).dump() == saved);
                REQUIRE(main.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
                REQUIRE(main.floatingCount() == 1);
            }
            // Reverse cycling wraps too, then the top bar returns to this preset.
            REQUIRE(main.keyPressed(juce::KeyPress(juce::KeyPress::tabKey,
                juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)));
            CHECK(main.selectedWorkspace() == (index + 3) % 4);
            main.clickSwitcher(index);
            REQUIRE(main.floatingCount() == 1);
            drainMessages();
            deliverSnapshot();
        }
        REQUIRE(waitFor([&] { return model.analysis.latest().resetSeq == 45 && model.tf.averages > 0; }));
        CHECK(producer.blocks.load() > initialBlocks);
        CHECK(model.tf.averages > 0);
        CHECK_FALSE(model.rtaDb.empty());
        model.cancelSessionOperations();
        main.setUiScale(100);
        main.floating(0)->onClose(); // queued work also expires on shell teardown
    }
    drainMessages();
    juce::Timer::callPendingTimersSynchronously();
    foh::theme::ThemeManager::get().sendChangeMessage();
    drainMessages();
    CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
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

TEST_CASE("Floating panel keyboard focus keeps workspace cycling and scale shortcuts reachable", "[app][lifetime][workspace]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.selectWorkspace("tuning");
    main.showWorkspace();
    main.floatPanel("RTA");
    REQUIRE(main.floatingCount() == 1);
    auto* window = main.floating(0);
    window->panel.setWantsKeyboardFocus(true);
    window->toFront(true);
    window->panel.grabKeyboardFocus();
    REQUIRE(window->panel.hasKeyboardFocus(true));
    REQUIRE(window->getPeer()->handleKeyPress(juce::KeyPress('=', juce::ModifierKeys::commandModifier, 0)));
    CHECK(main.uiScalePercent() == 125);
    REQUIRE(window->getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::ctrlModifier, 0)));
    CHECK(main.selectedWorkspace() == 3);
    main.floatPanel("SPLNumbers");
    REQUIRE(main.floatingCount() == 1);
    window = main.floating(0);
    window->panel.setWantsKeyboardFocus(true);
    window->toFront(true); window->panel.grabKeyboardFocus();
    REQUIRE(window->getPeer()->handleKeyPress(juce::KeyPress(juce::KeyPress::tabKey,
        juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)));
    CHECK(main.selectedWorkspace() == 2);
    REQUIRE(main.floatingCount() == 1);
    window = main.floating(0);
    window->toFront(true); window->panel.grabKeyboardFocus();
    REQUIRE(window->getPeer()->handleKeyPress(juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0)));
    CHECK(main.uiScalePercent() == 100);
    REQUIRE(window->getPeer()->handleKeyPress(juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0)));
    drainMessages();
    CHECK(main.selectedWorkspace() == 0);
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

TEST_CASE("Multiple floating windows release listeners before state-vector edits and teardown", "[app][lifetime][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    {
        TestMain main(temporary.folder);
        main.selectWorkspace("tuning"); main.showWorkspace();
        for (const auto* id : {"RTA", "TransferFunction", "Inspector"}) main.floatPanel(id);
        REQUIRE(main.floatingCount() == 3);
        std::vector<juce::Component::SafePointer<FloatingPanelWindow>> old;
        for (int i = 0; i < 3; ++i) old.emplace_back(main.floating(i));
        // Closing a middle entry must replace every window borrowing vector
        // elements. Other already-queued actions must not affect replacements.
        main.floating(1)->onClose();
        main.floating(2)->onDock();
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 2000;
        while (main.floatingCount() == 3 && juce::Time::getMillisecondCounterHiRes() < deadline) drainMessages();
        for (const auto& pointer : old) CHECK(pointer == nullptr);
        REQUIRE(main.floatingCount() == 2);
        CHECK(main.workspace().floating.size() == 2);
        for (int i = 0; i < 2; ++i) {
            auto* window = main.floating(i);
            const auto id = window->panel.panelId();
            window->setBounds(60 + i * 100, 60, 500, 300);
            const auto& states = main.workspace().floating;
            const auto state = std::find_if(states.begin(), states.end(), [&](const auto& s) { return s.panel == id; });
            REQUIRE(state != states.end());
            CHECK(state->bounds.x == window->getX());
            window->onDock();
        }
        main.selectWorkspace("alignment");
        drainMessages();
        main.selectWorkspace("tuning");
        REQUIRE(main.floatingCount() == 2);
        main.appModel().notify(AppModel::Snapshot | AppModel::Settings | AppModel::Capture);
    }
    drainMessages();
    CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
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

TEST_CASE("Plots skip painting when workspace sizing leaves no plot area", "[app][workspace]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    PlotComponent plot;
    plot.bars = true;
    plot.series.push_back({{100, 1000}, {-30, -20}, juce::Colours::white});
    plot.barLo = {80, 800}; plot.barHi = {120, 1200};
    for (bool legend : {false, true}) {
        plot.showLegend = legend;
        for (const auto size : {juce::Point<int>{0, 0}, {1, 1}, {60, 100}, {100, 32}}) {
            plot.setSize(size.x, size.y);
            juce::Image image(juce::Image::ARGB, 120, 120, true);
            juce::Graphics graphics(image); plot.paint(graphics);
            CHECK(image.getPixelAt(50, 10).getAlpha() == 0);
            CHECK(image.getPixelAt(1, 1).getAlpha() == 0);
        }
    }
    plot.setSize(120, 120);
    juce::Image image(juce::Image::ARGB, 120, 120, true);
    juce::Graphics graphics(image); plot.paint(graphics);
    CHECK(image.getPixelAt(50, 10).getAlpha() != 0);
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
