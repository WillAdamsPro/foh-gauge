#include <catch2/catch_test_macros.hpp>
#include "Widgets.h"
#include "AnalysisEngine.h"
#include <chrono>
#include <thread>

namespace {
void capture(AudioEngine& engine, const float* mic, const float* ref, int n) {
    const float* inputs[] { mic, ref };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, n, {});
}
template <typename Predicate>
bool waitFor(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}
}

TEST_CASE("Capture waits for the analysis restart acknowledgement", "[app][audio]") {
    AudioEngine engine;
    const float samples[] { 0.25f, -0.75f };
    engine.captureEpoch.store(1);
    capture(engine, samples, samples, 2);
    REQUIRE(engine.micRing.available() == 0);
    REQUIRE(engine.refRing.available() == 0);
    engine.analysisEpoch.store(1);
    capture(engine, samples, samples, 2);
    REQUIRE(engine.micRing.available() == 2);
    REQUIRE(engine.refRing.available() == 2);
}

TEST_CASE("Audio peaks include negative samples and reset between UI periods", "[app][audio]") {
    AudioEngine engine;
    const float loud[] { -0.95f, 0.1f }, quiet[] { -0.2f, 0.05f };
    capture(engine, loud, quiet, 2);
    capture(engine, quiet, quiet, 2);
    REQUIRE(engine.micPeak.exchange(0) == 0.95f);
    REQUIRE(engine.refPeak.exchange(0) == 0.2f);
    capture(engine, quiet, quiet, 2);
    REQUIRE(engine.micPeak.exchange(0) == 0.2f);
}

TEST_CASE("Analysis restart clears queued stereo audio and delay history", "[app][audio][delay]") {
    AudioEngine engine;
    AnalysisEngine analysis(engine);
    engine.captureEpoch.store(1);
    analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == 1; }));
    std::vector<float> ref(4096), mic(4096);
    ref[1000] = 1; mic[1120] = 1;
    capture(engine, mic.data(), ref.data(), 4096);
    REQUIRE(waitFor([&] { return engine.micRing.available() == 0 && engine.refRing.available() == 0; }));
    REQUIRE(analysis.send(foh::Command::findDelay(false, 1)));
    REQUIRE(waitFor([&] { return analysis.latest().delaySeq == 1; }));
    REQUIRE(analysis.latest().delay.ok());

    // Stop only the worker to deterministically leave old-rate samples queued.
    analysis.stop();
    capture(engine, mic.data(), ref.data(), 4096);
    engine.sampleRate.store(96000);
    engine.captureEpoch.fetch_add(1, std::memory_order_release);
    analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == 2; }));
    REQUIRE(engine.micRing.available() == 0);
    REQUIRE(engine.refRing.available() == 0);
    REQUIRE(analysis.send(foh::Command::findDelay(true, 2)));
    REQUIRE(waitFor([&] { return analysis.latest().delaySeq == 2; }));
    REQUIRE_FALSE(analysis.latest().delay.ok());
    analysis.stop();
}
