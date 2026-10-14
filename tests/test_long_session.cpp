#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "AnalysisEngine.h"
#include "Panels.h"
#include "ShowMonitoring.h"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>
#include <set>

// The thread remains stopped. Exercise exactly the worker's block, command and
// publication paths with a deterministic accelerated schedule and no sleeps.
struct AnalysisEngineSoakAccess {
    static void initialise(AnalysisEngine& a) { a.fftN = 1024; a.rebuild(48000); }
    static void block(AnalysisEngine& a, const float* data, int n) { a.processBlock(data, data, n); }
    static int commands(AnalysisEngine& a) { return a.serviceCommands(); }
    static void publish(AnalysisEngine& a) { a.publish(); }
    static int pending(const AnalysisEngine& a) { return a.commands.size(); }
    static auto history(const AnalysisEngine& a) {
        return std::make_pair(a.histRef.data(), a.histMic.data());
    }
    static bool bounded(const AnalysisEngine& a) {
        if (a.histRef.capacity() != 144000 || a.histMic.capacity() != 144000) return false;
        for (const auto& p : a.positions) if (p.tf.H.size() > 513 || p.tf.H.capacity() > 513) return false;
        return a.positionAverage.H.capacity() <= 513 && a.positionCount <= 3;
    }
};

TEST_CASE("Four-hour monitoring schedule recycles positions traces snapshots and commands", "[app][stress][soak]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AudioEngine engine; engine.state = AudioEngine::Ready; engine.monitoringSilent = true;
    AnalysisEngine analysis(engine); AnalysisEngineSoakAccess::initialise(analysis);
    std::array<float, 1024> programme;
    std::mt19937 rng(72); std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
    for (auto& x : programme) x = noise(rng);
    auto block = [&] { AnalysisEngineSoakAccess::block(analysis, programme.data(), int(programme.size())); };
    for (int i = 0; i < 32; ++i) block();
    REQUIRE(analysis.send(foh::Command::smoothing(0)));
    AnalysisEngineSoakAccess::commands(analysis);
    AnalysisEngineSoakAccess::publish(analysis);
    const auto reference = analysis.latest().tf;
    foh::TraceStore traces;
    for (int i = 0; i < foh::TraceStore::kMax; ++i) REQUIRE(traces.store("Synthetic", reference, 0));
    const auto history = AnalysisEngineSoakAccess::history(analysis);
    fohui::ShowDriftAlert alert;
    std::set<const foh::AnalysisSnapshot*> slots;
    double early = 0, late = 0;
    bool intact = true;
    // One representative block and one W7 update per synthetic second. The
    // separate Leq soak advances every 100 ms bucket for twelve full hours.
    constexpr int seconds = 4 * 3600;
    for (int second = 0; second < seconds; ++second) {
        const auto start = std::chrono::steady_clock::now();
        block();
        if (second % 60 == 0) {
            // Saturate, reject excess work, then recover in FIFO order.
            for (int i = 0; i < foh::CommandQueue::capacity(); ++i)
                intact &= analysis.send(foh::Command::micPosition(i % 3, 0));
            intact &= !analysis.send(foh::Command::clearMicPositions());
            intact &= AnalysisEngineSoakAccess::commands(analysis) == foh::CommandQueue::capacity();
        }
        AnalysisEngineSoakAccess::publish(analysis);
        const auto& snap = analysis.latest(); slots.insert(&snap);
        const auto drift = fohui::showDrift(snap.tf, reference);
        intact &= drift.valid && drift.rmsDb < 1e-8 && !alert.update(drift);
        intact &= snap.positionCount == 3 && snap.positionMask == 7;
        intact &= snap.multiPosition.H.size() == reference.H.size();
        intact &= std::abs(snap.multiPosition.magDb[20]) < 1e-8;
        intact &= !traces.store("Full", snap.tf, 0);
        intact &= traces.remove(0) && traces.store("Synthetic replacement", snap.tf, 0);
        intact &= traces.count() == foh::TraceStore::kMax;
        intact &= AnalysisEngineSoakAccess::bounded(analysis);
        intact &= AnalysisEngineSoakAccess::history(analysis) == history;
        intact &= AnalysisEngineSoakAccess::pending(analysis) == 0;
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (second >= 300 && second < 900) early += elapsed;
        if (second >= seconds - 600) late += elapsed;
    }
    CHECK(intact); CHECK(slots.size() <= 3);
    std::cout << "Soak representative block/update: early " << early / 600 * 1e6
              << " us, late " << late / 600 * 1e6 << " us\n";
    // A generous scheduling allowance detects history-dependent growth without
    // making an absolute performance promise on shared CI machines.
    CHECK(late < early * 4 + 0.1);
    REQUIRE(analysis.send(foh::Command::clearMicPositions()));
    AnalysisEngineSoakAccess::commands(analysis); AnalysisEngineSoakAccess::publish(analysis);
    CHECK(analysis.latest().multiPosition.H.empty());
    CHECK(analysis.latest().positionCount == 0);
}

TEST_CASE("Analysis command servicing yields under a continuously replenished queue", "[app][stress][concurrent]") {
    AudioEngine engine; engine.state = AudioEngine::Ready;
    AnalysisEngine analysis(engine); AnalysisEngineSoakAccess::initialise(analysis);
    std::array<float, 1024> input; input.fill(0.1f);
    AnalysisEngineSoakAccess::block(analysis, input.data(), int(input.size()));
    // Position capture is intentionally more work than enqueueing. With the old
    // unbounded drain, the producer can extend a single pass far beyond 64.
    const auto command = foh::Command::micPosition(0, 0);
    for (int i = 0; i < foh::CommandQueue::capacity(); ++i) REQUIRE(analysis.send(command));
    constexpr int produced = 4096;
    std::atomic<bool> started {false}, done {false};
    std::thread producer([&] {
        started = true;
        for (int i = 0; i < produced;) {
            if (analysis.send(command)) ++i;
            else std::this_thread::yield();
        }
        done = true;
    });
    while (!started.load()) std::this_thread::yield();
    bool bounded = true; int consumed = 0;
    do {
        const int count = AnalysisEngineSoakAccess::commands(analysis);
        bounded &= count <= foh::CommandQueue::capacity(); consumed += count;
        std::this_thread::yield();
    } while (!done.load() || AnalysisEngineSoakAccess::pending(analysis) != 0);
    producer.join();
    CHECK(bounded); CHECK(consumed == produced + foh::CommandQueue::capacity());
    CHECK(AnalysisEngineSoakAccess::pending(analysis) == 0);
}
