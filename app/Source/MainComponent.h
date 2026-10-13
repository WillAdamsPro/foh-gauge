#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "fohcore/fohcore.h"

namespace Col {
const juce::Colour bg(0xff0e1013), panel(0xff171a1f), grid(0xff2a2f37), text(0xffe6e8eb), dim(0xff8a919c),
                   cyan(0xff2fd4e0), amber(0xffffb020), red(0xffff5a5a);
}

// Audio engine: generator out + capture of mic/ref into lock-free rings. RT-safe callback.
class AudioEngine : public juce::AudioIODeviceCallback {
public:
    enum Gen { Off = 0, Pink = 1, Sweep = 2 };
    std::atomic<int> gen{Off}, micCh{0}, refCh{1}, outA{0}, outB{1};
    std::atomic<float> genGainDb{-20.f};
    std::atomic<float> micPeak{0}, refPeak{0};
    foh::RingBuffer micRing{1 << 19}, refRing{1 << 19};
    double sampleRate = 48000;
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override {}
private:
    foh::PinkNoise pink; foh::LogSweep sweep; float lastGain = -999;
    float scratch[4096] {};
};

class PlotComponent : public juce::Component {
public:
    struct Series { std::vector<double> f, v; juce::Colour c; bool dashed = false; float width = 1.5f; };
    juce::String yLabel; double yMin = -12, yMax = 12; bool logY = false; double fMin = 20, fMax = 20000;
    std::vector<Series> series; std::vector<std::pair<double,double>> shade; // freq bands to shade
    juce::String note;
    void paint(juce::Graphics&) override;
    float xOf(double f) const; float yOf(double v) const;
    bool bars = false; std::vector<double> barLo, barHi; // for RTA
};

class MainComponent : public juce::Component, private juce::Timer {
public:
    MainComponent(); ~MainComponent() override;
    void paint(juce::Graphics&) override; void resized() override;
private:
    void timerCallback() override;
    void showPage(int p); void updateTF(); void updateRTA(); void findDelay(); void storeTrace(const juce::String& name);
    void refreshTraceList(); void loadCal(); void layoutGuide(); void guideAction(int which);
    juce::String explain() const;

    juce::AudioDeviceManager dm; AudioEngine engine;
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    // analysis
    std::unique_ptr<foh::TransferFunction> tf; std::unique_ptr<foh::RTA> rta;
    std::vector<float> micHist, refHist; int histPos = 0; // last ~2 s for delay finder
    foh::TraceStore traces; foh::MicCal cal; bool calOn = false; foh::DelayResult lastDelay; foh::TFResult lastTF;
    bool paused = false; double analysisRate = 48000;
    // nav
    juce::OwnedArray<juce::TextButton> nav; int page = 0; juce::Label status;
    // setup page
    juce::ComboBox micBox, refBox, outABox, outBBox, genBox; juce::Slider genGain; juce::TextButton calBtn {"Load mic cal..."}, calToggle {"Cal ON"};
    juce::Label setupHelp, calLabel, levelLabel;
    // TF page
    PlotComponent magPlot, phasePlot, cohPlot; juce::ComboBox smoothBox, avgBox; juce::TextButton storeBtn {"Store"}, findBtn {"Find delay"}, clearAvgBtn {"Reset avg"}, pauseBtn {"Pause"};
    juce::Label delayLabel; juce::TextButton insertBtn {"Insert delay"}; juce::ListBox traceList; juce::TextButton delTraceBtn {"Delete trace"};
    struct TraceModel : juce::ListBoxModel { MainComponent* o = nullptr; int getNumRows() override; void paintListBoxItem(int, juce::Graphics&, int, int, bool) override; void listBoxItemClicked(int, const juce::MouseEvent&) override; } traceModel;
    // RTA page
    PlotComponent rtaPlot; juce::ComboBox rtaFracBox;
    // Guides page
    juce::ComboBox guideBox; juce::Label guideTitle, guideBody, guideResult; juce::TextButton guidePrev {"< Back"}, guideNext {"Next >"}, guideAct {"Do it"};
    int guide = 0, step = 0; double lockedDelayMs = 0;
    juce::Label help; // right "What am I looking at?" panel
    std::unique_ptr<juce::FileChooser> chooser;
};
