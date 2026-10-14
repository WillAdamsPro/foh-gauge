#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Panels.h"
#include "WorkspaceView.h"
#include "ShowMonitoring.h"
#include <chrono>
#include <thread>
#include <random>
#include <iostream>
#ifndef _WIN32
#include <sys/resource.h>
#endif

namespace {
foh::TFResult syntheticTF(double gain = 0) {
    foh::TFResult tf; tf.fftSize = 16384; tf.averages = 16;
    for (int i = 0; i <= tf.fftSize / 2; ++i) {
        tf.freq.push_back(i * tf.sampleRate / tf.fftSize);
        tf.magDb.push_back(gain); tf.coherence.push_back(1);
    }
    return tf;
}
void click(Panel& p, const juce::String& text) {
    for (auto* child : p.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b && b->getButtonText() == text) { b->onClick(); return; }
    FAIL("Missing button " << text);
}
juce::String status(Panel& p) {
    for (auto* child : p.getChildren()) if (auto* label = dynamic_cast<juce::Label*>(child)) return label->getText();
    return {};
}
void selectReference(Panel& p) {
    for (auto* child : p.getChildren()) if (auto* box = dynamic_cast<juce::ComboBox*>(child)) box->setSelectedId(1, juce::sendNotificationSync);
}
}

TEST_CASE("W7 compares coherent TF magnitude on logarithmic frequencies", "[app][show]") {
    auto ref = syntheticTF(), live = syntheticTF(4);
    REQUIRE(fohui::showDrift(live, ref).valid);
    REQUIRE(fohui::showDrift(live, ref).rmsDb == Catch::Approx(4));
    live = syntheticTF(-4);
    REQUIRE(fohui::showDrift(live, ref).rmsDb == Catch::Approx(4));
    // Ignore unreliable regions, rather than treating silence as system drift.
    for (size_t i = 0; i < live.freq.size(); ++i) if (live.freq[i] > 1000) { live.coherence[i] = 0.2; live.magDb[i] = 100; }
    REQUIRE(fohui::showDrift(live, ref).valid);
    REQUIRE(fohui::showDrift(live, ref).rmsDb == Catch::Approx(4));
    std::fill(live.coherence.begin(), live.coherence.end(), 0.2);
    REQUIRE_FALSE(fohui::showDrift(live, ref).valid);
    live = syntheticTF(); live.sampleRate = 96000;
    REQUIRE_FALSE(fohui::showDrift(live, ref).valid);
    REQUIRE_FALSE(fohui::showDrift({}, ref).valid);
    ref = syntheticTF(); ref.coherence.assign(ref.coherence.size(), std::numeric_limits<double>::quiet_NaN());
    REQUIRE_FALSE(fohui::showDrift(syntheticTF(), ref).valid);
    ref = syntheticTF(); ref.blanked.assign(ref.freq.size(), 1);
    REQUIRE_FALSE(fohui::showDrift(syntheticTF(), ref).valid);
}

TEST_CASE("W7 alerts require sustained drift and reset when data is unreliable", "[app][show]") {
    fohui::ShowDriftAlert alert;
    REQUIRE_FALSE(alert.update({true, 4})); REQUIRE_FALSE(alert.update({true, 4}));
    REQUIRE(alert.update({true, 4})); REQUIRE_FALSE(alert.update({}));
    REQUIRE_FALSE(alert.update({true, 4})); REQUIRE_FALSE(alert.update({true, 2.9}));
    REQUIRE_FALSE(alert.update({true, 3})); REQUIRE_FALSE(alert.update({true, 3})); REQUIRE(alert.update({true, 3}));
}

TEST_CASE("W7 monitoring silences generators and chimes while preserving capture", "[app][show][audio]") {
    AudioEngine engine; engine.gen = AudioEngine::Pink; engine.monitoringSilent = true;
    engine.playChime();
    float mic[512], ref[512], a[512], b[512];
    std::fill_n(mic, 512, 0.1f); std::fill_n(ref, 512, 0.1f);
    const float* inputs[] {mic, ref}; float* outputs[] {a, b};
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 512, {});
    for (int i = 0; i < 512; ++i) { REQUIRE(a[i] == 0); REQUIRE(b[i] == 0); }
    REQUIRE(engine.micRing.available() == 512); REQUIRE(engine.refRing.available() == 512);
    engine.gen = AudioEngine::Off; engine.monitoringSilent = false;
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 512, {});
    for (auto value : a) REQUIRE(value == 0); // suppressed chime never resumes
}

TEST_CASE("W7 panel owns a stable reference and releases silent mode", "[app][show]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; REQUIRE(model.traces.store("Synthetic reference", syntheticTF(), 0));
    auto panel = PanelRegistry::standard().create("ShowMonitoring", model);
    REQUIRE(panel); selectReference(*panel); model.engine.gen = AudioEngine::Pink;
    click(*panel, "Monitor"); REQUIRE(model.engine.monitoringSilent.load()); REQUIRE(model.engine.gen.load() == AudioEngine::Off);
    model.removeTrace(0); // monitoring retains its own copy
    REQUIRE(model.engine.monitoringSilent.load());
    click(*panel, "Stop monitoring"); REQUIRE_FALSE(model.engine.monitoringSilent.load());
    REQUIRE(model.traces.store("Synthetic replacement", syntheticTF(), 0)); selectReference(*panel);
    click(*panel, "Monitor"); panel.reset(); REQUIRE_FALSE(model.engine.monitoringSilent.load());
    REQUIRE(model.engine.gen.load() == AudioEngine::Off);
}

// Set FOH_W7_CPU=1 to run the optional real-time, visible synthetic acceptance check.
// Process CPU includes callback feeding, analysis, snapshot copying and chart rendering.
TEST_CASE("W7 visible synthetic programme CPU measurement", "[app][show][cpu]") {
#ifdef _WIN32
    SKIP("Optional process CPU measurement requires POSIX getrusage");
#else
    if (juce::SystemStats::getEnvironmentVariable("FOH_W7_CPU", "") != "1") return;
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; model.engine.state = AudioEngine::Ready;
    model.analysis.start();
    auto panel = PanelRegistry::standard().create("ShowMonitoring", model);
    panel->setSize(1000, 700); panel->addToDesktop(juce::ComponentPeer::windowIsTemporary); panel->setVisible(true);
    std::atomic<bool> running {true}; std::atomic<float> gain {1};
    std::thread feeder([&] {
        std::mt19937 rng(7); std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
        float mic[512], ref[512], a[512], b[512]; const float* inputs[] {mic, ref};
        float* outputs[] {a, b};
        auto next = std::chrono::steady_clock::now();
        while (running.load()) {
            for (int i = 0; i < 512; ++i) { ref[i] = noise(rng); mic[i] = ref[i] * gain.load(); }
            model.engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 512, {});
            next += std::chrono::microseconds(10667); std::this_thread::sleep_until(next);
        }
    });
    auto pump = [&] {
        const auto& snap = model.analysis.latest(); model.tf = snap.tf;
        model.rtaDb = snap.rtaDb; model.rtaBands = snap.rtaBands; model.micLevelValid = snap.micLevelValid;
        model.refPeak = model.engine.refPeak.exchange(0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(67);
    };
    for (int i = 0; i < 45; ++i) pump(); // warm up analysis and GUI
    bool stored = model.traces.store("Synthetic programme", model.tf, 0);
    model.notify(AppModel::Traces); selectReference(*panel); click(*panel, "Monitor");
    rusage before {}, after {}; getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 90; ++i) pump();
    const auto steadyStatus = status(*panel);
    gain = 1.585f; // +4 dB coherent system gain drift
    for (int i = 0; i < 105; ++i) pump();
    const auto driftStatus = status(*panel);
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    getrusage(RUSAGE_SELF, &after);
    {
        juce::FileOutputStream imageFile(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("foh-w7-panel.png"));
        juce::PNGImageFormat png;
        png.writeImageToStream(panel->createComponentSnapshot(panel->getLocalBounds()), imageFile);
    }
    model.micLevelValid = false; model.refPeak = 0; model.setPaused(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1100);
    const auto missingStatus = status(*panel);
    auto seconds = [](timeval t) { return double(t.tv_sec) + t.tv_usec / 1e6; };
    const double cpu = 100 * (seconds(after.ru_utime) + seconds(after.ru_stime) - seconds(before.ru_utime) - seconds(before.ru_stime)) / wall;
    std::cout << "W7 process CPU: " << cpu << "% over " << wall << " s, 48 kHz/512, 16384 FFT, visible 1000x700 panel\n"
              << steadyStatus << "\n" << driftStatus << "\n" << missingStatus << "\n";
    running = false; feeder.join(); model.analysis.stop(); panel->removeFromDesktop();
    REQUIRE(stored); REQUIRE(steadyStatus.startsWith("Monitoring:")); REQUIRE(driftStatus.startsWith("DRIFT ALERT:"));
    REQUIRE(missingStatus.startsWith("Waiting:")); REQUIRE(cpu < 10);
#endif
}

TEST_CASE("Show mode exposes W7 even with an older saved workspace", "[app][show][workspace]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; WorkspaceView view(model, PanelRegistry::standard());
    auto workspaces = fohws::builtInPresets(); auto& workspace = workspaces.back();
    workspace.root = fohws::LayoutNode::leaf("RTA");
    view.setSize(1000, 700); view.setWorkspace(&workspace);
    view.setShowMode(true);
    auto* monitoring = view.panel("ShowMonitoring", false);
    REQUIRE(monitoring); REQUIRE(monitoring->getParentComponent());
    REQUIRE_FALSE(view.panel("RTA")->getParentComponent());
    REQUIRE(workspace.root.panel == "RTA");
    view.setShowMode(false);
    REQUIRE(view.panel("RTA")->getParentComponent());
    REQUIRE_FALSE(monitoring->getParentComponent());
}

TEST_CASE("W7 timers and plots survive repeated monitoring restarts and destruction", "[app][show][stress]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    REQUIRE(model.traces.store("Synthetic reference", syntheticTF(), 0));
    model.engine.state = AudioEngine::Ready;
    for (int i = 0; i < 200; ++i) {
        auto panel = PanelRegistry::standard().create("ShowMonitoring", model);
        selectReference(*panel);
        for (int restart = 0; restart < 5; ++restart) {
            click(*panel, "Monitor"); CHECK(model.engine.monitoringSilent.load());
            click(*panel, "Stop monitoring"); CHECK_FALSE(model.engine.monitoringSilent.load());
        }
        click(*panel, "Monitor");
        // Destroy with its timer still armed; all callbacks must expire.
        panel.reset();
        CHECK_FALSE(model.engine.monitoringSilent.load());
    }
    auto panel = PanelRegistry::standard().create("ShowMonitoring", model);
    selectReference(*panel); click(*panel, "Monitor");
    panel->setSize(1000, 700); panel->addToDesktop(0); panel->setVisible(true);
    model.tf = syntheticTF(); model.micLevelValid = true; model.refPeak = 0.1f;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(1100);
    for (auto* child : panel->getChildren()) if (auto* plot = dynamic_cast<PlotComponent*>(child)) {
        CHECK(plot->series.size() <= 2);
        for (const auto& series : plot->series) CHECK(series.f.size() <= model.tf.freq.size());
    }
    panel.reset(); CHECK_FALSE(model.engine.monitoringSilent.load());
    juce::Timer::callPendingTimersSynchronously();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK_FALSE(model.engine.monitoringSilent.load());
}

TEST_CASE("W7 alert counters remain bounded across twelve hours of synthetic updates", "[app][show][stress]") {
    fohui::ShowDriftAlert alert;
    bool correct = true;
    for (int second = 0; second < 12 * 3600; ++second) {
        const int phase = second % 60;
        const auto drift = phase < 30 ? fohui::ShowDrift{true, 4} : fohui::ShowDrift{};
        correct &= alert.update(drift) == (phase >= 2 && phase < 30);
        correct &= alert.consecutive >= 0 && alert.consecutive <= 3;
    }
    CHECK(correct);
}

TEST_CASE("W7 rejects malformed clocks grids silent references and overflow", "[app][show][edges]") {
    const auto ref = syntheticTF();
    for (int variant = 0; variant < 10; ++variant) {
        auto bad = ref;
        switch (variant) {
            case 0: bad.sampleRate = NAN; break;
            case 1: bad.sampleRate = 0; break;
            case 2: bad.fftSize = 1; break;
            case 3: bad.freq[5] = NAN; break;
            case 4: std::reverse(bad.freq.begin(), bad.freq.end()); break;
            case 5: bad.magDb.assign(bad.freq.size(), -400); break;
            case 6: bad.magDb.assign(bad.freq.size(), 1e300); break;
            case 7: bad.coherence.assign(bad.freq.size(), 2); break;
            case 8: bad.H.assign(bad.freq.size(), 0); break;
            case 9: bad.magDb.clear(); break;
        }
        CHECK_FALSE(fohui::showDrift(bad, bad).valid);
        CHECK_FALSE(fohui::showDrift(ref, bad).valid);
    }
    fohui::ShowDriftAlert alert;
    for (int i = 0; i < 3; ++i) CHECK_FALSE(alert.update({true, INFINITY}));
}

TEST_CASE("W7 session reload releases monitoring and requires a new reference", "[app][show][edges]") {
    juce::ScopedJuceInitialiser_GUI gui;
    AppModel model; REQUIRE(model.traces.store("Synthetic reference", syntheticTF(), 0));
    auto panel = PanelRegistry::standard().create("ShowMonitoring", model);
    selectReference(*panel); click(*panel, "Monitor");
    REQUIRE(model.engine.monitoringSilent.load());
    model.notify(AppModel::Session);
    CHECK_FALSE(model.engine.monitoringSilent.load());
    click(*panel, "Monitor"); CHECK_FALSE(model.engine.monitoringSilent.load());
    selectReference(*panel); click(*panel, "Monitor"); CHECK(model.engine.monitoringSilent.load());
    click(*panel, "Stop monitoring"); CHECK_FALSE(model.engine.monitoringSilent.load());
}
