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

TEST_CASE("Capture snapshots acknowledge reset and carry the QA settle decision", "[app][qa]") {
    AudioEngine engine;
    AnalysisEngine analysis(engine);
    analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    REQUIRE_FALSE(analysis.latest().convergence.finished);

    // Synthetic identical broadband channels: coherent, stable transfer function.
    foh::PinkNoise noise(42);
    std::vector<float> signal(8192);
    noise.process(signal.data(), (int) signal.size());
    auto feed = [&] {
        capture(engine, signal.data(), signal.data(), (int) signal.size());
        REQUIRE(waitFor([&] { return engine.micRing.available() == 0 && engine.refRing.available() == 0; }));
    };
    for (int i = 0; i < 8; ++i) feed();
    REQUIRE(waitFor([&] { return analysis.latest().convergence.finished; }));
    const auto settled = analysis.latest();
    REQUIRE(settled.resetSeq == 1);
    REQUIRE(settled.convergence.settled);
    REQUIRE_FALSE(settled.convergence.unsettled);
    REQUIRE(settled.convergence.stableFrames == 3);
    REQUIRE(settled.convergence.deltaDb < 0.5);
    REQUIRE(foh::classifyCoherence(settled.tf).light == foh::TrafficLight::Green);

    // A new capture must not reuse that finished result. Silence cannot settle,
    // so the worker must publish an unsettled result after 10 s of audio.
    REQUIRE(analysis.send(foh::Command::resetAverages(2)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 2; }));
    REQUIRE_FALSE(analysis.latest().convergence.finished);
    std::fill(signal.begin(), signal.end(), 0.0f);
    for (int i = 0; i < 61; ++i) feed();
    REQUIRE(waitFor([&] { return analysis.latest().convergence.finished; }));
    const auto timedOut = analysis.latest();
    REQUIRE(timedOut.convergence.unsettled);
    REQUIRE(timedOut.convergence.timedOut);
    REQUIRE_FALSE(timedOut.convergence.settled);
    REQUIRE(timedOut.convergence.elapsedSeconds >= 10.0);
    REQUIRE(timedOut.convergence.coherentBins == 0);
    REQUIRE(foh::classifyCoherence(timedOut.tf).light == foh::TrafficLight::Red);
    analysis.stop();
}

TEST_CASE("Delay snapshots carry the core two-spike warning", "[app][qa][delay]") {
    AudioEngine engine;
    AnalysisEngine analysis(engine);
    analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    std::vector<float> ref(4096), mic(4096);
    ref[1000] = 1.0f;
    mic[1120] = 1.0f; mic[1240] = 1.0f; // equal arrivals, 2.5 ms apart
    capture(engine, mic.data(), ref.data(), (int) ref.size());
    REQUIRE(waitFor([&] { return engine.micRing.available() == 0 && engine.refRing.available() == 0; }));
    REQUIRE(analysis.send(foh::Command::findDelay(false, 1)));
    REQUIRE(waitFor([&] { return analysis.latest().delaySeq == 1; }));
    const auto result = analysis.latest().delay;
    REQUIRE(result.ok());
    REQUIRE(result.twoSimilarArrivals);
    REQUIRE(result.twoSimilarArrivals == foh::twoSpikeFlag(result.arrivals));
    analysis.stop();
}

TEST_CASE("Output chime is bounded, routed and stops at each sample rate", "[app][audio][chime]") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        AudioEngine engine;
        engine.sampleRate = fs;
        engine.outA = 1; engine.outB = -1;
        engine.captureEpoch = 1; // Playback must work even during an analysis restart.
        constexpr int block = 256;
        float unused[block], selected[block]; float* outputs[] {unused, selected};
        engine.playChime();
        float peak = 0;
        const int blocks = static_cast<int>(fs * 0.7 / block) + 1;
        for (int b = 0; b < blocks; ++b) {
            engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, block, {});
            for (int i = 0; i < block; ++i) {
                REQUIRE(unused[i] == 0.f);
                REQUIRE(std::isfinite(selected[i]));
                peak = std::max(peak, std::abs(selected[i]));
            }
        }
        const float expectedPeak = std::pow(10.f, -18.f / 20.f);
        REQUIRE(peak <= expectedPeak + 1e-6f);
        REQUIRE(peak > expectedPeak * 0.99f);
        for (float s : selected) REQUIRE(s == 0.f);
        engine.outB = 0; engine.playChime();
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, block, {});
        REQUIRE(selected[0] == 0.f); // Retrigger starts with the attack ramp.
        REQUIRE(selected[100] != 0.f);
        for (int i = 0; i < block; ++i) REQUIRE(unused[i] == selected[i]);
    }
}
