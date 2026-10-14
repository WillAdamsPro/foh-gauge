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

// Sole consumer in synchronous bridge tests; never start the analysis worker.
class AnalysisEngineTestAccess {
public:
    explicit AnalysisEngineTestAccess(AnalysisEngine& engine)
        : analysis(engine), mic(size_t(engine.fftN)), ref(size_t(engine.fftN)),
          silence(size_t(engine.fftN), 0.f) {}
    void step() {
        foh::ScopedFlushDenormals ftz;
        analysis.processPendingAudio(mic, ref, silence, epoch);
        analysis.publish();
    }
private:
    AnalysisEngine& analysis;
    std::vector<float> mic, ref, silence;
    uint64_t epoch = 0;
};

namespace {
void capture(AudioEngine& engine, const float* mic, const float* ref, int n) {
    const float* inputs[] { mic, ref };
    engine.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, n, {});
}
template <typename Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
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
    AudioEngine engine;
    AnalysisEngine analysis(engine);
    AnalysisEngineTestAccess worker(analysis);
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    worker.step();
    REQUIRE(analysis.latest().resetSeq == 1);
    REQUIRE_FALSE(analysis.latest().micLevelValid);

    // Five fixed 20 ms blocks, with an integral number of 1 kHz cycles per block.
    // Publish only after all 4800 samples have been consumed: no timer or thread.
    constexpr int blockSamples = 960, blocks = 5;
    std::vector<float> mic(blockSamples), ref(blockSamples);
    for (size_t i = 0; i < mic.size(); ++i) {
        mic[i] = float(0.1 * std::sin(2 * juce::MathConstants<double>::pi * 1000 * double(i) / 48000));
        ref[i] = mic[i] * 0.5f;
    }
    auto feed = [&] {
        for (int i = 0; i < blocks; ++i)
            capture(engine, mic.data(), ref.data(), blockSamples);
        REQUIRE(engine.micRing.available() == blockSamples * blocks);
        REQUIRE(engine.refRing.available() == blockSamples * blocks);
        worker.step();
        REQUIRE(engine.micRing.available() == 0);
        REQUIRE(engine.refRing.available() == 0);
    };
    feed();
    const auto first = analysis.latest();
    REQUIRE(first.micLevelValid);
    REQUIRE(first.micChannel == 0);
    REQUIRE(first.inputEpoch == engine.captureEpoch.load());
    REQUIRE(first.micLevelDbfs == Catch::Approx(-20).margin(0.01));
    foh::SplCal cal; REQUIRE(cal.calibrate(first.micLevelDbfs, 94));
    REQUIRE(cal.level(first.micLevelDbfs) == Catch::Approx(94).margin(0.1));
    REQUIRE(first.leq.seconds == Catch::Approx(0.1).margin(1e-9));
    for (int w : {0, 1, 2}) {
        REQUIRE(cal.level(first.leq.level[w]) == Catch::Approx(94).margin(0.1));
        REQUIRE(cal.level(first.leq.session[w]) == Catch::Approx(94).margin(0.1));
    }
    analysis.paused = true;
    feed();
    REQUIRE_FALSE(analysis.latest().micLevelValid);
    REQUIRE(analysis.latest().leq.seconds == Catch::Approx(0.1).margin(1e-9));
    ++analysis.leqRevision;
    worker.step();
    REQUIRE(analysis.latest().leqRevision == 1);
    REQUIRE(analysis.latest().leq.seconds == 0);
    analysis.paused = false;
    feed(); // Routing must clear a populated meter, independently of leqRevision.
    REQUIRE(analysis.latest().micLevelValid);
    REQUIRE(analysis.latest().leq.seconds == Catch::Approx(0.1).margin(1e-9));

    // Leave old-route audio queued to verify the reset drains it before ack.
    capture(engine, mic.data(), ref.data(), blockSamples);
    REQUIRE(engine.micRing.available() == blockSamples);
    engine.micCh = 1;
    capture(engine, mic.data(), ref.data(), blockSamples);
    REQUIRE(engine.captureEpoch.load() == first.inputEpoch + 1);
    REQUIRE(engine.analysisEpoch.load() == first.inputEpoch);
    REQUIRE(engine.micRing.available() == blockSamples); // New-route block was discarded.
    REQUIRE(engine.refRing.available() == blockSamples);
    worker.step();
    REQUIRE(engine.analysisEpoch.load() == engine.captureEpoch.load());
    REQUIRE(engine.micRing.available() == 0);
    REQUIRE(engine.refRing.available() == 0);
    const auto reset = analysis.latest();
    REQUIRE(reset.inputEpoch == first.inputEpoch + 1);
    REQUIRE(reset.micChannel == 1);
    REQUIRE_FALSE(reset.micLevelValid);
    REQUIRE(reset.micLevelDbfs == -120);
    REQUIRE(reset.leq.seconds == 0);
    for (int w : {0, 1, 2}) REQUIRE(reset.leq.session[w] == -120);

    feed();
    const auto second = analysis.latest();
    REQUIRE(second.micLevelValid);
    REQUIRE(second.micChannel == 1);
    REQUIRE(second.inputEpoch == first.inputEpoch + 1);
    REQUIRE(second.leq.seconds == Catch::Approx(0.1).margin(1e-9));
    REQUIRE(second.leq.session[2] == Catch::Approx(-26.0206).margin(0.01));
    REQUIRE(second.micLevelDbfs == Catch::Approx(-26.0206).margin(0.01));
    worker.step(); // An explicit publication with no new audio invalidates the level.
    REQUIRE_FALSE(analysis.latest().micLevelValid); // Never calibrate a stale snapshot.
    REQUIRE(analysis.latest().micLevelDbfs == -120);
    REQUIRE(analysis.latest().leq.seconds == Catch::Approx(0.1).margin(1e-9));
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
    double rate = 48000; int block = 256; int activeOutputs = 3;
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
    juce::BigInteger getActiveOutputChannels() const override { return juce::BigInteger(activeOutputs); }
    juce::BigInteger getActiveInputChannels() const override { return juce::BigInteger(3); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};
}

TEST_CASE("Interrupted connection tests fail and fresh tests recover", "[app][audio][routing][edges]") {
    AudioEngine engine; SyntheticDevice device;
    constexpr int block = 256;
    float signal[block], silence[block] {}, a[block] {}, b[block] {};
    std::fill(std::begin(signal), std::end(signal), 0.1f);
    const float* inputs[] {signal, silence}; float* outputs[] {a, b};
    auto feed = [&] { engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, block, {}); };
    for (int event = 0; event < 5; ++event) {
        engine.audioDeviceAboutToStart(&device);
        engine.testConnection(0, 0); feed();
        REQUIRE(engine.connectionResult() == foh::ConnectionResult::pending);
        if (event == 0) engine.audioDeviceStopped();
        if (event == 1) engine.audioDeviceError("Synthetic unplug");
        if (event == 2) { device.rate = 96000; engine.audioDeviceAboutToStart(&device); }
        if (event == 3) engine.audioDeviceAboutToStart(&device); // replacement with same shape/rate
        if (event == 4) {
            engine.audioDeviceIOCallbackWithContext(inputs, 1, outputs, 2, block, {});
            for (int i = 0; i < 400; ++i) feed();
        }
        CHECK(engine.connectionResult() == foh::ConnectionResult::invalid);
        engine.audioDeviceAboutToStart(&device);
        for (int i = 0; i < 400; ++i) feed();
        CHECK(engine.connectionResult() == foh::ConnectionResult::invalid); // never resumes the old test
        engine.testConnection(0, 0);
        for (int i = 0; i < 400; ++i) feed();
        CHECK(engine.connectionResult() == foh::ConnectionResult::pass);
    }
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

TEST_CASE("Device rebuild rejects queued capture commands but preserves settings", "[app][audio][delay]") {
    AudioEngine engine; AnalysisEngine analysis(engine); SyntheticDevice device;
    engine.audioDeviceAboutToStart(&device); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    analysis.stop();
    // Deterministically enqueue work using the old device's history and sample units.
    REQUIRE(analysis.send(foh::Command::referenceDelay(240)));
    REQUIRE(analysis.send(foh::Command::findDelay(true, 75)));
    REQUIRE(analysis.send(foh::Command::resetAverages(75)));
    REQUIRE(analysis.send(foh::Command::micPosition(0, 5)));
    foh::Command sweep; sweep.type = foh::CommandType::MeasureSweep;
    REQUIRE(analysis.send(sweep));
    REQUIRE(analysis.send(foh::Command::averages(4)));
    REQUIRE(analysis.send(foh::Command::rtaFraction(6)));
    const auto oldEpoch = engine.captureEpoch.load();
    engine.audioDeviceStopped(); device.rate = 96000; device.block = 64;
    engine.audioDeviceAboutToStart(&device);
    // A UI lock may arrive after the rebuild while carrying an old snapshot's samples.
    REQUIRE(analysis.send(foh::Command::referenceDelay(480), oldEpoch));
    analysis.start();
    REQUIRE(waitFor([&] { return analysis.latest().inputEpoch == engine.captureEpoch.load(); }));
    const auto snapshot = analysis.latest();
    CHECK(snapshot.tf.sampleRate == 96000);
    CHECK(snapshot.refDelaySamples == 0);
    CHECK(snapshot.delaySeq == 0);
    CHECK_FALSE(snapshot.delayWasSub);
    CHECK(snapshot.resetSeq != 75);
    CHECK(snapshot.positionCount == 0);
    CHECK_FALSE(snapshot.sweepCapturing);
    CHECK(engine.gen.load() == AudioEngine::Off);
    CHECK(snapshot.rtaFraction == 6);
    // Commands submitted before the worker acknowledges the new device must survive.
    analysis.stop();
    engine.audioDeviceStopped(); device.block = 1024;
    engine.audioDeviceAboutToStart(&device);
    REQUIRE(analysis.send(foh::Command::referenceDelay(96)));
    REQUIRE(analysis.send(foh::Command::resetAverages(76)));
    analysis.start();
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 76; }));
    CHECK(analysis.latest().refDelayMs == Catch::Approx(1));
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
    explicit ConcurrentCapture(AudioEngine& e, bool constantEnergy = false)
        : engine(e), fixedEnergy(constantEnergy), worker([this] { run(); }) {}
    ~ConcurrentCapture() { done.store(true); worker.join(); }
    std::atomic<double> requestedRate{48000}, runningRate{0};
    std::atomic<int> requestedBlock{1024}, runningBlock{0};
    std::atomic<bool> requestedRunning{true}, running{false};
    std::atomic<uint64_t> blocks{0}, allocations{0}, audibleBlocks{0}, silentBlocks{0};
    std::atomic<bool> finiteOutput{true};
private:
    void run() {
        SyntheticDevice device;
        float signal[1024], left[1024], right[1024];
        const float* inputs[] {signal, signal};
        float* outputs[] {left, right};
        foh::PinkNoise noise(43);
        noise.process(signal, 1024);
        if (fixedEnergy) {
            // Balanced synthetic broadband pairs: every supported block has
            // zero DC and exactly -20 dBFS energy, independent of buffer length.
            for (int i = 0; i < 1024; i += 2) {
                signal[i] = signal[i] >= 0 ? 0.07071068f : -0.07071068f;
                signal[i + 1] = -signal[i];
            }
        }
        device.block = requestedBlock.load();
        engine.audioDeviceAboutToStart(&device);
        runningBlock.store(device.block);
        runningRate.store(device.rate);
        running.store(true);
        while (!done.load()) {
            if (!requestedRunning.load()) {
                if (running.exchange(false)) engine.audioDeviceStopped();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!running.load()) {
                engine.audioDeviceAboutToStart(&device);
                running.store(true);
            }
            const auto rate = requestedRate.load();
            const auto block = requestedBlock.load();
            if (rate != device.rate || block != device.block) {
                engine.audioDeviceStopped();
                device.rate = rate; device.block = block;
                engine.audioDeviceAboutToStart(&device);
                runningBlock.store(block);
                runningRate.store(rate);
            }
            // Exercise handoffs, not worker throughput: an instrumented Debug
            // FFT can be slower than this synthetic producer. Bound the backlog
            // so repeated overflow resets cannot starve snapshot publication.
            // This pacing belongs only to the test producer, never the callback.
            if (engine.micRing.available() < 4096 && engine.refRing.available() < 4096) {
                callbackAllocations = 0; auditAllocations = true;
                engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, device.block, {});
                auditAllocations = false;
                allocations.fetch_add(uint64_t(callbackAllocations));
                bool audible = false;
                for (int i = 0; i < device.block; ++i) {
                    if (!std::isfinite(left[i]) || !std::isfinite(right[i])) finiteOutput.store(false);
                    audible |= left[i] != 0 || right[i] != 0;
                }
                if (audible) audibleBlocks.fetch_add(1);
                else silentBlocks.fetch_add(1);
                blocks.fetch_add(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        engine.audioDeviceStopped();
    }
    AudioEngine& engine;
    const bool fixedEnergy;
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

TEST_CASE("Buffer and rate rebuilds clear live history while retaining stored traces and calibration", "[app][audio][concurrent][multipos][spl]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start(); ConcurrentCapture producer(engine, true);
    const auto cal = std::make_shared<const foh::MicCal>(foh::parseMicCal("20 2\n1000 2\n20000 2\n"));
    REQUIRE(cal->ok()); analysis.setCal(cal, "Synthetic");
    foh::TraceStore traces;
    foh::SplCal spl; REQUIRE(spl.calibrate(-20, 94));
    for (const auto config : {std::pair<double, int>{48000, 64}, {96000, 256}, {96000, 1024},
                              {44100, 128}, {48000, 1024}}) {
        REQUIRE(waitFor([&] { const auto& s = analysis.latest(); return s.tf.averages > 0 && s.micLevelValid && s.leq.valid && s.calApplied; }));
        REQUIRE(analysis.send(foh::Command::micPosition(0, 2.5)));
        REQUIRE(analysis.send(foh::Command::findDelay(false, 75)));
        REQUIRE(waitFor([&] { return analysis.latest().positionCount == 1 && analysis.latest().delaySeq == 75; }));
        const auto before = analysis.latest();
        REQUIRE(before.delay.ok());
        if (traces.count() == 0) REQUIRE(traces.store("Synthetic before", before.tf, before.refDelayMs));
        const auto stored = traces.at(0);
        producer.requestedRunning.store(false);
        REQUIRE(waitFor([&] { const auto& s = analysis.latest(); return !producer.running.load()
            && s.inputEpoch == engine.captureEpoch.load() && s.tf.averages == 0; }));
        const auto cleared = analysis.latest();
        CHECK(cleared.leq.seconds == 0); CHECK_FALSE(cleared.leq.valid);
        CHECK_FALSE(cleared.micLevelValid); CHECK(cleared.positionMask == 0);
        CHECK(cleared.positionCount == 0); CHECK(cleared.multiPosition.H.empty());
        CHECK_FALSE(cleared.delay.ok()); CHECK(cleared.delaySeq == 0); CHECK_FALSE(cleared.delayWasSub);
        CHECK(cleared.refDelaySamples == 0);
        // A delay request while stopped must not recover the previous device's history.
        REQUIRE(analysis.send(foh::Command::findDelay(true, 76)));
        REQUIRE(waitFor([&] { return analysis.latest().delaySeq == 76; }));
        CHECK_FALSE(analysis.latest().delay.ok());
        producer.requestedRate.store(config.first); producer.requestedBlock.store(config.second);
        producer.requestedRunning.store(true);
        REQUIRE(waitFor([&] { const auto& s = analysis.latest(); return producer.runningRate.load() == config.first
            && producer.runningBlock.load() == config.second && s.inputEpoch == engine.captureEpoch.load()
            && s.tf.sampleRate == config.first && s.tf.averages > 0 && s.leq.valid && s.micLevelValid; }));
        const auto after = analysis.latest();
        CHECK(after.positionMask == 0); CHECK(after.delaySeq == 0); CHECK(after.calApplied);
        // Rate/buffer changes must not add an SPL offset to the unweighted signal.
        CHECK(after.leq.level[2] == Catch::Approx(before.leq.level[2]).margin(0.05));
        CHECK(spl.level(after.leq.level[2]) == Catch::Approx(94).margin(0.05));
        CHECK(traces.at(0).tf.H == stored.tf.H); CHECK(traces.at(0).tf.sampleRate == stored.tf.sampleRate);
        CHECK(traces.at(0).refDelayMs == stored.refDelayMs);
    }
    CHECK(producer.allocations.load() == 0); CHECK(producer.finiteOutput.load());
}

TEST_CASE("A device epoch change invalidates the UI delay before its next timer tick", "[app][audio][delay]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    model.delay.confidence = 1; model.delay.arrivals.push_back({240, 5, 0, 0, false});
    REQUIRE(model.delayLocked());
    model.engine.audioDeviceStopped();
    CHECK_FALSE(model.delayLocked());
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
        // SPL/mic calibration changes invalidate Leq through this same atomic handoff.
        analysis.leqRevision.store(id);
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
                && s.leqRevision == id && s.leq.seconds > 0 && s.tf.averages > 0;
        }));
        const auto snapshot = analysis.latest();
        REQUIRE(snapshot.refDelaySamples == double(id));
        for (double level : snapshot.leq.level) CHECK(std::isfinite(level));
        REQUIRE(snapshot.rtaDb.size() == snapshot.rtaBands.size());
        REQUIRE(snapshot.inputEpoch == engine.captureEpoch.load());
    }
}

TEST_CASE("Multi-mic worker captures replaces clears and compensates reference delay", "[app][audio][multipos]") {
    AudioEngine engine; AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    REQUIRE(analysis.send(foh::Command::micPosition(0, 0)));
    // No averaged audio yet: a capture must not create a position.
    REQUIRE(analysis.send(foh::Command::resetAverages(100)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 100; }));
    REQUIRE(analysis.latest().positionCount == 0);
    foh::PinkNoise noise(123);
    std::vector<float> signal(8192);
    auto feed = [&] {
        for (int i = 0; i < 8; ++i) {
            noise.process(signal.data(), (int)signal.size());
            capture(engine, signal.data(), signal.data(), (int)signal.size());
            REQUIRE(waitFor([&] { return engine.micRing.available() == 0 && engine.refRing.available() == 0; }));
        }
        REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    };
    feed();
    REQUIRE(analysis.send(foh::Command::micPosition(0, 0)));
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 1; }));
    REQUIRE(analysis.latest().multiPosition.H.empty());
    // The raw channels still match. Reference compensation creates +phase, which
    // must be undone by the residual position delay (total 0 ms minus ref 1 ms).
    REQUIRE(analysis.send(foh::Command::referenceDelay(48)));
    REQUIRE(analysis.send(foh::Command::resetAverages(101)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 101; }));
    feed();
    REQUIRE(analysis.send(foh::Command::micPosition(1, 0)));
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 2; }));
    auto avg = analysis.latest().multiPosition;
    REQUIRE(avg.H.size() == 8193);
    // 999 Hz bin: without removing the applied reference delay, the two
    // positions would cancel almost completely instead of remaining at 0 dB.
    const size_t bin = 341;
    REQUIRE(avg.magDb[bin] == Catch::Approx(0).margin(0.1));
    REQUIRE(avg.phaseDeg[bin] == Catch::Approx(0).margin(1));
    REQUIRE(analysis.send(foh::Command::micPosition(2, 0)));
    REQUIRE(waitFor([&] { return analysis.latest().positionMask == 7; }));
    REQUIRE(analysis.latest().positionCount == 3);
    // Replacement and invalid slots cannot grow past three positions.
    REQUIRE(analysis.send(foh::Command::micPosition(1, 0)));
    REQUIRE(analysis.send(foh::Command::micPosition(3, 0)));
    REQUIRE(analysis.send(foh::Command::resetAverages(102)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 102; }));
    REQUIRE(analysis.latest().positionCount == 3);
    REQUIRE(analysis.send(foh::Command::clearMicPositions()));
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 0; }));
    REQUIRE(analysis.latest().multiPosition.H.empty());
    feed();
    REQUIRE(analysis.send(foh::Command::micPosition(0, 0)));
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 1; }));
    engine.captureEpoch.fetch_add(1);
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 0; }));
    REQUIRE(analysis.latest().positionMask == 0);
    analysis.stop();
}

TEST_CASE("Multi-mic TF average overlays the target with controls reachable", "[app][multipos][ui]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    AppModel model;
    auto panel = PanelRegistry::standard().create("TransferFunction", model);
    REQUIRE(panel);
    const auto minimum = panel->minSize();
    panel->setBounds(0, 0, minimum.w, minimum.h);
    panel->addToDesktop(0);
    panel->setVisible(true);
    auto state = fohws::Json::object(); state.set("targetShown", true);
    panel->loadState(state);
    PlotComponent* plot = nullptr;
    juce::ToggleButton* show = nullptr;
    for (auto* child : panel->getChildren()) {
        if (auto* p = dynamic_cast<PlotComponent*>(child)) plot = p;
        if (auto* b = dynamic_cast<juce::ToggleButton*>(child); b && b->getButtonText() == "Multi-mic") show = b;
        if (auto* b = dynamic_cast<juce::TextButton*>(child)) {
            if (b->getButtonText() == "Use found" || b->getButtonText() == "Capture" || b->getButtonText() == "Clear") {
                REQUIRE(panel->getLocalBounds().contains(b->getBounds()));
                REQUIRE(b->getWidth() > 40);
            }
        }
    }
    REQUIRE(plot); REQUIRE(show);
    REQUIRE(panel->getLocalBounds().contains(show->getBounds()));
    REQUIRE(show->getWidth() > 80);
    model.multiPosition.fftSize = 1024;
    model.multiPosition.magDb.assign(513, 2);
    model.multiPosition.coherence.assign(513, 1);
    model.positionCount = 2;
    panel->modelChanged(AppModel::Snapshot);
    auto hasSeries = [&](const juce::String& name) {
        return std::any_of(plot->series.begin(), plot->series.end(), [&](const auto& s) { return s.label == name; });
    };
    REQUIRE(hasSeries("Multi-mic average"));
    for (const auto& series : plot->series) if (series.label == "Multi-mic average") {
        REQUIRE_FALSE(series.v.empty());
        for (double value : series.v) REQUIRE(value == 2);
    }
    REQUIRE(hasSeries("Target +6"));
    show->setToggleState(false, juce::dontSendNotification); show->onClick();
    REQUIRE_FALSE(hasSeries("Multi-mic average"));
    REQUIRE(hasSeries("Target +6"));
    show->setToggleState(true, juce::dontSendNotification);
    model.positionCount = 1; panel->modelChanged(AppModel::Snapshot);
    REQUIRE_FALSE(hasSeries("Multi-mic average"));
}

TEST_CASE("Sweep measurement captures loopback and publishes impulse and ETC", "[app][audio][sweep]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    foh::Command command; command.type = foh::CommandType::MeasureSweep;
    // In hardware this preparation occurs in JUCE's serialized about-to-start callback.
    // The test device supplies 48 kHz, matching the synthetic input stream.
    SyntheticDevice device; device.block = 512;
    engine.audioDeviceAboutToStart(&device);
    analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    REQUIRE(analysis.send(command));
    REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing; }));
    constexpr int block = 512;
    std::vector<float> output(330000), mic(block), ref(block), second(block);
    for (int offset = 0; offset + block <= int(output.size()); offset += block) {
        for (int i = 0; i < block; ++i) {
            const int source = offset + i - block; // common I/O latency cancels
            ref[i] = source >= 0 ? output[size_t(source)] : 0;
            mic[i] = source >= 231 ? 0.6f * (0.6f * output[size_t(source-230)] + 0.4f * output[size_t(source-231)]) : 0;
        }
        const float* inputs[] { mic.data(), ref.data() };
        float* outputs[] { output.data() + offset, second.data() };
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, block, {});
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Full-length deconvolution runs on the worker and can exceed the usual
    // three-second handoff deadline under TSan; retain all impulse assertions.
    REQUIRE(waitFor([&] { return !analysis.latest().sweepCapturing; }, std::chrono::seconds(30)));
    const auto result = analysis.latest().sweep;
    REQUIRE(result.response.ok());
    CHECK(result.response.arrivals[0].delayMs == Catch::Approx(4.8).margin(0.02));
    CHECK(result.etcDb.size() == result.response.impulse.size());
    CHECK(engine.gen.load() == AudioEngine::Off);
    CHECK(std::all_of(output.begin() + 240000, output.end(), [](float value) { return value == 0; }));
    analysis.stop();
}

TEST_CASE("Sweep playback ramps down on stop and up on restart", "[app][audio][sweep]") {
    AudioEngine engine; SyntheticDevice device; engine.audioDeviceAboutToStart(&device);
    std::array<float, 512> a{}, b{};
    float* outputs[] {a.data(), b.data()};
    engine.gen.store(AudioEngine::Sweep);
    for (int block = 0; block < 20; ++block)
        engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, {});
    engine.gen.store(AudioEngine::Off);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, {});
    for (int i = 0; i < 512; ++i) {
        CHECK(std::abs(a[size_t(i)]) <= 0.1 * std::max(0.0, 1 - (i+1)/480.0) + 1e-7);
        CHECK(a[size_t(i)] == b[size_t(i)]);
    }
    engine.gen.store(AudioEngine::Sweep);
    engine.audioDeviceIOCallbackWithContext(nullptr, 0, outputs, 2, 512, {});
    CHECK(a.front() == 0);
    for (int i = 0; i < 480; ++i)
        CHECK(std::abs(a[size_t(i)]) <= 0.1 * i/480.0 + 1e-7);
}

TEST_CASE("Pausing cancels a sweep capture and stops playback", "[app][audio][sweep]") {
    AudioEngine engine; SyntheticDevice device; engine.audioDeviceAboutToStart(&device);
    AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    foh::Command command; command.type = foh::CommandType::MeasureSweep;
    REQUIRE(analysis.send(command));
    REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing; }));
    analysis.paused.store(true);
    REQUIRE(waitFor([&] { return !analysis.latest().sweepCapturing; }));
    CHECK_FALSE(analysis.latest().sweep.response.error.empty());
    CHECK(analysis.latest().sweep.response.impulse.empty());
    CHECK(engine.gen.load() == AudioEngine::Off);
    analysis.stop();
}

TEST_CASE("Analysis engines join pending sweep and meter work on repeated destruction", "[app][audio][lifetime][stress]") {
    for (int pass = 0; pass < 12; ++pass) {
        AudioEngine engine; SyntheticDevice device;
        device.rate = pass % 3 == 0 ? 44100 : pass % 3 == 1 ? 48000 : 96000;
        engine.audioDeviceAboutToStart(&device);
        auto analysis = std::make_unique<AnalysisEngine>(engine);
        analysis->start();
        REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
        std::array<float, 16384> signal {};
        for (size_t i = 0; i < signal.size(); ++i)
            signal[i] = float(0.1 * std::sin(2 * juce::MathConstants<double>::pi * 1000 * i / device.rate));
        capture(engine, signal.data(), signal.data(), int(signal.size()));
        REQUIRE(waitFor([&] { return analysis->latest().leq.seconds > 0; }));
        foh::Command sweep; sweep.type = foh::CommandType::MeasureSweep;
        REQUIRE(analysis->send(sweep));
        REQUIRE(waitFor([&] { return analysis->latest().sweepCapturing; }));
        capture(engine, signal.data(), signal.data(), int(signal.size()));
        analysis->setCal(std::make_shared<foh::MicCal>(), "Synthetic");
        REQUIRE(analysis->send(foh::Command::findDelay(false, pass + 1)));
        // Match JUCE's shutdown order: quiesce I/O before joining the worker.
        engine.audioDeviceStopped();
        analysis.reset();
        CHECK(engine.gen.load() == AudioEngine::Off);
    }
}

struct AppModelMeasurementTestAccess {
    static void poll(AppModel& model) { model.timerCallback(); }
};

TEST_CASE("Hotplug and device errors discard TF RTA SPL and delay history", "[app][audio][hotplug]") {
    AudioEngine engine; AnalysisEngine analysis(engine); SyntheticDevice device;
    engine.audioDeviceAboutToStart(&device); analysis.start();
    float signal[4096]; foh::PinkNoise noise(64); noise.process(signal, 4096);
    float a[4096] {}, b[4096] {}; float* outputs[] {a, b};
    const float* inputs[] {signal, signal};
    for (int event = 0; event < 5; ++event) {
        CAPTURE(event);
        REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
        REQUIRE(waitFor([&] {
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 4096, {});
            std::this_thread::sleep_for(std::chrono::duration<double>(4096.0 / device.rate));
            return analysis.latest().tf.averages > 0 && analysis.latest().leq.seconds > 0;
        }));
        REQUIRE(analysis.send(foh::Command::findDelay(false, event + 1)));
        REQUIRE(waitFor([&] { return analysis.latest().delaySeq == uint64_t(event + 1); }));
        REQUIRE(engine.micPeak.load() > 0);
        const auto oldSeq = analysis.latest().seq;
        if (event == 0) engine.audioDeviceStopped(); // unplug, with no subsequent callbacks
        if (event == 1) { device.rate = 96000; engine.audioDeviceAboutToStart(&device); }
        if (event == 2) { // lost generator output while inputs still work
            engine.gen = AudioEngine::Pink;
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, 256, {});
            CHECK(engine.state.load() == AudioEngine::InvalidOutputs);
            CHECK(engine.gen.load() == AudioEngine::Off);
            CHECK(std::string_view(engine.statusText()).find("Setup") != std::string_view::npos);
        }
        if (event == 3) {
            engine.refCh = 4;
            engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 2, 256, {});
            CHECK(engine.state.load() == AudioEngine::InvalidInputs);
        }
        if (event == 4) {
            std::thread error([&] { engine.audioDeviceError("Synthetic unplug"); }); error.join();
        }
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return s.seq > oldSeq && s.inputEpoch == engine.captureEpoch.load()
                && s.tf.averages == 0 && s.leq.seconds == 0;
        }));
        const auto cleared = analysis.latest();
        CHECK_FALSE(cleared.micLevelValid);
        CHECK_FALSE(cleared.delay.ok());
        CHECK(cleared.convergence.frames == 0);
        for (auto level : cleared.rtaDb) CHECK(level <= -100);
        CHECK(engine.micPeak.load() == 0); CHECK(engine.refPeak.load() == 0);
        // Replacement with the same name/rate is still a new measurement epoch.
        engine.refCh = 1; engine.audioDeviceAboutToStart(&device);
    }
}

TEST_CASE("Device changes cancel model captures without storing a trace or stale SPL", "[app][audio][hotplug]") {
    juce::ScopedJuceInitialiser_GUI gui;
    for (int event = 0; event < 4; ++event) {
        CAPTURE(event);
        AppModel model;
        AppModelMeasurementTestAccess::poll(model);
        model.tf.magDb = {3}; model.rtaDb = {3}; model.leq.seconds = 42;
        model.leq.session = {94, 94, 94}; model.micLevelValid = true;
        model.micPeak = model.refPeak = 0.5f;
        bool completed = false;
        model.capture.active = true;
        model.capture.then = [&](bool settled) { completed = true; CHECK_FALSE(settled); };
        SyntheticDevice device;
        if (event == 0) model.engine.audioDeviceStopped();
        if (event == 1) model.engine.audioDeviceAboutToStart(&device);
        if (event == 2) model.engine.audioDeviceError("Synthetic unplug");
        if (event == 3) {
            float signal[64] {}; const float* inputs[] {signal, signal};
            model.engine.micCh = 4;
            model.engine.audioDeviceIOCallbackWithContext(inputs, 2, nullptr, 0, 64, {});
        }
        AppModelMeasurementTestAccess::poll(model);
        CHECK(completed); CHECK_FALSE(model.capture.active);
        CHECK(model.traces.count() == 0);
        CHECK(model.tf.magDb.empty()); CHECK(model.rtaDb.empty());
        CHECK(model.leq.seconds == 0); CHECK_FALSE(model.micLevelValid);
        CHECK(model.micPeak == 0); CHECK(model.refPeak == 0);
        if (event != 1) { // A capture attempted while still unavailable also fails promptly.
            completed = false; model.capture.active = true;
            model.capture.then = [&](bool settled) { completed = true; CHECK_FALSE(settled); };
            AppModelMeasurementTestAccess::poll(model);
            CHECK(completed); CHECK_FALSE(model.capture.active);
            CHECK(model.traces.count() == 0);
        }
    }
}

TEST_CASE("A replacement missing a configured output requires Setup even with generator off", "[app][audio][hotplug]") {
    AudioEngine engine; SyntheticDevice device;
    engine.audioDeviceAboutToStart(&device);
    const auto before = engine.captureEpoch.load();
    device.activeOutputs = 1; engine.audioDeviceAboutToStart(&device);
    CHECK(engine.captureEpoch.load() > before);
    CHECK(engine.state.load() == AudioEngine::InvalidOutputs);
    float signal[64] {}, output[64]; const float* inputs[] {signal, signal}; float* outputs[] {output};
    engine.analysisEpoch = engine.captureEpoch.load();
    for (int i = 0; i < 3; ++i)
        engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, 64, {});
    CHECK(engine.state.load() == AudioEngine::InvalidOutputs);
    CHECK(engine.micRing.available() == 0);
    CHECK(std::string_view(engine.statusText()).find("Setup") != std::string_view::npos);
    engine.outB = -1; // Correct the map in Setup, then resume with fresh history.
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, 64, {});
    CHECK(engine.state.load() == AudioEngine::Ready);
    engine.analysisEpoch = engine.captureEpoch.load();
    engine.audioDeviceIOCallbackWithContext(inputs, 2, outputs, 1, 64, {});
    CHECK(engine.micRing.available() == 64);
}

TEST_CASE("Asynchronous device errors overlap live analysis without retaining measurements", "[app][audio][hotplug][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine); analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0 && analysis.latest().leq.seconds > 0; }));
    engine.audioDeviceError("Synthetic unplug during analysis");
    REQUIRE(waitFor([&] {
        const auto& s = analysis.latest();
        return s.tf.averages == 0 && s.leq.seconds == 0 && !s.micLevelValid;
    }));
    const auto blocks = producer.blocks.load();
    REQUIRE(waitFor([&] { return producer.blocks.load() > blocks + 8; }));
    CHECK(analysis.latest().tf.averages == 0);
    CHECK(engine.micPeak.load() == 0); CHECK(engine.refPeak.load() == 0);
    // A serialized replacement recovers, but only new audio is averaged.
    producer.requestedRate.store(96000);
    REQUIRE(waitFor([&] {
        const auto& s = analysis.latest();
        return !engine.deviceFailed.load() && s.tf.sampleRate == 96000 && s.tf.averages > 0
            && s.inputEpoch == engine.captureEpoch.load();
    }));
}

TEST_CASE("Device stop and restart during capture discard old analysis and sweep state", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    for (uint64_t id = 1; id <= 3; ++id) {
        REQUIRE(analysis.send(foh::Command::resetAverages(id)));
        REQUIRE(waitFor([&] { return analysis.latest().resetSeq == id && analysis.latest().tf.averages > 0; }));
        foh::Command sweep; sweep.type = foh::CommandType::MeasureSweep;
        REQUIRE(analysis.send(sweep));
        REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing; }));
        const auto epoch = engine.captureEpoch.load();
        producer.requestedRunning.store(false);
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return engine.state.load() == AudioEngine::Stopped && s.inputEpoch > epoch
                && !s.sweepCapturing && !s.micLevelValid && s.tf.averages == 0;
        }));
        REQUIRE(engine.gen.load() == AudioEngine::Off);
        REQUIRE(analysis.latest().sweep.response.impulse.empty());
        producer.requestedRunning.store(true);
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return s.inputEpoch == engine.captureEpoch.load() && s.tf.averages > 0 && s.micLevelValid;
        }));
    }
    CHECK(producer.allocations.load() == 0);
    CHECK(producer.finiteOutput.load());
}

TEST_CASE("Sample-rate change interrupts an active log sweep before publishing new-rate measurements", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    for (double rate : {96000.0, 44100.0, 48000.0}) {
        foh::Command sweep; sweep.type = foh::CommandType::MeasureSweep;
        REQUIRE(analysis.send(sweep));
        REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing; }));
        producer.requestedRate.store(rate);
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return s.tf.sampleRate == rate && s.inputEpoch == engine.captureEpoch.load()
                && s.tf.averages > 0 && !s.sweepCapturing;
        }));
        const auto snapshot = analysis.latest();
        CHECK(snapshot.sweep.response.impulse.empty());
        CHECK_FALSE(snapshot.sweep.response.error.empty());
        CHECK(engine.gen.load() == AudioEngine::Off);
    }
    CHECK(producer.allocations.load() == 0);
    CHECK(producer.finiteOutput.load());
}

TEST_CASE("Generator changes during TF averaging keep playback and worker handoffs RT safe", "[app][audio][concurrent]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    analysis.start();
    ConcurrentCapture producer(engine);
    REQUIRE(waitFor([&] { return analysis.latest().tf.averages > 0; }));
    REQUIRE(analysis.send(foh::Command::averages(0))); // cumulative average across generator changes
    auto seq = analysis.latest().seq;
    for (int mode : {AudioEngine::Pink, AudioEngine::Off, AudioEngine::Sweep, AudioEngine::Off,
                     AudioEngine::Pink, AudioEngine::Sweep, AudioEngine::Off}) {
        const auto audible = producer.audibleBlocks.load();
        const auto silent = producer.silentBlocks.load();
        engine.gen.store(mode);
        engine.genGainDb.store(-24.f + float(mode));
        REQUIRE(waitFor([&] {
            const auto& s = analysis.latest();
            return s.seq > seq && s.tf.averages > 0
                && (mode == AudioEngine::Off ? producer.silentBlocks.load() > silent
                                               : producer.audibleBlocks.load() > audible);
        }));
        const auto snapshot = analysis.latest();
        seq = snapshot.seq;
        CHECK(snapshot.leq.seconds > 0);
        CHECK(snapshot.inputEpoch == engine.captureEpoch.load());
        for (auto level : snapshot.rtaDb) CHECK(std::isfinite(level));
    }
    CHECK(producer.allocations.load() == 0);
    CHECK(producer.finiteOutput.load());
}

TEST_CASE("Target overlays and stored trace edits stay on the reader while live meters update", "[app][audio][concurrent][traces][target]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    // The isolated model supplies UI state only. Start its real analysis worker
    // and copy snapshots on this sole reader, as the production model timer does.
    AppModel model;
    auto panel = PanelRegistry::standard().create("TransferFunction", model);
    REQUIRE(panel);
    panel->setSize(800, 600); panel->addToDesktop(0); panel->setVisible(true);
    model.analysis.start();
    ConcurrentCapture producer(model.engine);
    REQUIRE(waitFor([&] { return model.analysis.latest().tf.averages > 0; }));
    auto seq = model.analysis.latest().seq;
    for (int i = 0; i < 8; ++i) {
        REQUIRE(waitFor([&] { return model.analysis.latest().seq > seq; }));
        const auto snapshot = model.analysis.latest(); // owned copy, never keep a borrowed slot
        seq = snapshot.seq;
        model.tf = snapshot.tf; model.leq = snapshot.leq;
        model.rtaDb = snapshot.rtaDb; model.rtaBands = snapshot.rtaBands;
        model.micPeak = model.engine.micPeak.exchange(0);
        CHECK(model.leq.seconds > 0);
        auto state = fohws::Json::object();
        state.set("targetChoice", i % 3 + 1); state.set("targetShown", true);
        state.set("targetBand", bool(i % 2)); state.set("targetOffset", double(i));
        panel->loadState(state);
        model.clearTraces();
        REQUIRE(model.traces.store("Synthetic", snapshot.tf, snapshot.refDelayMs));
        model.renameTrace(0, "Edited"); model.toggleTrace(0); model.toggleTrace(0);
        model.selectSum(0, 0);
        const auto sum = model.sumPreview();
        REQUIRE(sum.magDb.size() == snapshot.tf.magDb.size());
        CHECK(sum.magDb[341] == Catch::Approx(snapshot.tf.magDb[341] + 6.0206).margin(0.01));
        model.notify(AppModel::Snapshot | AppModel::Traces);
        PlotComponent* plot = nullptr;
        for (auto* child : panel->getChildren())
            if (auto* p = dynamic_cast<PlotComponent*>(child)) plot = p;
        REQUIRE(plot);
        const auto target = "Target +" + juce::String(6 + (i % 3) * 3);
        CHECK(std::any_of(plot->series.begin(), plot->series.end(),
            [&](const auto& s) { return s.label == target; }));
        CHECK(std::any_of(plot->series.begin(), plot->series.end(),
            [](const auto& s) { return s.label == "Edited"; }));
        CHECK(plot->toleranceBands.size() == size_t(i % 2));
        CHECK(snapshot.tf.H == model.traces.at(0).tf.H);
    }
    CHECK(producer.allocations.load() == 0);
    CHECK(producer.finiteOutput.load());
}

TEST_CASE("Invalid level input stays finite and cannot calibrate SPL", "[app][audio][splcal][edges]") {
    AudioEngine engine; AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    int revision = 0;
    for (float value : {0.f, 0.25f, 1.f, -1.f, std::numeric_limits<float>::denorm_min(), NAN, INFINITY}) {
        analysis.leqRevision = ++revision;
        REQUIRE(waitFor([&] { return analysis.latest().leqRevision == uint64_t(revision); }));
        std::vector<float> input(4800, value);
        capture(engine, input.data(), input.data(), int(input.size()));
        REQUIRE(waitFor([&] { return analysis.latest().leq.seconds >= 0.1; }));
        const auto snapshot = analysis.latest();
        CHECK_FALSE(snapshot.micLevelValid); CHECK_FALSE(snapshot.leq.valid);
        CHECK(std::isfinite(snapshot.micLevelDbfs));
        for (auto level : snapshot.leq.session) CHECK(std::isfinite(level));
        if (value != 0.25f) for (auto coherence : snapshot.tf.coherence) CHECK(coherence == 0);
        REQUIRE(analysis.send(foh::Command::micPosition(0, 0)));
        auto reset = foh::Command::resetAverages(uint64_t(revision) + 1);
        REQUIRE(analysis.send(reset));
        REQUIRE(waitFor([&] { return analysis.latest().resetSeq == reset.id; }));
        CHECK(analysis.latest().positionCount == 0);
    }
    analysis.stop();
}

TEST_CASE("Rate restart invalidates in-flight sweeps and multi-mic captures", "[app][audio][sweep][multipos][edges]") {
    AudioEngine engine; SyntheticDevice device; engine.audioDeviceAboutToStart(&device);
    AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    std::vector<float> noise(32768); foh::PinkNoise generator; generator.process(noise.data(), int(noise.size()));
    capture(engine, noise.data(), noise.data(), int(noise.size()));
    REQUIRE(waitFor([&] { return analysis.latest().convergence.frames > 0; }));
    foh::Command command; command.type = foh::CommandType::StoreMicPosition; command.i = 0;
    REQUIRE(analysis.send(command));
    REQUIRE(waitFor([&] { return analysis.latest().positionCount == 1; }));
    command.type = foh::CommandType::MeasureSweep; REQUIRE(analysis.send(command));
    REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing; }));
    engine.audioDeviceStopped(); device.rate = 96000; engine.audioDeviceAboutToStart(&device);
    REQUIRE(waitFor([&] { return analysis.latest().tf.sampleRate == 96000; }));
    const auto snapshot = analysis.latest();
    CHECK_FALSE(snapshot.sweepCapturing); CHECK_FALSE(snapshot.sweep.response.ok());
    CHECK_FALSE(snapshot.sweep.response.error.empty()); CHECK(snapshot.positionCount == 0);
    CHECK(snapshot.multiPosition.H.empty()); CHECK_FALSE(snapshot.leq.valid);
    analysis.stop();
}

TEST_CASE("Unusable live pairs clear coherent TF before multi-mic capture", "[app][audio][multipos][show][edges]") {
    AudioEngine engine; AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(analysis.send(foh::Command::resetAverages(1)));
    REQUIRE(waitFor([&] { return analysis.latest().resetSeq == 1; }));
    std::vector<float> noise(32768); foh::PinkNoise generator; generator.process(noise.data(), int(noise.size()));
    for (float value : {0.f, 1.f, NAN, INFINITY}) {
        capture(engine, noise.data(), noise.data(), int(noise.size()));
        REQUIRE(waitFor([&] { return foh::coherenceSummary(analysis.latest().tf).meanCoherence > 0.9; }));
        std::vector<float> bad(32768, value);
        // Missing/silent reference and damaged reference must invalidate live TF.
        capture(engine, noise.data(), bad.data(), int(noise.size()));
        REQUIRE(waitFor([&] {
            const auto& tf = analysis.latest().tf;
            return !tf.coherence.empty() && std::all_of(tf.coherence.begin(), tf.coherence.end(), [](double c) { return c == 0; });
        }));
        for (auto coherence : analysis.latest().tf.coherence) CHECK(coherence == 0);
        foh::Command store; store.type = foh::CommandType::StoreMicPosition;
        REQUIRE(analysis.send(store));
        auto reset = foh::Command::resetAverages(analysis.latest().resetSeq + 1);
        REQUIRE(analysis.send(reset));
        REQUIRE(waitFor([&] { return analysis.latest().resetSeq == reset.id; }));
        CHECK(analysis.latest().positionCount == 0);
    }
    capture(engine, noise.data(), noise.data(), int(noise.size()));
    REQUIRE(waitFor([&] { return foh::coherenceSummary(analysis.latest().tf).meanCoherence > 0.9; }));
    analysis.stop();
}

TEST_CASE("Analysis applies stored loopback only to its capture epoch", "[app][audio][loopback]") {
    AudioEngine engine; AnalysisEngine analysis(engine);
    engine.captureEpoch.store(1); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == 1; }));
    foh::LoopbackCorrection c; c.sampleRate = 48000; c.fftSize = 16384;
    c.H.assign(8193, foh::cpx(.5, 0)); REQUIRE(c.valid());
    auto correction = std::make_shared<const foh::LoopbackCorrection>(c);
    auto revision = analysis.setLoopback(correction, 1);
    foh::PinkNoise noise(23); std::vector<float> ref(16384), mic(ref.size());
    for (size_t i = 0; i < ref.size(); ++i) { ref[i] = noise.next(); mic[i] = .5f * ref[i]; }
    auto feed = [&] {
        for (int block = 0; block < 4; ++block) {
            capture(engine, mic.data(), ref.data(), (int)ref.size());
            REQUIRE(waitFor([&] { return engine.micRing.available() == 0; }));
        }
        REQUIRE(waitFor([&] {
            const auto& snapshot = analysis.latest();
            return snapshot.inputEpoch == engine.captureEpoch.load() && snapshot.tf.averages >= 3;
        }));
    };
    auto acknowledged = [&] {
        REQUIRE(waitFor([&] { return analysis.latest().loopbackRevision == revision; }));
        return analysis.latest(); // copy: latest() only keeps a reference until the next read
    };
    feed();
    auto snapshot = acknowledged();
    CHECK(snapshot.inputEpoch == 1);
    CHECK(snapshot.tf.magDb[100] == Catch::Approx(0).margin(.1));
    CHECK(snapshot.rawTf.magDb[100] == Catch::Approx(-6.0206).margin(.1));

    // Publications can advance without consuming a correction update. A newer seq
    // alone cannot acknowledge setLoopback (the intermittent failure in #101).
    auto seq = snapshot.seq;
    REQUIRE(waitFor([&] { return analysis.latest().seq > seq; }));
    CHECK(analysis.latest().loopbackRevision == revision);
    revision = analysis.setLoopback(correction, 0); // stale routing
    snapshot = acknowledged();
    CHECK(snapshot.tf.magDb[100] == Catch::Approx(-6.0206).margin(.1));

    revision = analysis.setLoopback(correction, 1);
    CHECK(acknowledged().tf.magDb[100] == Catch::Approx(0).margin(.1));
    // A device/capture restart invalidates an already-consumed correction too.
    engine.captureEpoch.fetch_add(1, std::memory_order_release);
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == 2; }));
    feed();
    snapshot = analysis.latest();
    CHECK(snapshot.inputEpoch == 2);
    CHECK(snapshot.loopbackRevision == revision);
    CHECK(snapshot.tf.magDb[100] == Catch::Approx(-6.0206).margin(.1));
    revision = analysis.setLoopback(correction, 2);
    CHECK(acknowledged().tf.magDb[100] == Catch::Approx(0).margin(.1));
    revision = analysis.setLoopback(nullptr, 2);
    CHECK(acknowledged().tf.magDb[100] == Catch::Approx(-6.0206).margin(.1));
    analysis.stop();
}

TEST_CASE("Guide sweep cancellation is scoped to its accepted request", "[app][audio][sweep][guide][edges]") {
    AudioEngine engine; SyntheticDevice device; engine.audioDeviceAboutToStart(&device);
    AnalysisEngine analysis(engine); analysis.start();
    REQUIRE(waitFor([&] { return engine.analysisEpoch.load() == engine.captureEpoch.load(); }));
    foh::Command sweep; sweep.type = foh::CommandType::MeasureSweep; sweep.id = 42;
    REQUIRE(analysis.send(sweep));
    REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing && analysis.latest().sweepSeq == 42; }));
    foh::Command cancel; cancel.type = foh::CommandType::CancelSweep; cancel.id = 42;
    REQUIRE(analysis.send(cancel));
    REQUIRE(waitFor([&] { return !analysis.latest().sweepCapturing; }));
    CHECK(engine.gen.load() == AudioEngine::Off);
    sweep.id = 43; REQUIRE(analysis.send(sweep));
    REQUIRE(waitFor([&] { return analysis.latest().sweepCapturing && analysis.latest().sweepSeq == 43; }));
    const auto seq = analysis.latest().seq;
    REQUIRE(analysis.send(cancel)); // delayed cancellation from the retired guide
    REQUIRE(waitFor([&] { return analysis.latest().seq > seq; }));
    CHECK(analysis.latest().sweepCapturing); CHECK(analysis.latest().sweepSeq == 43);
    cancel.id = 43; REQUIRE(analysis.send(cancel));
    REQUIRE(waitFor([&] { return !analysis.latest().sweepCapturing; }));
    CHECK(engine.gen.load() == AudioEngine::Off); analysis.stop();
}
