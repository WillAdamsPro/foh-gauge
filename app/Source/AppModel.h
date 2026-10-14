// AppModel: the shared model every Panel reads from. Owns audio I/O, the analysis thread bridge
// (SnapshotPublisher/CommandQueue via AnalysisEngine), trace store, mic cal and the guide/overlay state.
// UI thread only. Panels subscribe as Listeners; nothing here draws.
#pragma once
#include "Widgets.h"
#include "AnalysisEngine.h"
#include <fohcore/json.h>
#include <map>

class AppModel : private juce::Timer {
public:
    enum What { Snapshot = 1, Delay = 2, Traces = 4, Cal = 8, Levels = 16, Capture = 32, Overlay = 64, Settings = 128, Theme = 256 };
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
    void removeTrace(int i) { traces.remove(i); notify(Traces); }
    void toggleTrace(int i) { traces.at(i).visible = !traces.at(i).visible; notify(Traces); }

    // ---- capture: fresh core settle result, or warn and proceed after 10 s
    struct { bool active = false, unsettled = false; juce::String name; double startMs = 0; uint64_t resetId = 0; bool resetAcknowledged = false; std::function<void(bool)> then; } capture;
    void beginCapture(const juce::String& name, std::function<void(bool)> then);

    // SPL calibration is message-thread state, keyed by audio system/device/channel.
    std::map<std::string, foh::SplCal> splCals;
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

    // ---- mic cal
    foh::MicCal cal; juce::String calFile; std::string calText; bool calOn = false;
    void loadCalFile(const juce::File& f);
    void setCalEnabled(bool on);

    // ---- guide overlay drawn by the TF / Phase panels while a guide step needs it
    struct Overlay { std::vector<PlotComponent::Series> mag, phase; std::vector<std::pair<double,double>> shade; juce::String shadeLabel, phaseNote; bool phaseOnly250 = false; bool active = false; } overlay;

    std::function<void()> overlayBuilder; // set by the Guide panel; run before each notify so plots see a fresh overlay
    std::function<void(const std::string&)> requestWorkspace; // guide actions such as "Open Setup"

    // ---- inspector text for the focused panel
    juce::String inspectorTitle, inspectorText;
    void setInspector(const juce::String& title, const juce::String& text) { inspectorTitle = title; inspectorText = text; notify(Overlay); }

private:
    std::string levelInputKey; uint64_t levelInputEpoch = 0; double levelTimeMs = 0;
    void timerCallback() override;
    juce::ListenerList<Listener> listeners;
    uint64_t lastInputEpoch = 0; bool lastDeviceFailed = false;
    uint64_t lastSeq = 0, lastDelaySeq = 0, nextDelayId = 0, nextCaptureId = 0;
    struct { uint64_t id = 0; std::function<void()> then; } pendingDelay;
};
