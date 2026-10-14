#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "Widgets.h"
#include "AnalysisEngine.h"
#include "SettleStatus.h"
#include "Panels.h"
#include "SetupPage.h"
#include <fohcore/session.h>
#include <chrono>
#include <string_view>
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

TEST_CASE("TF settle wording warns only on timeout or a completed unsettled capture", "[app][qa]") {
    foh::ConvergenceState convergence;
    auto text = [&](bool active = false, bool captureUnsettled = false) {
        return std::string_view(fohui::settleStatusText(convergence, active, captureUnsettled));
    };
    REQUIRE(text() == "averaging...");
    REQUIRE(text(true) == "averaging...");
    convergence.unsettled = true;
    convergence.elapsedSeconds = 9.99;
    REQUIRE(text(true) == "averaging...");
    REQUIRE(text(true, true) == "averaging..."); // An active capture has not ended.
    convergence.timedOut = true;
    convergence.elapsedSeconds = 10.0;
    REQUIRE(text() == "\xe2\x96\xb2 unsettled");
    REQUIRE(text(true) == "\xe2\x96\xb2 unsettled");
    convergence = {};
    REQUIRE(text(false, true) == "\xe2\x96\xb2 unsettled");
    convergence.settled = true;
    REQUIRE(text() == "settled");
    REQUIRE(text(false, true) == "\xe2\x96\xb2 unsettled");
    convergence = {}; // Starting a new capture resets its warning and settle state.
    REQUIRE(text(true) == "averaging...");
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

TEST_CASE("Invalid input samples never poison audio peak meters", "[app][audio][edges]") {
    AudioEngine engine;
    const float input[] {std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), -0.5f};
    capture(engine, input, input, 4);
    REQUIRE(engine.micPeak.exchange(0) == 0.5f);
    REQUIRE(engine.refPeak.exchange(0) == 0.5f);
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
    REQUIRE(analysis.latest().tf.sampleRate == 96000);
    REQUIRE(analysis.latest().tf.averages == 0);
    REQUIRE_FALSE(analysis.latest().convergence.finished);
    for (double level : analysis.latest().rtaDb) REQUIRE(std::isfinite(level));
    analysis.stop();
}

TEST_CASE("Capture snapshots acknowledge reset and carry the QA settle decision", "[app][qa]") {
    AudioEngine engine;
    AnalysisEngine analysis(engine);
    analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    REQUIRE_FALSE(analysis.latest().convergence.finished);
    REQUIRE(std::string_view(fohui::settleStatusText(analysis.latest().convergence, true, false)) == "averaging...");

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
    REQUIRE(std::string_view(fohui::settleStatusText(settled.convergence, false, false)) == "settled");
    REQUIRE(foh::classifyCoherence(settled.tf).light == foh::TrafficLight::Green);

    // A new capture must not reuse that finished result. Silence cannot settle,
    // so the worker must publish an unsettled result after 10 s of audio.
    REQUIRE(analysis.send(foh::Command::resetAverages(2)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 2; }));
    REQUIRE_FALSE(analysis.latest().convergence.finished);
    std::fill(signal.begin(), signal.end(), 0.0f);
    for (int i = 0; i < 58; ++i) feed();
    REQUIRE(waitFor([&] { return analysis.latest().convergence.elapsedSeconds >= 9.0; }));
    const auto averaging = analysis.latest().convergence;
    REQUIRE(averaging.elapsedSeconds < 10.0);
    REQUIRE_FALSE(averaging.finished);
    REQUIRE(std::string_view(fohui::settleStatusText(averaging, true, false)) == "averaging...");
    for (int i = 0; i < 3; ++i) feed();
    REQUIRE(waitFor([&] { return analysis.latest().convergence.finished; }));
    const auto timedOut = analysis.latest();
    REQUIRE(timedOut.convergence.unsettled);
    REQUIRE(timedOut.convergence.timedOut);
    REQUIRE_FALSE(timedOut.convergence.settled);
    REQUIRE(timedOut.convergence.elapsedSeconds >= 10.0);
    REQUIRE(timedOut.convergence.coherentBins == 0);
    REQUIRE(std::string_view(fohui::settleStatusText(timedOut.convergence, false, true)) == "\xe2\x96\xb2 unsettled");
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
    // Match the worker's zero-padded history window. The UI snapshot must
    // retain the core confidence, including exactly one ambiguity penalty.
    const int window = foh::recommendedDelayWindowSamples(48000, false);
    std::vector<float> paddedRef((size_t) window), paddedMic((size_t) window);
    std::copy(ref.begin(), ref.end(), paddedRef.end() - (long) ref.size());
    std::copy(mic.begin(), mic.end(), paddedMic.end() - (long) mic.size());
    const auto expected = foh::findDelay(paddedRef, paddedMic, 48000);
    REQUIRE(result.confidence == Catch::Approx(expected.confidence));
    REQUIRE(result.confidence > 0.0);
    REQUIRE(result.confidence <= 0.75);

    // Start a fresh history so the clean capture cannot reuse either spike.
    engine.captureEpoch.fetch_add(1, std::memory_order_release);
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    mic[1240] = 0.0f;
    capture(engine, mic.data(), ref.data(), (int) ref.size());
    REQUIRE(waitFor([&] { return engine.micRing.available() == 0 && engine.refRing.available() == 0; }));
    REQUIRE(analysis.send(foh::Command::findDelay(false, 2)));
    REQUIRE(waitFor([&] { return analysis.latest().delaySeq == 2; }));
    const auto single = analysis.latest().delay;
    REQUIRE(single.ok());
    REQUIRE_FALSE(single.twoSimilarArrivals);
    REQUIRE(single.confidence == Catch::Approx(1.0));
    analysis.stop();
}

TEST_CASE("Output chime is bounded, routed and stops at each sample rate", "[app][audio][chime]") {
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        AudioEngine engine;
        engine.sampleRate = fs;
        engine.outA = 1; engine.outB = 0; // Both generator routes are enabled.
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
        engine.gen = AudioEngine::Pink;
        engine.playChime();
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, block, {});
        REQUIRE(selected[0] == 0.f); // Retrigger starts with the attack ramp.
        REQUIRE(selected[100] != 0.f);
        for (int i = 0; i < block; ++i) REQUIRE(unused[i] == 0.f);

        // The wizard can test the loopback output independently of the PA route,
        // even while the measurement generator is enabled on both outputs.
        engine.playChime(0);
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, block, {});
        REQUIRE(unused[0] == 0.f);
        REQUIRE(unused[100] != 0.f);
        for (float s : selected) REQUIRE(s == 0.f);
    }
}

TEST_CASE("SPL level snapshots measure the selected input and reset on routing changes", "[app][audio][splcal]") {
    AudioEngine engine; AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    std::vector<float> mic(4800), ref(4800);
    for (size_t i = 0; i < mic.size(); ++i) {
        mic[i] = float(0.1 * std::sin(2 * juce::MathConstants<double>::pi * 1000 * double(i) / 48000));
        ref[i] = mic[i] * 0.5f;
    }
    capture(engine, mic.data(), ref.data(), int(mic.size()));
    REQUIRE(waitFor([&] { return analysis.latest().micLevelValid; }));
    auto first = analysis.latest();
    REQUIRE(first.micChannel == 0);
    REQUIRE(first.micLevelDbfs == Catch::Approx(-20).margin(0.01));
    foh::SplCal cal; REQUIRE(cal.calibrate(first.micLevelDbfs, 94));
    REQUIRE(cal.level(first.micLevelDbfs) == Catch::Approx(94).margin(0.1));
    REQUIRE(first.leq.seconds == Catch::Approx(0.1));
    for (int w : {0, 1, 2}) {
        REQUIRE(cal.level(first.leq.level[w]) == Catch::Approx(94).margin(0.1));
        REQUIRE(cal.level(first.leq.session[w]) == Catch::Approx(94).margin(0.1));
    }
    analysis.paused = true;
    capture(engine, mic.data(), ref.data(), int(mic.size()));
    REQUIRE(waitFor([&] { return engine.micRing.available() == 0; }));
    REQUIRE(waitFor([&] { return !analysis.latest().micLevelValid; }));
    REQUIRE(analysis.latest().leq.seconds == Catch::Approx(0.1));
    ++analysis.leqRevision;
    REQUIRE(waitFor([&] { return analysis.latest().leqRevision == 1; }));
    REQUIRE(analysis.latest().leq.seconds == 0);
    analysis.paused = false;
    engine.micCh = 1;
    capture(engine, mic.data(), ref.data(), int(mic.size())); // Discard while routing restart is acknowledged.
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    capture(engine, mic.data(), ref.data(), int(mic.size()));
    REQUIRE(waitFor([&] { const auto& s = analysis.latest(); return s.micLevelValid && s.micChannel == 1; }));
    const auto second = analysis.latest();
    REQUIRE(second.inputEpoch != first.inputEpoch);
    REQUIRE(second.leq.seconds == Catch::Approx(0.1));
    REQUIRE(second.leq.session[2] == Catch::Approx(-26.0206).margin(0.01));
    REQUIRE(second.micLevelDbfs == Catch::Approx(-26.0206).margin(0.01));
    REQUIRE(waitFor([&] { return !analysis.latest().micLevelValid; })); // Never calibrate a stale snapshot.
    analysis.stop();
}

TEST_CASE("Connection tests isolate output and report synthetic input routing", "[app][audio][routing]") {
    AudioEngine engine;
    engine.gen = AudioEngine::Pink;
    constexpr int block = 256;
    float a[block] {}, b[block] {}, signal[block];
    std::fill(std::begin(signal), std::end(signal), 0.1f);
    float silence[block] {};
    const float* inputs[] {silence, signal}; float* outputs[] {a, b};
    engine.testConnection(1, 1);
    for (int i = 0; i < 188; ++i) {
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, block, {});
        for (auto sample : a) CHECK(sample == 0.f);
    }
    CHECK(engine.connectionResult() == foh::ConnectionResult::pass);
    engine.testConnection(0, 0);
    // Separate synthetic input buffers from output buffers.
    const float* swapped[] {silence, signal};
    for (int i = 0; i < 188; ++i)
        engine.audioDeviceIOCallbackWithContext(swapped, 2, outputs, 2, block, {});
    CHECK(engine.connectionResult() == foh::ConnectionResult::swapInputs);
    engine.testConnection(1, 1); engine.cancelConnectionTest();
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, block, {});
    CHECK(engine.connectionResult() == foh::ConnectionResult::pending);
    engine.testConnection(-1, 1);
    CHECK(engine.connectionResult() == foh::ConnectionResult::invalid);
}

namespace {
// No hardware or microphone access: exercise JUCE's quiesced lifecycle contract.
class SyntheticDevice : public juce::AudioIODevice {
public:
    SyntheticDevice() : AudioIODevice("Synthetic", "Synthetic") {}
    double rate = 48000; int block = 256;
    juce::StringArray getOutputChannelNames() override { return {"1", "2"}; }
    juce::StringArray getInputChannelNames() override { return {"1", "2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {rate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {block}; }
    int getDefaultBufferSize() override { return block; }
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger(3); }
    juce::BigInteger getActiveInputChannels() const override { return juce::BigInteger(3); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};
}

namespace {
class SyntheticDeviceType : public juce::AudioIODeviceType {
public:
    SyntheticDeviceType() : AudioIODeviceType("Synthetic") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Synthetic"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override {
        return new SyntheticDevice;
    }
};

juce::ComboBox* routingChoice(SetupPage& setup, const juce::String& prefix) {
    for (int i = 0; i < setup.getNumChildComponents(); ++i)
        if (auto* box = dynamic_cast<juce::ComboBox*>(setup.getChildComponent(i)))
            if (box->getItemText(0) == prefix + ": None") return box;
    return nullptr;
}
}

TEST_CASE("Session panel restore updates engine routing and every Setup page", "[app][session][routing]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model; // Existing isolated test model: no preferences or analysis worker.
    model.dm.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    model.dm.setCurrentAudioDeviceType("Synthetic", true);
    auto deviceSetup = model.dm.getAudioDeviceSetup();
    deviceSetup.inputDeviceName = deviceSetup.outputDeviceName = "Synthetic";
    deviceSetup.useDefaultInputChannels = deviceSetup.useDefaultOutputChannels = false;
    deviceSetup.inputChannels = deviceSetup.outputChannels = juce::BigInteger(3);
    REQUIRE(model.dm.setAudioDeviceSetup(deviceSetup, true).isEmpty());
    REQUIRE(model.dm.getCurrentAudioDevice() != nullptr);

    auto panel = PanelRegistry::standard().create("InputsAndDevices", model);
    REQUIRE(panel != nullptr);
    auto* embedded = dynamic_cast<SetupPage*>(panel->getChildComponent(0));
    REQUIRE(embedded != nullptr);
    SetupPage standalone(model);
    const char* prefixes[] {"Mic", "Reference", "Generator output", "Loopback output"};
    auto checkSelections = [&](const int (&channels)[4]) {
        for (auto* setup : {embedded, &standalone})
            for (int i = 0; i < 4; ++i) {
                auto* box = routingChoice(*setup, prefixes[i]);
                REQUIRE(box != nullptr);
                CHECK(box->getSelectedId() == channels[i] + 2);
            }
    };
    const int defaults[] {0, 1, 0, 1};
    checkSelections(defaults);

    // Round-trip a synthetic bundle, then restore its InputsAndDevices state as
    // WorkspaceView does. No other Settings notifications can mask a missing one.
    struct Bundle {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("foh-synthetic-routing-" + juce::Uuid().toString().toStdString() + ".fohsession");
        ~Bundle() { std::error_code error; std::filesystem::remove_all(path, error); }
    } bundle;
    foh::Session session;
    session.deviceType = session.inputDevice = session.outputDevice = "Synthetic";
    session.inputChannels = session.outputChannels = "11";
    session.mic = 1; session.reference = 0; session.outputA = 1; session.outputB = 0;
    auto routing = fohws::Json::object();
    routing.set("mic", session.mic); routing.set("ref", session.reference);
    routing.set("outA", session.outputA); routing.set("outB", session.outputB);
    auto states = fohws::Json::object(); states.set("InputsAndDevices", routing);
    session.workspace.set("panelState", states);
    std::string error;
    REQUIRE(foh::saveSession(bundle.path, session, error));
    foh::Session loaded;
    REQUIRE(foh::loadSession(bundle.path, loaded, error));
    panel->loadState(loaded.workspace["panelState"]["InputsAndDevices"]);
    CHECK(model.engine.micCh.load() == loaded.mic);
    CHECK(model.engine.refCh.load() == loaded.reference);
    CHECK(model.engine.outA.load() == loaded.outputA);
    CHECK(model.engine.outB.load() == loaded.outputB);
    const int restored[] {1, 0, 1, 0};
    checkSelections(restored);
    CHECK(panel->saveState() == routing);

    // Explicit None selections must survive restore too.
    for (const auto* key : {"mic", "ref", "outA", "outB"}) routing.set(key, -1);
    panel->loadState(routing);
    CHECK(model.engine.micCh.load() == -1); CHECK(model.engine.refCh.load() == -1);
    CHECK(model.engine.outA.load() == -1); CHECK(model.engine.outB.load() == -1);
    const int none[] {-1, -1, -1, -1};
    checkSelections(none);
    CHECK(panel->saveState() == routing);
}

TEST_CASE("Device stop and rate/buffer restarts cancel playback and clear analysis", "[app][audio]") {
    AudioEngine engine; AnalysisEngine analysis(engine); SyntheticDevice device;
    engine.audioDeviceAboutToStart(&device); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    float signal[1024]; std::fill(std::begin(signal), std::end(signal), 0.2f);
    const float* inputs[] {signal, signal};
    float a[1024], b[1024]; float* outputs[] {a, b};
    engine.gen = AudioEngine::Sweep; engine.testConnection(1, 1);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, {});
    engine.audioDeviceStopped();
    CHECK(engine.gen.load() == AudioEngine::Off);
    CHECK(engine.connectionResult() == foh::ConnectionResult::invalid);
    CHECK(std::string_view(engine.statusText()).find("stopped") != std::string_view::npos);
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, {});
    for (int i = 0; i < 256; ++i) { CHECK(a[i] == 0); CHECK(b[i] == 0); }
    REQUIRE(waitFor([&] { const auto& s = analysis.latest(); return s.inputEpoch == engine.captureEpoch.load() && !s.micLevelValid; }));
    for (double rate : {44100.0, 96000.0, 48000.0}) {
        device.rate = rate; device.block = rate == 48000 ? 1024 : 64;
        engine.audioDeviceAboutToStart(&device);
        REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
        CHECK(engine.micRing.available() == 0); CHECK(engine.refRing.available() == 0);
        for (int frames = 0; frames < 1024; frames += device.block) {
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, device.block, {});
            for (int i = 0; i < device.block; ++i) { CHECK(a[i] == 0); CHECK(b[i] == 0); }
        }
        REQUIRE(waitFor([&] { return engine.micRing.available() == 0; }));
        engine.audioDeviceStopped();
    }
    device.rate = 0;
    engine.audioDeviceAboutToStart(&device);
    CHECK(engine.state.load() == AudioEngine::Stopped);
    analysis.stop();
}

TEST_CASE("Reference rerouting and unavailable channels invalidate capture", "[app][audio]") {
    AudioEngine engine;
    float signal[64] {}; const float* inputs[] {signal, signal};
    capture(engine, signal, signal, 64);
    engine.refCh = 0;
    capture(engine, signal, signal, 64);
    CHECK(engine.captureEpoch.load() == 1);
    CHECK(engine.micRing.available() == 64); // no mixed-routing samples queued
    engine.analysisEpoch = engine.captureEpoch.load();
    engine.micCh = 3;
    engine.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, 64, {});
    CHECK(engine.state.load() == AudioEngine::InvalidInputs);
    CHECK(engine.captureEpoch.load() == 2);
    engine.analysisEpoch = 2;
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, nullptr, 0, 64, {});
    CHECK(engine.micRing.available() == 64); // no fabricated silence measurement
    engine.micCh = 0; engine.refCh = 1;
    capture(engine, signal, signal, 64);
    engine.analysisEpoch = engine.captureEpoch.load();
    engine.gen = AudioEngine::Pink; engine.outA = 10;
    capture(engine, signal, signal, 64);
    CHECK(engine.state.load() == AudioEngine::InvalidOutputs);
}

TEST_CASE("Stereo overflow drops both channels and requests a history reset", "[app][audio]") {
    AudioEngine engine;
    std::vector<float> full(size_t(engine.refRing.capacity()), 0.25f);
    REQUIRE(engine.refRing.push(full.data(), int(full.size())));
    // Represents the consumer having freed mic space before freeing ref space.
    float signal[64] {};
    capture(engine, signal, signal, 64);
    CHECK(engine.micRing.available() == 0);
    CHECK(engine.refRing.available() == engine.refRing.capacity());
    CHECK(engine.captureEpoch.load() == 1);
    CHECK(engine.state.load() == AudioEngine::Overflow);
    AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == 1; }));
    CHECK(engine.micRing.available() == 0); CHECK(engine.refRing.available() == 0);
    capture(engine, signal, signal, 64);
    CHECK(engine.state.load() == AudioEngine::Ready);
    analysis.stop();
}

// Count C++ heap allocations only inside the audited callback, on this thread.
#include <cstdlib>
#include <new>
namespace {
thread_local bool auditAllocations = false;
thread_local int callbackAllocations = 0;
}
void* operator new(std::size_t n) {
    if (auditAllocations) ++callbackAllocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

TEST_CASE("Audio callback is allocation free across generators, tests and variable blocks", "[app][audio][rt]") {
    AudioEngine engine; SyntheticDevice device;
    engine.audioDeviceAboutToStart(&device); engine.analysisEpoch = engine.captureEpoch.load();
    float input[8192] {}, output[8192];
    const float* inputs[] {input, input}; float* outputs[] {output};
    engine.outB = -1;
    for (int mode : {AudioEngine::Off, AudioEngine::Pink, AudioEngine::Sweep}) {
        engine.gen = mode; engine.genGainDb = -30.0f + float(mode);
        for (int n : {1, 16, 256, 4096, 8192}) {
            // First exercise the generator, then exercise chime/detection on the same block.
            engine.cancelConnectionTest();
            callbackAllocations = 0; auditAllocations = true;
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, n, {});
            auditAllocations = false;
            REQUIRE(callbackAllocations == 0);
            for (int i = 0; i < n; ++i) REQUIRE(std::isfinite(output[i]));
            engine.testConnection(0, 1);
            callbackAllocations = 0; auditAllocations = true;
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, n, {});
            auditAllocations = false;
            REQUIRE(callbackAllocations == 0);
            for (int i = 0; i < n; ++i) REQUIRE(std::isfinite(output[i]));
        }
    }
    engine.refCh = 100;
    callbackAllocations = 0; auditAllocations = true;
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 1, 256, {});
    engine.audioDeviceStopped();
    auditAllocations = false;
    CHECK(callbackAllocations == 0);
}

TEST_CASE("Peak exchange contention preserves the final callback peak", "[app][audio][rt]") {
    AudioEngine engine;
    const float samples[] {-0.9f, 0.1f};
    std::atomic<bool> done{false};
    std::thread ui([&] { while (!done.load()) { engine.micPeak.exchange(0); engine.refPeak.exchange(0); } });
    for (int i = 0; i < 10000; ++i) capture(engine, samples, samples, 2);
    done = true; ui.join();
    capture(engine, samples, samples, 2);
    CHECK(engine.micPeak.exchange(0) == 0.9f);
    CHECK(engine.refPeak.exchange(0) == 0.9f);
}

TEST_CASE("Device errors from another thread silence playback until restart", "[app][audio]") {
    AudioEngine engine; SyntheticDevice device;
    engine.gen = AudioEngine::Pink; engine.playChime(0);
    const auto epoch = engine.captureEpoch.load();
    std::thread error([&] { engine.audioDeviceError("Synthetic error"); }); error.join();
    CHECK(engine.captureEpoch.load() == epoch); // an arbitrary thread never races the ring producer
    CHECK(std::string_view(engine.statusText()).find("error") != std::string_view::npos);
    float output[256]; float* outputs[] {output};
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 1, 256, {});
    for (float s : output) CHECK(s == 0);
    engine.audioDeviceAboutToStart(&device);
    CHECK_FALSE(engine.deviceFailed.load());
    CHECK(engine.gen.load() == AudioEngine::Off);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 1, 256, {});
    for (float s : output) CHECK(s == 0); // no stale chime resumes
}

namespace {
// Owns the producer and all lifecycle callbacks, just as JUCE serializes device
// restarts with I/O. The test thread acts only as the message-thread consumer.
class ConcurrentCapture {
public:
    explicit ConcurrentCapture(AudioEngine& e) : engine(e), worker([this] { run(); }) {}
    ~ConcurrentCapture() { done.store(true); worker.join(); }
    std::atomic<double> requestedRate{48000}, runningRate{0};
    std::atomic<uint64_t> blocks{0};
private:
    void run() {
        SyntheticDevice device;
        float signal[1024];
        foh::PinkNoise noise(43);
        noise.process(signal, 1024);
        engine.audioDeviceAboutToStart(&device);
        runningRate.store(device.rate);
        while (!done.load()) {
            const auto rate = requestedRate.load();
            if (rate != device.rate) {
                engine.audioDeviceStopped();
                device.rate = rate;
                engine.audioDeviceAboutToStart(&device);
                runningRate.store(rate);
            }
            capture(engine, signal, signal, 1024);
            blocks.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        engine.audioDeviceStopped();
    }
    AudioEngine& engine;
    std::atomic<bool> done{false};
    std::thread worker;
};
}

TEST_CASE("Sample-rate changes overlap live analysis and snapshot reads", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    for (double rate : {96000.0, 44100.0, 48000.0, 96000.0, 48000.0}) {
        producer.requestedRate.store(rate);
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return producer.runningRate.load() == rate && s.tf.sampleRate == rate
                && s.inputEpoch == engine.captureEpoch.load() && s.tf.averages > 0;
        }));
        const auto snapshot = analysis.latest();
        REQUIRE(snapshot.tf.sampleRate == rate);
        REQUIRE(snapshot.rtaDb.size() == snapshot.rtaBands.size());
        for (auto level : snapshot.rtaDb) REQUIRE(std::isfinite(level));
    }
}

TEST_CASE("Capture reset and pause commands overlap the analysis job", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    for (uint64_t id = 1; id <= 8; ++id) {
        // Same handoffs used by beginCapture and the toolbar Stop/Start controls.
        analysis.paused.store(true);
        REQUIRE(analysis.send(foh::Command::resetAverages(id)));
        REQUIRE(waitFor([&] { return analysis.latest().resetSeq == id; }));
        const auto stopped = analysis.latest();
        REQUIRE(stopped.tf.averages == 0);
        REQUIRE_FALSE(stopped.micLevelValid);
        const auto blocks = producer.blocks.load();
        REQUIRE(waitFor([&] {
            return producer.blocks.load() >= blocks + 8 && analysis.latest().seq > stopped.seq;
        }));
        REQUIRE(analysis.latest().tf.averages == 0);
        analysis.paused.store(false);
        REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
        REQUIRE(analysis.latest().resetSeq == id);
    }
}

TEST_CASE("Settings and calibration changes overlap a synthetic measurement", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    const auto calibration = std::make_shared<const foh::MicCal>(
        foh::parseMicCal("20 0\n1000 0\n20000 0\n"));
    REQUIRE(calibration->ok());
    for (uint64_t id = 1; id <= 8; ++id) {
        const int fraction = id % 2 ? 6 : 12;
        engine.genGainDb.store(-30.f + float(id));
        analysis.setCal(id % 2 ? calibration : nullptr, "Synthetic");
        REQUIRE(analysis.send(foh::Command::smoothing(fraction)));
        REQUIRE(analysis.send(foh::Command::rtaFraction(fraction)));
        REQUIRE(analysis.send(foh::Command::averages(4)));
        REQUIRE(analysis.send(foh::Command::blankThreshold(0.7)));
        REQUIRE(analysis.send(foh::Command::referenceDelay(double(id))));
        REQUIRE(analysis.send(foh::Command::resetAverages(id)));
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return s.resetSeq == id && s.smoothingFraction == fraction
                && s.rtaFraction == fraction && s.calApplied == bool(id % 2)
                && s.tf.averages > 0;
        }));
        const auto snapshot = analysis.latest();
        REQUIRE(snapshot.refDelaySamples == double(id));
        REQUIRE(snapshot.rtaDb.size() == snapshot.rtaBands.size());
        REQUIRE(snapshot.inputEpoch == engine.captureEpoch.load());
    }
}
