#include "ui_form_helpers.h"
#include <functional>
#include <catch2/catch_test_macros.hpp>
#include "MainComponent.h"
#include "WindowBounds.h"
#include <thread>
#include "PreferencesRecovery.h"
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif

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
    bool resetFromFloating() {
        return MainComponent::keyPressed(juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0), floats[0]);
    }
    bool pauseFromFloating() {
        return MainComponent::keyPressed(juce::KeyPress(juce::KeyPress::spaceKey), floats[0]);
    }
    juce::Viewport& viewport() { return contentViewport; }
    juce::Component& canvas() { return contentCanvas; }
    juce::Component& logicalContent() { return content; }
    std::vector<juce::Component*> toolbarControls() {
        return { &brandLockup, &switcher, &status, &wsMenuBtn, &pauseBtn, &setupBtn };
    }
    bool inShowMode() const { return showMode; }
    AppModel& appModel() { return model; }
    juce::TextButton& outputControl() { return outputOn; }
    void refreshStatus() { timerCallback(); }
    StatusPills& statusControl() { return status; }
    SetupPage& setupPage() { return setup; }
    juce::TextButton& pauseControl() { return pauseBtn; }

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
                float signal[1024], a[1024], b[1024];
                float* outputs[] {a, b};
                foh::PinkNoise noise(70); noise.process(signal, 1024);
                const float* inputs[] {signal, signal};
                while (!done.load()) {
                    if (engine.micRing.available() < 4096 && engine.refRing.available() < 4096) {
                        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 1024, {});
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
            { juce::Graphics graphics(image); plot.paint(graphics); }
            CHECK(image.getPixelAt(50, 10).getAlpha() == 0);
            CHECK(image.getPixelAt(1, 1).getAlpha() == 0);
        }
    }
    plot.setSize(120, 120);
    juce::Image image(juce::Image::ARGB, 120, 120, true);
    { juce::Graphics graphics(image); plot.paint(graphics); }
    // Read after the graphics context flushes its platform drawing commands.
    // Sample inside the plot well, clear of text and grid lines.
    CHECK(image.getPixelAt(75, 50).getAlpha() != 0);
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
        auto rig = foh::RigProfile{}.toJson(); rig.set("clusterCabinets", foh::Json::array());
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
    float output[64] {}, loopback[64] {}, input[64] {};
    const float* inputs[] {input, input};
    float* outputs[] { output, loopback };
    {
        RoutingWizard wizard(model.engine);
        wizard.setChannels({"Synthetic mic", "Synthetic reference"}, {"Synthetic output", "Synthetic loopback"});
        bool started = false;
        for (auto* child : formCanvas(wizard).getChildren())
            if (auto* button = dynamic_cast<juce::TextButton*>(child))
                if (button->getButtonText() == "Test connection") { button->onClick(); started = true; }
        REQUIRE(started);
        model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, {});
        CHECK(output[16] != 0);
    }
    // A pending timer must never call back into the destroyed wizard.
    juce::Thread::sleep(60);
    juce::Timer::callPendingTimersSynchronously();
    drainMessages();
    model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 64, {});
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

TEST_CASE("Session notice replacement and owner teardown cannot leave native animations", "[app][session][lifetime][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    const auto peers = juce::Desktop::getInstance().getNumComponents();
    auto main = std::make_unique<TestMain>(temporary.folder);
    for (int cycle = 0; cycle < 8; ++cycle) {
        INFO("notice replacement " << cycle);
        // A missing synthetic session creates a notice and closes its predecessor.
        REQUIRE_FALSE(main->readSession(temporary.folder.getChildFile("Missing.fohsession")));
        drainMessages();
        auto* modals = juce::ModalComponentManager::getInstance();
        REQUIRE(modals->getNumModalComponents() == 1);
        auto* notice = dynamic_cast<juce::AlertWindow*>(modals->getModalComponent(0));
        REQUIRE(notice != nullptr);
        REQUIRE(notice->getPeer() != nullptr);
#if JUCE_MAC
        NSWindow* native = [(NSView*) notice->getPeer()->getNativeHandle() window];
        REQUIRE(native != nil);
        CHECK(native.animationBehavior == NSWindowAnimationBehaviorNone);
#endif
    }
    main.reset();
    drainMessages();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(250);
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
    CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
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
    CHECK_FALSE(main.isExpertMode());
    auto* guide = main.workspaceView().panel("GuideInspector");
    auto state = fohws::Json::object(); state.set("guide", 2); state.set("step", 1);
    guide->loadState(state);
    const auto guideState = guide->saveState().dump();
    const auto original = main.workspace().root;
    main.setExpertMode(true);
    CHECK(main.appModel().expertMode());
    CHECK(main.workspace().root == original);
    CHECK(main.workspaceView().panel("GuideInspector") == guide);
    CHECK(guide->saveState().dump() == guideState);
    for (int i = 0; i < 4; ++i) {
        main.selectWorkspace(i);
        CHECK(main.isExpertMode());
        for (const auto& entry : PanelRegistry::standard().all())
            CHECK(main.workspaceView().panel(entry.id, false) != nullptr);
    }
    main.setShowMode(true);
    main.setExpertMode(false);
    CHECK(main.isExpertMode());
    CHECK(main.inShowMode());
    main.setShowMode(false);
    main.setExpertMode(false);
    CHECK_FALSE(main.isExpertMode());
    main.selectWorkspace("alignment");
    main.appModel().setExpertMode(false);
    CHECK_FALSE(main.isExpertMode());
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
            drainMessages();
            if (auto* alert = dynamic_cast<juce::AlertWindow*>(juce::ModalComponentManager::getInstance()->getModalComponent(0))) {
                alert->triggerButtonClick("Start noise"); drainMessages();
            }
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
    model.rigProfile->clusterCabinets = {2, 3, 3};
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
    REQUIRE(model.rigProfile); CHECK(model.rigProfile->clusterCabinets == std::vector<int>{2, 3, 3});
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
    CHECK(model.engine.micCh.load() == -1); CHECK(model.engine.refCh.load() == -1);
    CHECK(model.engine.outA.load() == -1); CHECK(model.engine.outB.load() == -1);
    CHECK(model.dm.getCurrentAudioDevice() == nullptr);
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

TEST_CASE("Audio preferences recover as a unit before device initialization", "[app][prefs][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    const juce::String device = "<DEVICESETUP deviceType=\"Synthetic\" audioDeviceRate=\"48000\" audioDeviceBufferSize=\"256\" audioDeviceInChans=\"11\"/>";
    juce::PropertySet prefs;
    prefs.setValue("setupComplete", true); prefs.setValue("audioDeviceState", device);
    prefs.setValue("micChannel", 2); prefs.setValue("refChannel", 3);
    prefs.setValue("generatorChannel", 2); prefs.setValue("loopbackChannel", -1);
    prefs.setValue("appearance", "sunlight");
    SECTION("valid") {
        CHECK_FALSE(fohapp::recoverAudioPreferences(prefs));
        CHECK(prefs.getIntValue("micChannel") == 2); CHECK(prefs.getBoolValue("setupComplete")); return;
    }
    SECTION("missing device") { prefs.removeValue("audioDeviceState"); }
    SECTION("empty device") { prefs.setValue("audioDeviceState", ""); }
    SECTION("truncated device") { prefs.setValue("audioDeviceState", "<DEVICESETUP"); }
    SECTION("wrong root") { prefs.setValue("audioDeviceState", "<PROPERTIES/>"); }
    SECTION("empty setup") { prefs.setValue("audioDeviceState", "<DEVICESETUP/>"); }
    SECTION("oversized device") { prefs.setValue("audioDeviceState", juce::String(std::string(1024 * 1024 + 1, ' '))); }
    SECTION("oversized mask") { prefs.setValue("audioDeviceState", "<DEVICESETUP deviceType=\"Synthetic\" audioDeviceInChans=\"" + juce::String(std::string(1025, '1')) + "\"/>"); }
    SECTION("bad rate") { prefs.setValue("audioDeviceState", device.replace("48000", "nan")); }
    SECTION("bad buffer") { prefs.setValue("audioDeviceState", device.replace("256", "-1")); }
    SECTION("bad channel") { prefs.setValue("refChannel", "oops"); }
    SECTION("missing channel") { prefs.removeValue("refChannel"); }
    SECTION("fractional channel") { prefs.setValue("micChannel", "1.5"); }
    SECTION("oversized channel") { prefs.setValue("generatorChannel", "1024"); }
    SECTION("wrong completion type") { prefs.setValue("setupComplete", "true"); }
    REQUIRE(fohapp::recoverAudioPreferences(prefs));
    CHECK_FALSE(prefs.getBoolValue("setupComplete")); CHECK(prefs.getValue("audioDeviceState").isEmpty());
    for (const auto* key : {"micChannel", "refChannel", "generatorChannel", "loopbackChannel"}) CHECK(prefs.getIntValue(key) == -1);
    CHECK(prefs.getValue("appearance") == "sunlight");
}

TEST_CASE("Interface preference corruption cannot publish a partial calibration map", "[app][prefs][regression]") {
    foh::LoopbackCorrection cal{48000, 2, {{1, 0}, {1, 0}}}; REQUIRE(cal.valid());
    std::map<std::string, foh::LoopbackCorrection> out{{"previous", cal}};
    auto j = foh::Json::object(); j.set("first", cal.toJson()); j.set("broken", foh::Json::object());
    for (const auto& text : {std::string(), std::string("{"), std::string("[]"), std::string("null"),
            std::string(foh::input::maxBytes + 1, ' '), j.dump()}) {
        CHECK_FALSE(fohapp::loadLoopbackPreferences(text, out));
        REQUIRE(out.size() == 1); CHECK(out.at("previous").H == cal.H);
    }
    j = foh::Json::object(); j.set("first", cal.toJson());
    REQUIRE(fohapp::loadLoopbackPreferences(j.dump(), out));
    REQUIRE(out.size() == 1); CHECK(out.at("first").H == cal.H);
    CHECK(fohapp::loadLoopbackPreferences("{}", out)); CHECK(out.empty());
}

TEST_CASE("Audio recovery overrides corrupt common preferences and tolerates fresh defaults", "[app][prefs][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::PropertySet prefs, common;
    CHECK_FALSE(fohapp::recoverAudioPreferences(prefs));
    CHECK_FALSE(prefs.getBoolValue("setupComplete"));
    common.setValue("setupComplete", true); common.setValue("audioDeviceState", "<wrong/>");
    common.setValue("micChannel", "bad"); prefs.setFallbackPropertySet(&common);
    REQUIRE(fohapp::recoverAudioPreferences(prefs));
    CHECK(prefs.getValue("audioDeviceState").isEmpty()); CHECK(prefs.getIntValue("micChannel") == -1);
    CHECK_FALSE(prefs.getBoolValue("setupComplete"));
    CHECK(prefs.getValue("setupComplete") == "0");
    CHECK_FALSE(fohapp::recoverAudioPreferences(prefs)); // Recovery remains safe on the next launch.
    prefs.setFallbackPropertySet(nullptr);
}

TEST_CASE("Scaled workspace overflow stays reachable and reset restores the origin", "[app][scale][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.setShowMode(false);
    main.showWorkspace();
    main.selectWorkspace(1);
    main.floatPanel("TransferFunction");
    REQUIRE(main.floatingCount() == 1);
    auto* floating = main.floating(0);
    auto* panel = &floating->panel;
    const auto floatingBounds = floating->getBounds();
    for (bool show : {false, true}) {
        main.setShowMode(show);
        for (int scale : {100, 125, 150, 200}) {
            for (auto size : {juce::Point<int>{640, 400}, {1512, 982}, {6000, 4000}, {80, 60}}) {
                CAPTURE(show, scale, size.x, size.y);
                main.setSize(size.x, size.y);
                const auto bounds = main.getBounds();
                main.setUiScale(scale);
                CHECK(main.getBounds() == bounds);
                for (auto* control : main.toolbarControls()) {
                    CHECK(main.logicalContent().getLocalBounds().contains(control->getBounds()));
                    // Scroll each control into view in physical canvas coordinates.
                    auto r = main.canvas().getLocalArea(control, control->getLocalBounds());
                    main.viewport().setViewPosition(r.getX(), r.getY());
                    CHECK(main.viewport().getViewArea().intersects(r));
                }
                main.viewport().setViewPosition(100000, 100000);
                CHECK(main.viewport().getViewPositionX() + main.viewport().getViewWidth() >= main.canvas().getWidth());
                CHECK(main.viewport().getViewPositionY() + main.viewport().getViewHeight() >= main.canvas().getHeight());
                if (show) REQUIRE(main.keyPressed(juce::KeyPress('0', juce::ModifierKeys::commandModifier, 0)));
                else REQUIRE(main.resetFromFloating());
                CHECK(main.uiScalePercent() == 100);
                CHECK(main.viewport().getViewPosition() == juce::Point<int>{});
                CHECK(main.inShowMode() == show);
                if (!show) {
                    REQUIRE(main.floatingCount() == 1);
                    CHECK(main.floating(0) == floating);
                    CHECK(&main.floating(0)->panel == panel);
                    CHECK(main.floating(0)->getBounds() == floatingBounds);
                }
            }
        }
    }
    main.setShowMode(false);
    main.setSize(1512, 982);
    main.setUiScale(200);
    main.fitSavedScale();
    CHECK(main.uiScalePercent() == 150);
    main.setSize(640, 400);
    main.setUiScale(200);
    main.fitSavedScale();
    CHECK(main.uiScalePercent() == 100);
    main.setSize(1500, 900);
    main.setUiScale(200);
    main.setMicrophoneAccessBlocked(true);
    main.fitSavedScale();
    CHECK(main.uiScalePercent() == 125);
    main.setMicrophoneAccessBlocked(false);
    main.setUiScale(200);
    main.fitSavedScale();
    CHECK(main.uiScalePercent() == 150);
    main.setSize(6000, 4000);
    main.setUiScale(100);
    CHECK_FALSE(main.viewport().getHorizontalScrollBar().isVisible());
    CHECK_FALSE(main.viewport().getVerticalScrollBar().isVisible());
}

TEST_CASE("Window geometry clamps both size extremes after display resolution changes", "[app][scale][window]") {
    const juce::Rectangle<int> large {-2560, -100, 2560, 1400};
    const juce::Rectangle<int> small {100, 40, 320, 200};
    for (auto area : {large, small, juce::Rectangle<int>{0, 25, 1512, 957}}) {
        for (auto bounds : {juce::Rectangle<int>{-9000, -9000, 8000, 5000},
                            juce::Rectangle<int>{9000, 9000, 1, 1}}) {
            const auto clamped = fohui::clampWindowBounds(bounds, area, 640, 400);
            CHECK(area.contains(clamped));
            CHECK(clamped.getWidth() >= juce::jmin(640, area.getWidth()));
            CHECK(clamped.getHeight() >= juce::jmin(400, area.getHeight()));
            CHECK(fohui::clampWindowBounds(clamped, area, 640, 400) == clamped);
            const auto shrunk = fohui::clampWindowBounds(clamped, small, 640, 400);
            CHECK(shrunk == small);
            CHECK(fohui::clampWindowBounds(shrunk, {}, 640, 400) == shrunk);
        }
    }
}

TEST_CASE("Generator safety persists across panels and expires with the app session", "[app][generator][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    REQUIRE(model.engine.gen.load() == AudioEngine::Off);
    REQUIRE(model.engine.genGainDb.load() == -20.f);
    auto panel = PanelRegistry::standard().create("Generator", model);
    REQUIRE(panel);
    auto controls = [&] {
        juce::Slider* slider = nullptr; juce::ToggleButton* full = nullptr;
        for (auto* child : panel->getChildren()) {
            if (auto* s = dynamic_cast<juce::Slider*>(child)) slider = s;
            if (auto* t = dynamic_cast<juce::ToggleButton*>(child)) full = t;
        }
        return std::make_pair(slider, full);
    };
    auto [gain, full] = controls(); REQUIRE(gain); REQUIRE(full);
    CHECK(gain->getMaximum() == -6);
    gain->setValue(0, juce::sendNotificationSync);
    CHECK(model.engine.genGainDb.load() == -6.f);
    full->setToggleState(true, juce::dontSendNotification); full->onClick();
    gain->setValue(0, juce::sendNotificationSync);
    CHECK(model.engine.genGainDb.load() == 0.f);
    panel.reset(); panel = PanelRegistry::standard().create("Generator", model);
    auto [newGain, newFull] = controls(); REQUIRE(newGain); REQUIRE(newFull);
    CHECK(newFull->getToggleState()); CHECK(newGain->getValue() == 0);
    newFull->setToggleState(false, juce::dontSendNotification); newFull->onClick();
    CHECK(model.engine.genGainDb.load() == -6.f);
    CHECK(newGain->getMaximum() == -6);
    AppModel fresh; CHECK_FALSE(fresh.allowsFullGeneratorLevel());
    CHECK(fresh.engine.gen.load() == AudioEngine::Off); CHECK(fresh.engine.genGainDb.load() == -20.f);
}

TEST_CASE("Generator confirmation cancels safely and is accepted once per app session", "[app][generator][regression]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    auto start = [&] { model.engine.gen = AudioEngine::Pink; model.notify(AppModel::Settings); };
    auto answer = [&](bool accept) {
        drainMessages();
        auto* modals = juce::ModalComponentManager::getInstance();
        REQUIRE(modals->getNumModalComponents() == 1);
        auto* alert = dynamic_cast<juce::AlertWindow*>(modals->getModalComponent(0)); REQUIRE(alert);
        auto* button = alert->getButton(accept ? "Start noise" : "Cancel"); REQUIRE(button);
        button->triggerClick(); drainMessages();
    };
    model.confirmGeneratorStart("Pink noise", start);
    CHECK(model.engine.gen.load() == AudioEngine::Off);
    answer(false); CHECK(model.engine.gen.load() == AudioEngine::Off);
    model.confirmGeneratorStart("Pink noise", start); answer(true);
    CHECK(model.engine.gen.load() == AudioEngine::Pink);
    model.setPaused(true); CHECK(model.engine.gen.load() == AudioEngine::Pink);
    model.stopGenerator(); CHECK(model.engine.gen.load() == AudioEngine::Off);
    model.confirmGeneratorStart("Pink noise", start);
    CHECK(model.engine.gen.load() == AudioEngine::Pink);
    CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
}

TEST_CASE("Generator controls and output stop remain visible in both themes and scales", "[app][generator][layout]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    for (bool sunlight : {false, true}) for (int scale : {100, 200}) {
        main.setSunlightTheme(sunlight); drainMessages();
        main.setUiScale(scale); main.setSize(1000 * scale / 100, 600 * scale / 100);
        main.appModel().engine.gen = AudioEngine::Pink;
        main.appModel().setPaused(true); main.refreshStatus();
        auto& output = main.outputControl(); REQUIRE(output.isVisible());
        CHECK(output.getTooltip().contains("Measuring paused. Noise output is still on."));
        CHECK(main.logicalContent().getLocalBounds().contains(output.getBounds()));
        for (auto* control : main.toolbarControls()) CHECK_FALSE(control->getBounds().intersects(output.getBounds()));
        auto panel = PanelRegistry::standard().create("Generator", main.appModel());
        REQUIRE(panel); panel->setLookAndFeel(&main.getLookAndFeel()); panel->setSize(360, 112);
        for (auto* child : panel->getChildren()) {
            CHECK(panel->getLocalBounds().contains(child->getBounds()));
            if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
                CHECK(toggle->findColour(juce::ToggleButton::tickDisabledColourId) == foh::ui::col::textSecondary());
        }
        if (auto* folder = std::getenv("FOH_GENERATOR_QA_DIR")) {
            juce::File dir(folder); dir.createDirectory();
            auto suffix = juce::String(sunlight ? "sunlight-" : "dark-") + juce::String(scale) + ".png";
            main.appModel().engine.gen = AudioEngine::Sweep; main.appModel().notify(AppModel::Settings);
            auto save = [&](juce::Component& c, const juce::String& name) {
                auto snapshot = c.createComponentSnapshot(c.getLocalBounds(), true, scale / 100.f);
                juce::Image image(juce::Image::RGB, snapshot.getWidth(), snapshot.getHeight(), true);
                juce::Graphics graphics(image); graphics.fillAll(foh::ui::col::cardBackground());
                graphics.drawImageAt(snapshot, 0, 0);
                auto stream = dir.getChildFile(name + suffix).createOutputStream(); REQUIRE(stream);
                stream->setPosition(0);
                juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream)); stream->truncate();
            };
            save(main.logicalContent(), "toolbar-"); save(*panel, "generator-");
        }
        panel->setLookAndFeel(nullptr);
        output.onClick(); CHECK(main.appModel().engine.gen.load() == AudioEngine::Off);
        CHECK_FALSE(output.isVisible());
    }
}

TEST_CASE("Stopping or destroying a pending generator confirmation cannot start output", "[app][generator][lifetime]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    bool started = false;
    for (bool display : {false, true}) {
        auto model = std::make_unique<AppModel>();
        model->confirmGeneratorStart("Pink noise", [&] { started = true; });
        if (display) drainMessages();
        model->stopGenerator(); drainMessages(); CHECK_FALSE(started);
        CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
        model->confirmGeneratorStart("Pink noise", [&] { started = true; });
        if (display) drainMessages();
        model.reset(); drainMessages(); CHECK_FALSE(started);
        CHECK(juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0);
    }
}

TEST_CASE("Measurement toolbar fits and every status has a Setup destination", "[app][toolbar][layout]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    TemporaryWorkspaces temporary;
    TestMain main(temporary.folder);
    main.setShowMode(false);
    for (bool sunlight : {false, true}) for (int scale : {100, 200}) {
        main.setSunlightTheme(sunlight); drainMessages();
        main.setSize(1024, 768); main.setUiScale(scale);
        main.appModel().micPeak = main.appModel().refPeak = 0;
        main.refreshStatus();
        auto controls = main.toolbarControls();
        for (size_t i = 0; i < controls.size(); ++i) {
            CHECK(main.logicalContent().getLocalBounds().contains(controls[i]->getBounds()));
            for (size_t j = i + 1; j < controls.size(); ++j)
                CHECK_FALSE(controls[i]->getBounds().intersects(controls[j]->getBounds()));
        }
        auto& status = main.statusControl();
        REQUIRE(status.pills.size() == 6);
        CHECK(status.pills[1].text == juce::String::fromUTF8("Mic \xe2\x80\x94"));
        CHECK(status.pills[2].text == juce::String::fromUTF8("Ref \xe2\x80\x94"));
        for (float peak : {0.f, 0.1f, 1.f}) {
            main.appModel().micPeak = main.appModel().refPeak = peak; main.refreshStatus();
            CHECK(status.pills[1].state == status.pills[2].state);
        }
        for (bool paused : {true, false}) {
            main.appModel().setPaused(paused); main.refreshStatus();
            CHECK(main.pauseControl().getButtonText() == (paused ? "Resume measuring" : "Pause measuring"));
            if (paused) CHECK(status.pills[0].text == "Paused");
        }
        REQUIRE(main.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)));
        CHECK(main.appModel().paused);
        CHECK(main.pauseControl().getButtonText() == "Resume measuring");
        CHECK(status.pills[0].text == "Paused");
        main.showWorkspace();
        if (auto* folder = std::getenv("FOH_TOOLBAR_QA_DIR")) {
            juce::File dir(folder); dir.createDirectory();
            auto image = main.logicalContent().createComponentSnapshot({0, 0, main.logicalContent().getWidth(), 44}, true, scale / 100.f);
            auto stream = dir.getChildFile(juce::String(sunlight ? "sunlight-" : "dark-") + juce::String(scale) + ".png").createOutputStream();
            REQUIRE(stream); juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream));
        }
        for (const auto pill : status.pills) {
            status.onAction(pill.action);
            CHECK(main.setupPage().isVisible());
            CHECK((int) main.setupPage().focusedSection() == pill.action);
        }
    }
    // At a wide size all pills are real focusable buttons, with working clicks.
    main.setUiScale(100); main.setSize(3200, 900); main.refreshStatus();
    int clicked = 0;
    for (auto* child : main.statusControl().getChildren()) {
        if (auto* button = dynamic_cast<juce::Button*>(child); button && button->isVisible()) {
            button->onClick(); ++clicked;
            CHECK(main.setupPage().isVisible());
        }
    }
    CHECK(clicked == 6);
    main.showWorkspace(); main.selectWorkspace("tuning"); main.floatPanel("RTA");
    REQUIRE(main.floatingCount() == 1);
    const bool wasPaused = main.appModel().paused;
    REQUIRE(main.pauseFromFloating());
    CHECK(main.appModel().paused != wasPaused);
}
