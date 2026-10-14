// AnalysisEngine: the single analysis thread between the audio callback and the UI.
// audio thread -> RingBuffers -> [this thread: fohcore TF / RTA / delay finder] -> SnapshotPublisher -> UI
// UI -> CommandQueue -> this thread. The UI never runs TF/RTA/delay processing itself.
#pragma once
#include <juce_core/juce_core.h>
#include <mutex>
#include <memory>
#include <array>
#include "fohcore/fohcore.h"

class AudioEngine;

class AnalysisEngine : private juce::Thread {
public:
    explicit AnalysisEngine(AudioEngine& e) : juce::Thread("FOH analysis"), engine(e) {}
    ~AnalysisEngine() override { stop(); }
    void start() { startThread(juce::Thread::Priority::normal); }
    void stop() { stopThread(-1); }

    // UI thread only (single producer / single reader)
    bool send(const foh::Command& c) { return commands.push(c); }
    const foh::AnalysisSnapshot& latest() { return publisher.latest(); }
    // Cal is not POD, so it is handed over by pointer swap (rare, non-RT). nullptr = cal off.
    void setCal(std::shared_ptr<const foh::MicCal> cal, const juce::String& name);
    std::atomic<bool> paused { false };

private:
    void run() override;
    void rebuild(double fs);
    void handle(const foh::Command& c);
    void publish();

    AudioEngine& engine;
    foh::SnapshotPublisher publisher;
    foh::CommandQueue commands;
    std::mutex calLock; std::shared_ptr<const foh::MicCal> pendingCal; juce::String pendingCalName; bool calDirty = false;
    std::shared_ptr<const foh::MicCal> cal; std::string calName;

    // analysis-thread state
    std::array<foh::MicPosition, 3> positions;
    uint8_t positionMask = 0;
    foh::TFResult positionAverage; int positionCount = 0;
    double levelEnergy = 0; int levelSamples = 0;
    uint64_t inputEpoch = 0; int micChannel = -1;
    double fs = 0; int fftN = 16384;
    std::unique_ptr<foh::TransferFunction> tf; std::unique_ptr<foh::RTA> rta;
    int smoothing = 12, rtaFrac = 3, averages = 16; double blankThr = 0.6;
    std::vector<float> histRef, histMic; int histPos = 0; // last 3 s, including the sub delay window
    foh::DelayResult delay; uint64_t delaySeq = 0, resetSeq = 0; bool delayWasSub = false;
};
