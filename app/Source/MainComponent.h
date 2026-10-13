#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "fohcore/fohcore.h"
#include "ThemeBridge.h"

namespace th = foh::theme;

// All non-ASCII UI text is written as UTF-8 \x escapes and MUST go through u8():
// juce::String(const char*) treats bytes as ASCII/Latin-1, so symbols like \u2713 render as junk.
inline juce::String u8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// Audio engine: generator out + capture of mic/ref into lock-free rings. RT-safe callback.
class AudioEngine : public juce::AudioIODeviceCallback {
public:
    enum Gen { Off = 0, Pink = 1, Sweep = 2 };
    std::atomic<int> gen{Off}, micCh{0}, refCh{1}, outA{0}, outB{1};
    std::atomic<float> genGainDb{-20.f};
    std::atomic<float> micPeak{0}, refPeak{0};
    foh::RingBuffer micRing{1 << 19}, refRing{1 << 19};
    std::atomic<double> sampleRate{48000};
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
private:
    foh::PinkNoise pink; foh::LogSweep sweep; float lastGain = -999;
    float scratch[4096] {};
};

// Inter (OFL) bundled via BinaryData; falls back to system sans if missing.
class FohLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FohLookAndFeel();
    void applyTheme(); // re-read ColourIds from the current Theme
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    static juce::Font font(float size, bool bold = false, bool semibold = false);
    static juce::Typeface::Ptr regular, semibold, bold;
};

class PlotComponent : public juce::Component {
public:
    struct Series { std::vector<double> f, v; juce::Colour c; const float* dash = nullptr; float width = foh::ui::dim::trace(); double maxF = 1e9; juce::String label; };
    struct Annotation { double lo, hi; juce::String label; };
    std::vector<Annotation> annotations; bool showLegend = false; // legend row: series with a label, 13 px, gap 18, padding 4/14
    juce::String yLabel; double yMin = -12, yMax = 12, yStep = 6; double fMin = 20, fMax = 20000; bool wrapPhase = false;
    std::vector<Series> series; std::vector<std::pair<double,double>> shade; juce::String shadeLabel; juce::String note, centreNote;
    bool bars = false; std::vector<double> barLo, barHi;
    std::vector<std::pair<double,double>> lowCoh; // hatched low-coherence regions (Hz)
    bool exampleData = false; // spec: EXAMPLE DATA tag must stay on every example/mock trace until real data exists
    void paint(juce::Graphics&) override;
    float xOf(double f) const; juce::Image hatchTile; juce::Colour hatchFor; // cached hatch pattern, rebuilt only on theme colour change
    float yOf(double v) const;
    int plotX() const { return 46; } int plotY() const { return 8; } int plotW() const { return getWidth() - 46 - 14; } int plotH() const { return getHeight() - 8 - 24 - (showLegend ? 22 : 0); }
};

// Simple impulse / delay view: delay-finder correlation (DelayResult::impulse, lag in samples) around the first
// arrival, with a marker + label per arrival. Display only; the peak picking is fohcore's.
class ImpulseComponent : public juce::Component {
public:
    void setResult(const foh::DelayResult& r, double fs) { res = r; sampleRate = fs; repaint(); }
    void paint(juce::Graphics&) override;
    foh::DelayResult res; double sampleRate = 48000; double preMs = 5, postMs = 45;
};

// Status bar pills (layout-spec: auto width, h ~30, padding 6/12, radius 18, 1 px pillBorder, 13 px, 8x8 status mark).
// Status is never colour-only: good = circle, caution = triangle, poor = square, always with a text label.
class StatusPills : public juce::Component {
public:
    enum State { None = -1, Good = 0, Caution = 1, Poor = 2 };
    struct Pill { juce::String text; int state = None; };
    void setPills(std::vector<Pill> p) { if (p.size() != pills.size() || !std::equal(p.begin(), p.end(), pills.begin(), [](auto& a, auto& b) { return a.text == b.text && a.state == b.state; })) { pills = std::move(p); repaint(); } }
    void paint(juce::Graphics&) override;
    std::vector<Pill> pills;
};

class MainComponent : public juce::Component, private juce::Timer, private juce::ChangeListener {
public:
    MainComponent(); ~MainComponent() override;
    void paint(juce::Graphics&) override; void resized() override;
private:
    void timerCallback() override; void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void showPage(int p); void updateTF(); void updateRTA(); void updateGuidePlots(); void findDelay(); bool storeTrace(const juce::String& name);
    void refreshTraceList(); void loadCal(); void layoutGuide(); void guideAction(int which);
    juce::String explain() const; const foh::Trace* findTrace(const char* n) const;
    juce::Rectangle<int> mainArea() const;

    FohLookAndFeel lnf;
    juce::AudioDeviceManager dm; AudioEngine engine;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    std::unique_ptr<foh::TransferFunction> tf; std::unique_ptr<foh::RTA> rta; double analysisRate = 48000;
    std::vector<float> micHist, refHist; int histPos = 0;
    foh::TraceStore traces; foh::MicCal cal; bool calOn = false; foh::DelayResult lastDelay; foh::TFResult lastTF;
    bool paused = false; foh::Band2 xover; std::vector<std::pair<juce::String, juce::String>> summaryRows; // step 09 Done card rows (label, value)
    // frame
    juce::OwnedArray<juce::TextButton> nav; int page = 0; StatusPills status; juce::TextButton pauseBtn {"Pause"};
    juce::Label help, pageTitle;
    // setup
    juce::ComboBox micBox, refBox, outABox, outBBox, genBox; juce::Slider genGain; juce::TextButton calBtn {"Load mic cal..."}, calToggle {"Cal ON"};
    juce::Label setupHelp, calLabel, levelLabel; juce::ComboBox themeBox;
    // TF
    PlotComponent magPlot, phasePlot, cohPlot; juce::ComboBox smoothBox, avgBox; juce::TextButton storeBtn {"Store"}, clearAvgBtn {"Reset avg"};
    juce::Label delayCaption, delayValue, delayDetail; juce::TextButton findBtn {"Find again"}, insertBtn {"Insert delay"};
    juce::ListBox traceList; juce::TextButton delTraceBtn {"Delete trace"};
    struct TraceModel : juce::ListBoxModel { MainComponent* o = nullptr; int getNumRows() override; void paintListBoxItem(int, juce::Graphics&, int, int, bool) override; void listBoxItemClicked(int, const juce::MouseEvent&) override; } traceModel;
    juce::Rectangle<int> tfCard, delayCard;
    // RTA
    PlotComponent rtaPlot; juce::ComboBox rtaFracBox;
    // Guides
    juce::ComboBox guideBox; juce::Label guideStepLabel, guideBody, guideResult, guideReadout, guidePolarity; juce::TextButton guidePrev {"Back"}, guideAct {"Do it"};
    ImpulseComponent tfImpulse, gImpulse;
    PlotComponent gPlotA, gPlotB; juce::Label doneSummary, doneActions; juce::TextButton doneAgain {"Measure again"}, doneHome {"Back to Home"};
    juce::Rectangle<int> gProgress, gChartCard, gActionCard, gDoneA, gDoneB;
    int guide = 1, step = 0; double lockedDelayMs = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
