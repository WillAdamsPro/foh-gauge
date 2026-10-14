#pragma once
#include <fohcore/routing.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "fohcore/fohcore.h"
#include "ThemeBridge.h"
#include <array>
#include <cmath>

namespace th = foh::theme;

// All non-ASCII UI text is written as UTF-8 \x escapes and MUST go through u8():
// juce::String(const char*) treats bytes as ASCII/Latin-1, so symbols like \u2713 render as junk.
inline juce::String u8(const char* s) { return juce::String(juce::CharPointer_UTF8(s)); }

// Console display rounding; message thread only.
inline double consoleMs(double ms) { return std::round(ms * 100.0) / 100.0; }

// Audio engine: generator out + capture of mic/ref into lock-free rings. RT-safe callback.
class AudioEngine : public juce::AudioIODeviceCallback {
    int lastMicChannel = 0, lastRefChannel = 1, lastInputCount = -1;
    bool lastInputsValid = true;
public:
    AudioEngine(); // Synthesizes the immutable chime before the audio callback is registered.
    // Message-thread commands; one atomic packet keeps channel selection and request together.
    // Setup defaults to the console/PA output; chimes never mirror to outB.
    void playChime() noexcept { playChime(outA.load()); }
    void playChime(int output) noexcept;
    void testConnection(int output, int expectedInput) noexcept;
    void cancelConnectionTest() noexcept;
    foh::ConnectionResult connectionResult() const noexcept;
    enum Gen { Off = 0, Pink = 1, Sweep = 2 };
    std::atomic<int> gen{Off}, micCh{0}, refCh{1}, outA{0}, outB{1};
    std::atomic<float> genGainDb{-20.f};
    std::atomic<bool> monitoringSilent{false}; // W7: suppress all generated playback
    static_assert(std::atomic<int>::is_always_lock_free);
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    std::atomic<float> micPeak{0}, refPeak{0};
    // Device restart handshake: capture waits for the analysis consumer to discard
    // both old-rate queues and reset history before accepting new-rate samples.
    std::atomic<uint64_t> captureEpoch{0}, analysisEpoch{0};
    foh::RingBuffer micRing{1 << 19}, refRing{1 << 19};
    std::atomic<double> sampleRate{48000};
    static_assert(std::atomic<double>::is_always_lock_free);
    enum State { Ready, Stopped, InvalidInputs, InvalidOutputs, Overflow };
    std::atomic<int> state{Ready};
    std::atomic<bool> deviceFailed{false};
    std::atomic<int> captureMicChannel{0};
    const char* statusText() const noexcept;
    void audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                          const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* d) override;
    void audioDeviceStopped() override;
    void audioDeviceError(const juce::String&) override;
private:
    void resetPlayback() noexcept; // JUCE lifecycle callbacks, with I/O quiesced
    std::array<float, 28801> chime {}; // 0.6 seconds at 48 kHz, final sample is zero
    std::atomic<uint64_t> chimeRequest {0};
    uint64_t lastChimeRequest = 0;
    uint32_t requestSequence = 0; // message thread only
    std::atomic<uint64_t> testResult {0};
    foh::ConnectionDetector connectionDetector;
    int testSamplesRemaining = 0, chimeOutput = -1;
    double chimePosition = 28800;
    foh::PinkNoise pink; foh::LogSweep sweep; float lastGain = -999;
};

// Inter (OFL) bundled via BinaryData; falls back to system sans if missing.
class FohLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FohLookAndFeel();
    void applyTheme(); // re-read ColourIds from the current Theme
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawDocumentWindowTitleBar(juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX, int titleSpaceW, const juce::Image*, bool drawTitleTextOnLeft) override; // floating panel chrome (ws spec §5)
    static juce::Font font(float size, bool bold = false, bool semibold = false);
    static juce::Font numeric(float size, bool bold = true); // readouts: monospaced digits (no OpenType tnum in JUCE)
    // Release bundled typefaces with the UI, before JUCE tears down CoreText.
    juce::Typeface::Ptr regular, semibold, bold;
};

class PlotComponent : public juce::Component {
public:
    struct Series { std::vector<double> f, v; juce::Colour c; const float* dash = nullptr; float width = foh::ui::dim::trace(); double maxF = 1e9; juce::String label; std::vector<uint8_t> gap; }; // gap[i]: blanked bin -> break the trace
    struct Annotation { double lo, hi; juce::String label; };
    std::vector<Annotation> annotations; bool showLegend = false; // legend row: series with a label, 13 px, gap 18, padding 4/14
    juce::String yLabel; double yMin = -12, yMax = 12, yStep = 6; double fMin = 20, fMax = 20000; bool wrapPhase = false;
    struct ToleranceBand { std::vector<double> f, lower, upper; juce::Colour colour; };
    std::vector<ToleranceBand> toleranceBands;
    std::vector<Series> series; std::vector<std::pair<double,double>> shade; juce::String shadeLabel; juce::String note, centreNote;
    bool bars = false; std::vector<double> barLo, barHi;
    std::vector<std::pair<double,double>> lowCoh; // hatched low-coherence regions (Hz)
    bool showLowCohCallout = false; // A2 low-coherence callout over the widest hatched band
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

// Helper ("What am I looking at?") panel. Docked in the helper column, or, below 1360 px wide,
// collapsed to a 48 px "?" rail that opens it as an overlay (layout-spec section 5).
class HelpPanel : public juce::Component {
public:
    // A2 inspector: "INSPECTOR" caption, then sections (heading + body) separated by hairlines.
    // setText() splits copy on blank lines; a paragraph starting "Heading: ..." (short heading) becomes its own section,
    // the first paragraph goes under "What am I looking at?".
    HelpPanel() { addChildComponent(body); }
    void paint(juce::Graphics&) override;
    void setText(const juce::String& copy);
    juce::Label body; bool overlay = false; juce::String caption {"INSPECTOR"};
    std::vector<std::pair<juce::String, juce::String>> sections;
};

// Small macOS-style segmented control (rounded track, raised selected segment, hairline separators).
class SegmentedControl : public juce::Component {
public:
    explicit SegmentedControl(juce::StringArray items) : labels(std::move(items)) {}
    std::function<void(int)> onChange;
    juce::BigInteger disabled; // segments that cannot be picked (e.g. SPL not built yet)
    void setSelected(int i, bool notify = false) { if (i >= 0 && disabled[i]) return; if (i == sel) return; sel = i; repaint(); if (notify && onChange) onChange(sel); }
    int getSelected() const { return sel; }
    void setLabels(juce::StringArray l) { if (l != labels) { labels = std::move(l); repaint(); } }
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override { if (labels.isEmpty()) return; setSelected(juce::jlimit(0, labels.size() - 1, e.x * labels.size() / juce::jmax(1, getWidth())), true); }
private:
    juce::StringArray labels; int sel = 0;
};

// Sidebar item with a hand-drawn 16 px line glyph (SF-Symbol-like), flat source-list styling.
class NavButton : public juce::Button {
public:
    enum Glyph { Home, Transfer, Rta, Guides };
    NavButton(const juce::String& name, Glyph gl) : juce::Button(name), glyph(gl) {}
    void paintButton(juce::Graphics&, bool over, bool down) override;
    static juce::Path glyphPath(Glyph, juce::Rectangle<float>);
private:
    Glyph glyph;
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

// shared drawing helpers
void makePrimary(juce::TextButton& b);
void card(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour border);
void card(juce::Graphics& g, juce::Rectangle<int> r);
void logDecimate(const foh::TFResult& r, const std::vector<double>& v, std::vector<double>& fo, std::vector<double>& vo, std::vector<uint8_t>* gap = nullptr);
