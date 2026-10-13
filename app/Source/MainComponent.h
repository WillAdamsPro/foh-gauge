#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "fohcore/fohcore.h"
#include "FohTheme.h"

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
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    static juce::Font font(float size, bool bold = false, bool semibold = false);
    static juce::Typeface::Ptr regular, semibold, bold;
};

class PlotComponent : public juce::Component {
public:
    struct Series { std::vector<double> f, v; juce::Colour c; const float* dash = nullptr; float width = th::stroke::trace; double maxF = 1e9; };
    juce::String yLabel; double yMin = -12, yMax = 12, yStep = 6; double fMin = 20, fMax = 20000; bool wrapPhase = false;
    std::vector<Series> series; std::vector<std::pair<double,double>> shade; juce::String shadeLabel; juce::String note, centreNote;
    bool bars = false; std::vector<double> barLo, barHi;
    bool exampleData = false; // spec: EXAMPLE DATA tag must stay on every example/mock trace until real data exists
    void paint(juce::Graphics&) override;
    float xOf(double f) const; float yOf(double v) const;
    int plotX() const { return 46; } int plotY() const { return 8; } int plotW() const { return getWidth() - 46 - 14; } int plotH() const { return getHeight() - 8 - 24; }
};

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent(); ~MainComponent() override;
    void paint(juce::Graphics&) override; void resized() override;
private:
    void timerCallback() override;
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
    bool paused = false; foh::Band2 xover; juce::String w2Summary;
    // frame
    juce::OwnedArray<juce::TextButton> nav; int page = 0; juce::Label status; juce::TextButton pauseBtn {"Pause"};
    juce::Label help, pageTitle;
    // setup
    juce::ComboBox micBox, refBox, outABox, outBBox, genBox; juce::Slider genGain; juce::TextButton calBtn {"Load mic cal..."}, calToggle {"Cal ON"};
    juce::Label setupHelp, calLabel, levelLabel;
    // TF
    PlotComponent magPlot, phasePlot, cohPlot; juce::ComboBox smoothBox, avgBox; juce::TextButton storeBtn {"Store"}, clearAvgBtn {"Reset avg"};
    juce::Label delayCaption, delayValue, delayDetail; juce::TextButton findBtn {"Find again"}, insertBtn {"Insert delay"};
    juce::ListBox traceList; juce::TextButton delTraceBtn {"Delete trace"};
    struct TraceModel : juce::ListBoxModel { MainComponent* o = nullptr; int getNumRows() override; void paintListBoxItem(int, juce::Graphics&, int, int, bool) override; void listBoxItemClicked(int, const juce::MouseEvent&) override; } traceModel;
    juce::Rectangle<int> tfCard, delayCard;
    // RTA
    PlotComponent rtaPlot; juce::ComboBox rtaFracBox;
    // Guides
    juce::ComboBox guideBox; juce::Label guideStepLabel, guideBody, guideResult, guideReadout; juce::TextButton guidePrev {"Back"}, guideAct {"Do it"};
    PlotComponent gPlotA, gPlotB; juce::Label doneSummary, doneActions; juce::TextButton doneAgain {"Measure again"}, doneHome {"Back to Home"};
    juce::Rectangle<int> gProgress, gChartCard, gActionCard, gDoneA, gDoneB;
    int guide = 1, step = 0; double lockedDelayMs = 0;
    std::unique_ptr<juce::FileChooser> chooser;
};
