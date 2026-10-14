// AppModel: the shared model every Panel reads from. Owns audio I/O, the analysis thread bridge
// (SnapshotPublisher/CommandQueue via AnalysisEngine), trace store, mic cal and the guide/overlay state.
// UI thread only. Panels subscribe as Listeners; nothing here draws.
#pragma once
#include "Widgets.h"
#include "AnalysisEngine.h"
#include <fohcore/json.h>
#include <map>
#include <array>
#include <optional>
#include <fohcore/rig_profile.h>

class AppModel : private juce::Timer {
public:
    enum What { Snapshot = 1, Delay = 2, Traces = 4, Cal = 8, Levels = 16, Capture = 32, Overlay = 64, Settings = 128, Theme = 256, Rig = 512 };
    struct Listener { virtual ~Listener() = default; virtual void modelChanged(int what) = 0; };

    AppModel();
    ~AppModel() override;
    void addListener(Listener* l) { listeners.add(l); }
    void removeListener(Listener* l) { listeners.remove(l); }
    void notify(int what) { listeners.call([what](Listener& l) { l.modelChanged(what); }); }

    // ---- audio / analysis
    juce::AudioDeviceManager dm; AudioEngine engine; AnalysisEngine analysis { engine };
    juce::ApplicationProperties settings;
    void send(const foh::Command& c) { analysis.send(c); }
    void setPaused(bool p) { paused = p; analysis.paused = p; notify(Levels); }
    bool paused = false;

    // ---- copied from the latest AnalysisSnapshot (latest() is only valid until the next call)
    foh::SweepResult sweep; bool sweepCapturing = false;
    foh::TFResult multiPosition; int positionCount = 0; uint8_t positionMask = 0;
    foh::TFResult tf; foh::DelayResult delay; bool delayWasSub = false;
    std::vector<double> rtaDb; std::vector<foh::Band> rtaBands; foh::ConvergenceState conv;
    double refDelaySamples = 0, refDelayMs = 0, sampleRate = 48000; bool calApplied = false; juce::String calName;
    float micPeak = 0, refPeak = 0; // linear, last timer period

    // ---- measurement settings (shared by toolbar pills / TF / RTA panels)
    int smoothingFraction = 12, averages = 16, rtaFraction = 3; double blankThreshold = 0.5; // QA (Vega): default TF coherence-blanking threshold 0.5, adjustable in the TF panel
    void setSmoothing(int f) { smoothingFraction = f; send(foh::Command::smoothing(f)); notify(Settings); }
    void setAverages(int n) { averages = n; send(foh::Command::averages(n)); notify(Settings); }
    void setRtaFraction(int f) { rtaFraction = f; send(foh::Command::rtaFraction(f)); notify(Settings); }
    void setBlankThreshold(double t) { blankThreshold = t; send(foh::Command::blankThreshold(t)); notify(Settings); }

    // ---- delay finder
    bool delayLocked() const { return delay.ok(); }
    void requestDelay(bool sub, std::function<void()> then = {});
    void lockDelayFromLast();
    void cancelSessionOperations() {
        capture.active = false; capture.then = {}; pendingDelay = {};
        delayPending = false; delay = {}; overlay = {};
    }
    double lockedDelayMs = 0; bool delayPending = false;

    // ---- traces
    foh::TraceStore traces;
    bool storeTrace(const juce::String& name);
    const foh::Trace* findTrace(const char* n) const;
    // Message-thread only, including the allocating virtual-sum calculation.
    bool validTrace(int i) const { return i >= 0 && i < traces.count(); }
    juce::Colour traceColour(int i) const { return traceColours[(size_t) i].value_or(foh::ui::col::storedTrace(i % 8)); }
    void renameTrace(int i, const juce::String& name) {
        if (!validTrace(i) || name.trim().isEmpty()) return;
        traces.at(i).name = name.trim().toStdString(); notify(Traces);
    }
    void colourTrace(int i, juce::Colour colour) {
        if (!validTrace(i)) return;
        traceColours[(size_t) i] = colour; notify(Traces);
    }
    void removeTrace(int i) {
        if (!traces.remove(i)) return;
        for (size_t j = (size_t) i; j + 1 < traceColours.size(); ++j) traceColours[j] = traceColours[j + 1];
        traceColours.back().reset();
        for (auto* source : {&sumA, &sumB}) { if (*source == i) *source = -1; else if (*source > i) --*source; }
        notify(Traces);
    }
    void clearTraces() { traces.clear(); traceColours = {}; sumA = sumB = -1; notify(Traces); }
    void toggleTrace(int i) { if (validTrace(i)) { traces.at(i).visible = !traces.at(i).visible; notify(Traces); } }
    int sumA = -1, sumB = -1;
    void selectSum(int a, int b) { sumA = validTrace(a) ? a : -1; sumB = validTrace(b) ? b : -1; notify(Traces); }
    foh::TFResult sumPreview() const {
        if (!validTrace(sumA) || !validTrace(sumB)) return {};
        const auto& a = traces.at(sumA).tf; const auto& b = traces.at(sumB).tf;
        auto complete = [](const foh::TFResult& t) {
            auto n = t.freq.size(); return n > 0 && t.H.size() == n && t.magDb.size() == n
                && t.phaseDeg.size() == n && t.coherence.size() == n;
        };
        if (!complete(a) || !complete(b) || a.freq != b.freq) return {};
        return foh::virtualSum(a, b);
    }

    // ---- capture: fresh core settle result, or warn and proceed after 10 s
    struct { bool active = false, unsettled = false; juce::String name; double startMs = 0; uint64_t resetId = 0; bool resetAcknowledged = false; std::function<void(bool)> then; } capture;
    void beginCapture(const juce::String& name, std::function<void(bool)> then);

    // SPL calibration is message-thread state, keyed by audio system/device/channel.
    std::map<std::string, foh::SplCal> splCals;
    foh::LeqReading leq;
    uint64_t leqRevision = 0;
    double micLevelDbfs = -120; bool micLevelValid = false;
    bool micLevelReady() const;
    std::string splInputKey() const;
    foh::SplCal splCal() const;
    double splSensitivity() const { return calOn && cal.hasSensFactor ? cal.sensitivityOffsetDb : 0; }
    double micLevel() const { return splCal().level(micLevelDbfs - (calOn ? cal.dbAt(1000) : 0), splSensitivity()); }
    bool calibrateSpl(double knownLevel);
    void clearSpl();
    foh::Json splCalJson() const;
    void loadSplCalJson(const foh::Json&);

    // Interface correction is stored per device/rate/routing, message thread only.
    std::map<std::string, foh::LoopbackCorrection> loopbackCals;
    std::string loopbackKey() const;
    bool beginLoopback();
    void clearLoopback();
    bool loopbackMeasuring = false;
    juce::String loopbackStatus;

    // ---- mic cal
    foh::MicCal cal; juce::String calFile; std::string calText; bool calOn = false;
    void loadCalFile(const juce::File& f);
    void setCalEnabled(bool on);

    std::optional<foh::RigProfile> rigProfile; // session-wide, message-thread only

    // ---- guide overlay drawn by the TF / Phase panels while a guide step needs it
    struct Overlay { std::vector<PlotComponent::Series> mag, phase; std::vector<std::pair<double,double>> shade; juce::String shadeLabel, phaseNote; bool phaseOnly250 = false; bool active = false; } overlay;

    std::function<void()> overlayBuilder; // set by the Guide panel; run before each notify so plots see a fresh overlay
    std::function<void(const std::string&)> requestWorkspace; // guide actions such as "Open Setup"

    // ---- inspector text for the focused panel
    juce::String inspectorTitle, inspectorText;
    void setInspector(const juce::String& title, const juce::String& text) { inspectorTitle = title; inspectorText = text; notify(Overlay); }

private:
    std::array<std::optional<juce::Colour>, foh::TraceStore::kMax> traceColours;
    void syncLoopback();
    void finishLoopback(const foh::TFResult* raw = nullptr);
    std::string activeLoopbackKey, measuringLoopbackKey;
    uint64_t activeLoopbackEpoch = 0, loopbackResetId = 0;
    double loopbackStart = 0, savedReferenceDelay = 0;
    int savedGenerator = 0; float savedGeneratorGain = -20;
    bool savedPaused = false;
    std::string levelInputKey; uint64_t levelInputEpoch = 0; double levelTimeMs = 0;
    void timerCallback() override;
    juce::ListenerList<Listener> listeners;
    int lastGeneratorMode = AudioEngine::Off;
    uint64_t lastInputEpoch = 0; bool lastDeviceFailed = false;
    uint64_t lastSeq = 0, lastDelaySeq = 0, nextDelayId = 0, nextCaptureId = 0;
    struct { uint64_t id = 0; std::function<void()> then; } pendingDelay;
};
