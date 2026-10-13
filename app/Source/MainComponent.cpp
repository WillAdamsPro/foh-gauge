#include "MainComponent.h"
#if __has_include("BinaryData.h")
 #include "BinaryData.h"
 #define FOH_HAS_FONTS 1
#endif
using namespace juce;
namespace C = foh::theme::colour;
namespace T = foh::theme::type;

// ===================== Look and feel =====================
Typeface::Ptr FohLookAndFeel::regular, FohLookAndFeel::semibold, FohLookAndFeel::bold;
FohLookAndFeel::FohLookAndFeel() {
#ifdef FOH_HAS_FONTS
    if (!regular) {
        regular  = Typeface::createSystemTypefaceFor(BinaryData::InterRegular_ttf, BinaryData::InterRegular_ttfSize);
        semibold = Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf, BinaryData::InterSemiBold_ttfSize);
        bold     = Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf, BinaryData::InterBold_ttfSize);
    }
#endif
    setColour(ResizableWindow::backgroundColourId, C::background);
    setColour(Label::textColourId, C::textPrimary);
    setColour(TextButton::buttonColourId, C::pillBackground); setColour(TextButton::textColourOffId, C::textBody);
    setColour(TextButton::buttonOnColourId, C::navItemActiveBg); setColour(TextButton::textColourOnId, C::textStrong);
    setColour(ComboBox::backgroundColourId, C::pillBackground); setColour(ComboBox::outlineColourId, C::pillBorder); setColour(ComboBox::textColourId, C::textBody);
    setColour(PopupMenu::backgroundColourId, C::cardBackground); setColour(ListBox::backgroundColourId, C::cardBackground);
    setColour(Slider::thumbColourId, C::accent); setColour(Slider::trackColourId, C::divider);
}
Typeface::Ptr FohLookAndFeel::getTypefaceForFont(const Font& f) {
    if (regular != nullptr && f.getTypefaceName() == Font::getDefaultSansSerifFontName()) {
        auto st = f.getTypefaceStyle();
        if (st.containsIgnoreCase("Bold")) return bold;
        if (st.containsIgnoreCase("SemiBold")) return semibold;
        return regular;
    }
    return LookAndFeel_V4::getTypefaceForFont(f);
}
Font FohLookAndFeel::font(float size, bool b, bool sb) {
    return Font(FontOptions(size).withStyle(b ? "Bold" : sb ? "SemiBold" : "Regular"));
}
void FohLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour& bgc, bool over, bool down) {
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    bool primary = b.getProperties()["primary"];
    if (primary) { g.setColour(C::accent.brighter(over ? 0.1f : 0.f).darker(down ? 0.2f : 0.f)); g.fillRoundedRectangle(r, th::radius::button); return; }
    g.setColour(b.getToggleState() ? C::navItemActiveBg : bgc.brighter(over ? 0.05f : 0.f));
    g.fillRoundedRectangle(r, th::radius::button);
    g.setColour(C::ghostBorder); g.drawRoundedRectangle(r, th::radius::button, th::stroke::hairline);
}
static void makePrimary(TextButton& b) { b.getProperties().set("primary", true); b.setColour(TextButton::textColourOffId, C::textOnAccent); }

// ===================== Plot =====================
float PlotComponent::xOf(double f) const { return (float) (plotX() + plotW() * std::log10(f / fMin) / std::log10(fMax / fMin)); }
float PlotComponent::yOf(double v) const { return (float) (plotY() + plotH() * (yMax - jlimit(yMin, yMax, v)) / (yMax - yMin)); }
void PlotComponent::paint(Graphics& g) {
    auto pr = Rectangle<float>((float) plotX(), (float) plotY(), (float) plotW(), (float) plotH());
    g.setFont(FohLookAndFeel::font(T::axisTick));
    for (auto& s : shade) { float a = xOf(s.first), b = xOf(s.second); g.setColour(C::accent.withAlpha(C::crossoverBandAlpha)); g.fillRect(a, pr.getY(), b - a, pr.getHeight());
        if (shadeLabel.isNotEmpty()) { g.setColour(C::accent); g.setFont(FohLookAndFeel::font(T::label)); g.drawText(shadeLabel, (int) a - 40, (int) pr.getY() + 10, (int) (b - a) + 80, 14, Justification::centred); g.setFont(FohLookAndFeel::font(T::axisTick)); } }
    g.setColour(C::border);
    for (double f : {20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) { if (f < fMin || f > fMax) continue;
        float x = xOf(f); g.drawVerticalLine((int) x, pr.getY(), pr.getBottom());
        g.setColour(C::textAxis); g.drawText(f >= 1000 ? String((int) (f / 1000)) + "k" : String((int) f), (int) x - 15, (int) pr.getBottom() + 4, 30, 14, Justification::centred); g.setColour(C::border); }
    for (double v = yMin; v <= yMax + 1e-9; v += yStep) { float y = yOf(v); g.drawHorizontalLine((int) y, pr.getX(), pr.getRight());
        g.setColour(C::textAxis); g.drawText(yStep < 1 ? String(v, 1) : String((int) v), 0, (int) y - 7, plotX() - 6, 14, Justification::right); g.setColour(C::border); }
    if (note.isNotEmpty()) { g.setColour(C::textAxis); g.drawText(note, (int) pr.getRight() - 300, (int) pr.getY() + 2, 296, 14, Justification::right); }
    if (centreNote.isNotEmpty()) { g.setColour(C::textAxis); g.setFont(FohLookAndFeel::font(T::small)); g.drawText(centreNote, (int) xOf(1500) - 120, (int) pr.getCentreY() - 8, 240, 16, Justification::centred); }
    Graphics::ScopedSaveState ss(g); g.reduceClipRegion(pr.toNearestInt());
    if (bars) for (auto& s : series) { if (s.dash) continue; g.setColour(C::rtaBar);
        for (size_t i = 0; i < s.v.size() && i < barLo.size(); ++i) { float a = xOf(barLo[i]) + 1, b = xOf(barHi[i]) - 1, y = yOf(s.v[i]); g.fillRect(a, y, std::max(1.f, b - a), pr.getBottom() - y); }
        Path p; for (size_t i = 0; i < s.v.size(); ++i) { float x = xOf(s.f[i]), y = yOf(s.v[i]); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); }
        g.setColour(s.c); g.strokePath(p, PathStrokeType(th::stroke::traceSecondary, PathStrokeType::curved, PathStrokeType::rounded)); }
    for (auto& s : series) {
        if (bars && !s.dash) continue;
        Path p; bool started = false; double prev = 0;
        for (size_t i = 0; i < s.f.size(); ++i) { double f = s.f[i]; if (f < fMin || f > fMax || f > s.maxF) continue;
            float x = xOf(f), y = yOf(s.v[i]);
            bool jump = started && wrapPhase && std::abs(s.v[i] - prev) > 180;
            if (!started || jump) { p.startNewSubPath(x, y); started = true; } else p.lineTo(x, y); prev = s.v[i]; }
        g.setColour(s.c); PathStrokeType st(s.width, PathStrokeType::curved, PathStrokeType::rounded);
        if (s.dash) { Path d; st.createDashedStroke(d, p, s.dash, 2); g.fillPath(d); } else g.strokePath(p, st);
    }
}
static void logDecimate(const foh::TFResult& r, const std::vector<double>& v, std::vector<double>& fo, std::vector<double>& vo) {
    fo.clear(); vo.clear(); if (r.fftSize == 0) return; double bin = r.sampleRate / r.fftSize; size_t last = 0;
    for (double f = 18; f < 22000; f *= std::pow(2.0, 1.0 / 30)) { size_t k = (size_t) std::lround(f / bin); if (k == last || k >= v.size()) continue; last = k; fo.push_back(k * bin); vo.push_back(v[k]); }
} // ~300 log-spaced points (spec)

// ===================== Guide content =====================
// chart: 0 none, 1 live mag (W2 mains), 2 stored mains + live sub, 3 phase to 400 Hz, 4 phase+sum, 5 done, 6 live TF mag full range
struct Step { const char* progress; const char* title; const char* body; const char* action; int act; int chart; };
static const std::vector<std::vector<Step>> kGuides = {
 { {"Connect", "Connect your gear", "UMC404HD: Out 1 to the console. Out 2 to In 2 with a 1/4\" TRS cable (reference loopback; TRS keeps it off phantom). ECM8000 to In 1 (XLR, +48 V on). Pick the device and channels on Setup at 48 kHz.", "Open Setup", 1, 0},
   {"Signal", "Signal check", "Start pink noise and bring the console up to a comfortable level. Both inputs should sit near -20 dBFS (top bar), never clipping. Mic 1-3 m on-axis from ONE main, ear height, away from walls.", "Start pink noise", 2, 6},
   {"Delay", "Find the reference delay", "The tallest spike is the direct sound; smaller later spikes are reflections. If two spikes are similar, check only one speaker is on.", "Find + lock delay", 3, 6},
   {"Check", "Check the trace", "Phase should be fairly flat and coherence high (above ~0.8) above 200 Hz. Low coherence = don't trust that region.", "Open transfer function", 4, 6},
   {"Save", "Save Position 1", "Store this trace so you can compare later. If you move the mic, find the delay again.", "Store 'Position 1'", 5, 6} },
 { {"Measure mains", "Measure the mains alone", "Mute the subs. Mic where sub and mains are about equally loud (often FOH). Pink noise on. This finds and locks the delay to the mains, then stores 'Main'.", "Capture mains \xe2\x97\x8f", 12, 1},
   {"Measure sub", "Measure the sub alone", "Mute the mains, unmute the subs. Don't find the delay again: the comparison depends on the same timing. The dashed line is your stored mains.", "Capture sub \xe2\x97\x8f", 7, 2},
   {"Find delay", "Find the delay", "Compare phase in the shaded crossover band (phase above 400 Hz is hidden). Judge polarity from these traces, not the impulse. Prefer the option where slopes match across the band and that needs the least delay.", "Use this delay", 8, 3},
   {"Apply & verify", "Apply and verify", "Enter the change in your DSP and unmute both. The sum (white) should RISE in the crossover band, up to ~+6 dB where the two are equal, not dip. Walk a few seats.", "Capture both \xe2\x97\x8f", 9, 4},
   {"Done", "Sub and mains aligned", "", "Measure again", 13, 5} },
 { {"Mains", "Measure the mains", "Mains already tuned. Mic in the seam: where main and fill are about equally loud, inside the fill's area. Fill muted. This finds and locks the delay to the mains.", "Find + lock delay", 3, 6},
   {"Fill", "Measure the fill", "Mute the mains, unmute the fill. The app finds the fill's arrival and compares it with the locked mains time.", "Find fill arrival", 10, 6},
   {"Set delay", "Set the fill delay", "Delay the fill by the amount shown. Many engineers add a few ms more so the mains arrive first and the image stays on stage (rule of thumb; trust your ears). Fill only as loud as needed.", "Open transfer function", 4, 6},
   {"Verify", "Verify both on", "Unmute both. Mute/unmute the fill and watch which spike moves. Phase should overlap in the mid/HF where they overlap. Walk from mains-only to fill-only zones and listen.", "Store 'Main+Fill'", 11, 6} },
};
static const char* kGuideNames[] = {"Setup + reference delay", "Align sub to mains", "Align fills to mains"};

// ===================== Main component =====================
MainComponent::MainComponent() {
    setLookAndFeel(&lnf);
    dm.initialiseWithDefaultDevices(4, 4);
    { auto s = dm.getAudioDeviceSetup(); s.sampleRate = 48000; dm.setAudioDeviceSetup(s, true); }
    dm.addAudioCallback(&engine);
    selector = std::make_unique<AudioDeviceSelectorComponent>(dm, 1, 4, 1, 4, false, false, false, false);
    addChildComponent(*selector);
    tf = std::make_unique<foh::TransferFunction>(16384, 48000.0); rta = std::make_unique<foh::RTA>(16384, 48000.0, 3);
    micHist.assign(96000, 0.f); refHist.assign(96000, 0.f);

    const char* pages[] = {"Home / Setup", "Transfer Function", "RTA / Spectrum", "Guides"};
    for (int i = 0; i < 4; ++i) { auto* b = nav.add(new TextButton(pages[i])); b->onClick = [this, i] { showPage(i); }; addAndMakeVisible(b); }
    addAndMakeVisible(status); status.setFont(FohLookAndFeel::font(T::small)); status.setColour(Label::textColourId, C::textBody);
    addAndMakeVisible(pauseBtn); pauseBtn.setClickingTogglesState(true); pauseBtn.onClick = [this] { paused = pauseBtn.getToggleState(); pauseBtn.setButtonText(paused ? "Resume" : "Pause"); };
    addAndMakeVisible(help); help.setJustificationType(Justification::topLeft); help.setColour(Label::textColourId, C::textBody); help.setFont(FohLookAndFeel::font(T::body));
    addAndMakeVisible(pageTitle); pageTitle.setFont(FohLookAndFeel::font(T::title, true));

    auto chanItems = [](ComboBox& b, const char* pre) { for (int i = 1; i <= 4; ++i) b.addItem(String(pre) + String(i), i); };
    chanItems(micBox, "Mic: In "); chanItems(refBox, "Ref: In "); chanItems(outABox, "To console: Out "); chanItems(outBBox, "Loopback: Out ");
    for (auto* b : {&micBox, &refBox, &outABox, &outBBox}) addChildComponent(*b);
    micBox.setSelectedId(1); refBox.setSelectedId(2); outABox.setSelectedId(1); outBBox.setSelectedId(2);
    micBox.onChange = [this] { engine.micCh = micBox.getSelectedId() - 1; };
    refBox.onChange = [this] { engine.refCh = refBox.getSelectedId() - 1; };
    outABox.onChange = [this] { engine.outA = outABox.getSelectedId() - 1; };
    outBBox.onChange = [this] { engine.outB = outBBox.getSelectedId() - 1; };
    genBox.addItem("Generator off", 1); genBox.addItem("Pink noise", 2); genBox.addItem("Log sweep 20 Hz-20 kHz (5 s, loops)", 3); genBox.setSelectedId(1);
    genBox.onChange = [this] { engine.gen = genBox.getSelectedId() - 1; }; addChildComponent(genBox);
    genGain.setRange(-60, 0, 0.5); genGain.setValue(-20); genGain.setTextValueSuffix(" dBFS"); genGain.onValueChange = [this] { engine.genGainDb = (float) genGain.getValue(); }; addChildComponent(genGain);
    calBtn.onClick = [this] { loadCal(); }; addChildComponent(calBtn);
    calToggle.setClickingTogglesState(true); calToggle.onClick = [this] { calOn = calToggle.getToggleState() && cal.ok(); }; addChildComponent(calToggle);
    for (auto* l : {&setupHelp, &calLabel, &levelLabel}) { l->setFont(FohLookAndFeel::font(T::body)); addChildComponent(*l); }
    setupHelp.setText("ROUTING (UMC404HD defaults)", dontSendNotification); setupHelp.setColour(Label::textColourId, C::textSecondary);
    calLabel.setText("No mic cal loaded", dontSendNotification);

    magPlot.yLabel = "MAGNITUDE DB"; magPlot.yMin = -12; magPlot.yMax = 12; magPlot.yStep = 6;
    phasePlot.yLabel = "PHASE \xc2\xb0"; phasePlot.yMin = -180; phasePlot.yMax = 180; phasePlot.yStep = 90; phasePlot.wrapPhase = true;
    cohPlot.yLabel = "COHERENCE"; cohPlot.yMin = 0; cohPlot.yMax = 1; cohPlot.yStep = 0.5;
    for (auto* p : {&magPlot, &phasePlot, &cohPlot, &rtaPlot, &gPlotA, &gPlotB}) addChildComponent(*p);
    smoothBox.addItem("Smoothing off", 1); smoothBox.addItem("Smoothing 1/3", 4); smoothBox.addItem("Smoothing 1/6", 7); smoothBox.addItem("Smoothing 1/12", 13); smoothBox.addItem("Smoothing 1/24", 25); smoothBox.addItem("Smoothing 1/48", 49); smoothBox.setSelectedId(13);
    avgBox.addItem("Avg 4", 4); avgBox.addItem("Avg 8", 8); avgBox.addItem("Avg 16", 16); avgBox.addItem("Avg 32", 32); avgBox.addItem("Avg \xe2\x88\x9e", 1000); avgBox.setSelectedId(16);
    avgBox.onChange = [this] { int a = avgBox.getSelectedId(); tf->setAverages(a == 1000 ? 0 : a); };
    tf->setAverages(16);
    for (auto* c : std::initializer_list<Component*>{&smoothBox, &avgBox, &storeBtn, &findBtn, &clearAvgBtn, &delayCaption, &delayValue, &delayDetail, &insertBtn, &traceList, &delTraceBtn}) addChildComponent(c);
    makePrimary(insertBtn);
    storeBtn.onClick = [this] { storeTrace("Trace " + String(traces.count() + 1)); };
    findBtn.onClick = [this] { findDelay(); };
    insertBtn.onClick = [this] { if (!lastDelay.arrivals.empty()) { tf->setReferenceDelaySamples((int) std::lround(lastDelay.arrivals[0].delaySamples)); lockedDelayMs = lastDelay.arrivals[0].delayMs; } };
    clearAvgBtn.onClick = [this] { tf->reset(); rta->reset(); };
    delayCaption.setText("DELAY FINDER", dontSendNotification); delayCaption.setFont(FohLookAndFeel::font(T::label)); delayCaption.setColour(Label::textColourId, C::textSecondary);
    delayValue.setFont(FohLookAndFeel::font(T::valueDelay, true)); delayValue.setText("--", dontSendNotification);
    delayDetail.setFont(FohLookAndFeel::font(T::body)); delayDetail.setColour(Label::textColourId, C::textSecondary); delayDetail.setText("press Find again", dontSendNotification);
    traceModel.o = this; traceList.setModel(&traceModel); traceList.setRowHeight(22);
    delTraceBtn.onClick = [this] { traces.remove(traceList.getSelectedRow()); refreshTraceList(); };

    rtaPlot.yLabel = "dBFS"; rtaPlot.yMin = -100; rtaPlot.yMax = 0; rtaPlot.yStep = 20; rtaPlot.bars = true;
    rtaFracBox.addItem("1/3 octave", 3); rtaFracBox.addItem("1/6 octave", 6); rtaFracBox.setSelectedId(3);
    rtaFracBox.onChange = [this] { rta->setFraction(rtaFracBox.getSelectedId()); }; addChildComponent(rtaFracBox);

    for (int i = 0; i < 3; ++i) guideBox.addItem(String("W") + String(i + 1) + ": " + kGuideNames[i], i + 1);
    guideBox.setSelectedId(2, dontSendNotification); guideBox.onChange = [this] { guide = guideBox.getSelectedId() - 1; step = 0; guideResult.setText({}, dontSendNotification); layoutGuide(); };
    for (auto* c : std::initializer_list<Component*>{&guideBox, &guideStepLabel, &guideBody, &guideResult, &guideReadout, &guidePrev, &guideAct, &doneSummary, &doneActions, &doneAgain, &doneHome}) addChildComponent(c);
    makePrimary(guideAct);
    guideStepLabel.setFont(FohLookAndFeel::font(T::label)); guideStepLabel.setColour(Label::textColourId, C::accent);
    guideBody.setFont(FohLookAndFeel::font(T::logo)); guideBody.setJustificationType(Justification::centredLeft);
    guideResult.setFont(FohLookAndFeel::font(T::body)); guideResult.setJustificationType(Justification::topLeft); guideResult.setColour(Label::textColourId, C::textBody);
    guideReadout.setFont(FohLookAndFeel::font(T::valueMedium, true));
    doneSummary.setFont(FohLookAndFeel::font(T::logo)); doneSummary.setJustificationType(Justification::topLeft);
    doneActions.setFont(FohLookAndFeel::font(T::logo)); doneActions.setJustificationType(Justification::topLeft);
    doneActions.setText("1. Walk a few seats and listen.\n2. Save the DSP settings.\n3. Note the delay values in your log.", dontSendNotification);
    makePrimary(doneAgain); doneAgain.onClick = [this] { step = 0; layoutGuide(); }; doneHome.onClick = [this] { showPage(0); };
    guidePrev.onClick = [this] { if (step > 0) { --step; layoutGuide(); } };
    guideAct.onClick = [this] { guideAction(kGuides[(size_t) guide][(size_t) step].act); };

    setSize(th::frame::baseWidth, th::frame::baseHeight);
    showPage(0); startTimerHz(15);
    // Dev/screenshot aid: FOH_START_PAGE=0..3, FOH_GUIDE=0..2, FOH_STEP=n
    if (auto* e = std::getenv("FOH_START_PAGE")) { guide = std::getenv("FOH_GUIDE") ? std::atoi(std::getenv("FOH_GUIDE")) : 1; step = std::getenv("FOH_STEP") ? std::atoi(std::getenv("FOH_STEP")) : 0;
        guideBox.setSelectedId(guide + 1, dontSendNotification); showPage(jlimit(0, 3, std::atoi(e))); }
}
MainComponent::~MainComponent() { stopTimer(); dm.removeAudioCallback(&engine); traceList.setModel(nullptr); setLookAndFeel(nullptr); }

Rectangle<int> MainComponent::mainArea() const {
    return getLocalBounds().withTrimmedTop(th::frame::statusBarHeight).withTrimmedLeft(th::frame::navWidth).withTrimmedRight(th::frame::helperWidth).reduced(th::frame::mainPadding);
}

static void card(Graphics& g, Rectangle<int> r, Colour border = C::border) {
    g.setColour(C::cardBackground); g.fillRoundedRectangle(r.toFloat(), th::radius::card);
    g.setColour(border); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), th::radius::card, th::stroke::hairline);
}

void MainComponent::paint(Graphics& g) {
    using namespace th::frame;
    g.fillAll(C::background);
    g.setColour(C::statusBarBackground); g.fillRect(0, 0, getWidth(), statusBarHeight);
    g.setColour(C::navBackground); g.fillRect(0, statusBarHeight, navWidth, getHeight());
    g.setColour(C::helperBackground); g.fillRect(getWidth() - helperWidth, statusBarHeight, helperWidth, getHeight());
    g.setColour(C::border); g.drawHorizontalLine(statusBarHeight - 1, 0, (float) getWidth());
    g.drawVerticalLine(navWidth - 1, (float) statusBarHeight, (float) getHeight()); g.drawVerticalLine(getWidth() - helperWidth, (float) statusBarHeight, (float) getHeight());
    g.setFont(FohLookAndFeel::font(T::logo, true)); g.setColour(C::textPrimary); g.drawText("FOH", 16, 0, 40, statusBarHeight, Justification::centredLeft);
    g.setColour(C::accent); g.drawText("Gauge", 52, 0, 80, statusBarHeight, Justification::centredLeft);
    g.setFont(FohLookAndFeel::font(T::small, false, true)); g.drawText("WHAT AM I LOOKING AT?", getWidth() - helperWidth + 20, statusBarHeight + 20, 280, 16, Justification::left);
    if (page >= 0 && page < nav.size()) { auto b = nav[page]->getBounds(); g.setColour(C::accent); g.fillRect(b.getX(), b.getY() + 6, (int) th::stroke::navActiveBar, b.getHeight() - 12); }
    if (page == 1) { card(g, tfCard); card(g, delayCard, C::cardBorderEmphasis);
        g.setFont(FohLookAndFeel::font(T::label)); g.setColour(C::textSecondary);
        for (auto* p : {&magPlot, &phasePlot, &cohPlot}) { auto b = p->getBounds(); Graphics::ScopedSaveState s(g);
            g.addTransform(AffineTransform::rotation(-MathConstants<float>::halfPi, (float) b.getX() - 14, (float) b.getCentreY()));
            g.drawText(p->yLabel, b.getX() - 14 - b.getHeight() / 2, b.getCentreY() - 8, b.getHeight(), 16, Justification::centred); } }
    if (page == 2) card(g, rtaPlot.getBounds().expanded(8));
    if (page == 3) {
        auto& steps = kGuides[(size_t) guide]; int n = (int) steps.size(), gap = 8; float w = (gProgress.getWidth() - gap * (n - 1)) / (float) n;
        for (int i = 0; i < n; ++i) { float x = gProgress.getX() + i * (w + gap);
            Colour bar = i < step ? C::ok : i == step ? C::accent : C::ghostBorder, lab = i < step ? C::ok : i == step ? C::textStrong : C::textAxis;
            g.setColour(bar); g.fillRoundedRectangle(x, (float) gProgress.getY(), w, 6.f, th::radius::progress);
            g.setColour(lab); g.setFont(FohLookAndFeel::font(T::small)); g.drawText(String(i < step ? "\xe2\x9c\x93 " : "") + String(i + 1) + ". " + steps[(size_t) i].progress, (int) x, gProgress.getY() + 14, (int) w, 16, Justification::left); }
        if (!gChartCard.isEmpty()) card(g, gChartCard);
        if (!gActionCard.isEmpty()) card(g, gActionCard, C::cardBorderEmphasis);
        if (!gDoneA.isEmpty()) { card(g, gDoneA); card(g, gDoneB);
            auto c = Rectangle<float>((float) gDoneA.getX() + 22, (float) gDoneA.getY() + 22, 56, 56); g.setColour(C::successBadgeBg); g.fillEllipse(c);
            g.setColour(C::ok); g.setFont(FohLookAndFeel::font(T::icon, true)); g.drawText("\xe2\x9c\x93", c, Justification::centred); }
    }
}

void MainComponent::resized() {
    using namespace th::frame;
    auto nr = Rectangle<int>(10, statusBarHeight + 16, navWidth - 20, 44);
    for (auto* b : nav) { b->setBounds(nr); nr.translate(0, 48); }
    status.setBounds(200, 0, getWidth() - 200 - 140, statusBarHeight);
    pauseBtn.setBounds(getWidth() - 120, 10, 100, 35);
    help.setBounds(getWidth() - helperWidth + 14, statusBarHeight + 44, helperWidth - 34, getHeight() - statusBarHeight - 60);
    auto c = mainArea();
    auto header = c.removeFromTop(29); pageTitle.setBounds(header.removeFromLeft(400)); c.removeFromTop(12);
    // header pills (TF)
    { auto h = header; storeBtn.setBounds(h.removeFromRight(80)); h.removeFromRight(8); clearAvgBtn.setBounds(h.removeFromRight(100)); h.removeFromRight(8);
      avgBox.setBounds(h.removeFromRight(110)); h.removeFromRight(8); smoothBox.setBounds(h.removeFromRight(150)); rtaFracBox.setBounds(header.removeFromRight(160)); guideBox.setBounds(Rectangle<int>(header.getRight() - 300, header.getY(), 300, 29)); }
    // setup
    { auto s = c; selector->setBounds(s.removeFromTop(330)); setupHelp.setBounds(s.removeFromTop(24));
      auto row = s.removeFromTop(30); int w = row.getWidth() / 4; for (auto* b : {&micBox, &refBox, &outABox, &outBBox}) b->setBounds(row.removeFromLeft(w).reduced(4, 0));
      s.removeFromTop(12); row = s.removeFromTop(30); genBox.setBounds(row.removeFromLeft(300).reduced(4, 0)); genGain.setBounds(row.removeFromLeft(360).reduced(8, 0));
      s.removeFromTop(12); row = s.removeFromTop(30); calBtn.setBounds(row.removeFromLeft(150).reduced(4, 0)); calToggle.setBounds(row.removeFromLeft(90).reduced(4, 0)); calLabel.setBounds(row);
      s.removeFromTop(12); levelLabel.setBounds(s.removeFromTop(30)); }
    // TF: card, then delay bar 63 h, then trace list
    { auto s = c; auto traceArea = s.removeFromBottom(96); s.removeFromBottom(10);
      delayCard = s.removeFromBottom(63); s.removeFromBottom(10); tfCard = s;
      auto in = tfCard.reduced(8).withTrimmedBottom(-4); in.removeFromLeft(26);
      int H = in.getHeight(); int ph = H - 3 * 24 - 8; // plot heights flex 190:150:90
      int mh = ph * 190 / 430 + 32, phh = ph * 150 / 430 + 32;
      magPlot.setBounds(in.removeFromTop(mh)); phasePlot.setBounds(in.removeFromTop(phh)); cohPlot.setBounds(in);
      auto d = delayCard.reduced(16, 12); insertBtn.setBounds(d.removeFromRight(118).withSizeKeepingCentre(118, 37)); d.removeFromRight(12);
      findBtn.setBounds(d.removeFromRight(103).withSizeKeepingCentre(103, 37));
      delayCaption.setBounds(d.removeFromLeft(110)); delayValue.setBounds(d.removeFromLeft(150)); delayDetail.setBounds(d);
      delTraceBtn.setBounds(traceArea.removeFromRight(120).removeFromTop(30)); traceList.setBounds(traceArea.withTrimmedRight(10)); }
    { auto s = c.reduced(8); rtaPlot.setBounds(s); }
    layoutGuide();
}

void MainComponent::showPage(int p) {
    page = p;
    for (int i = 0; i < nav.size(); ++i) { nav[i]->setToggleState(i == p, dontSendNotification); }
    std::vector<Component*> all {selector.get(), &micBox, &refBox, &outABox, &outBBox, &genBox, &genGain, &calBtn, &calToggle, &setupHelp, &calLabel, &levelLabel,
        &magPlot, &phasePlot, &cohPlot, &smoothBox, &avgBox, &storeBtn, &findBtn, &clearAvgBtn, &delayCaption, &delayValue, &delayDetail, &insertBtn, &traceList, &delTraceBtn,
        &rtaPlot, &rtaFracBox, &guideBox, &guideStepLabel, &guideBody, &guideResult, &guideReadout, &guidePrev, &guideAct, &gPlotA, &gPlotB, &doneSummary, &doneActions, &doneAgain, &doneHome};
    for (auto* x : all) x->setVisible(false);
    std::vector<Component*> on;
    if (p == 0) on = {selector.get(), &micBox, &refBox, &outABox, &outBBox, &genBox, &genGain, &calBtn, &calToggle, &setupHelp, &calLabel, &levelLabel};
    if (p == 1) on = {&magPlot, &phasePlot, &cohPlot, &smoothBox, &avgBox, &storeBtn, &findBtn, &clearAvgBtn, &delayCaption, &delayValue, &delayDetail, &insertBtn, &traceList, &delTraceBtn};
    if (p == 2) on = {&rtaPlot, &rtaFracBox};
    for (auto* x : on) x->setVisible(true);
    const char* titles[] = {"Home / Setup", "Transfer function", "RTA / Spectrum", ""};
    pageTitle.setText(titles[p], dontSendNotification); pageTitle.setVisible(p != 3);
    if (p == 3) layoutGuide();
    help.setText(explain(), dontSendNotification); repaint();
}

String MainComponent::explain() const {
    switch (page) {
        case 0: return "Pick your interface (UMC404HD) at 48 kHz.\n\nMic = In 1 (ECM8000, +48 V).\n\nReference = In 2: a 1/4\" TRS loopback from Out 2, so it carries exactly what goes to the system.\n\nOut 1 goes to the console. Both inputs should sit near -20 dBFS on pink noise, never clipping.\n\nLoad your mic cal file (CSV/TXT/FRD) and turn Cal ON.";
        case 1: return "This compares the console (reference) to the mic. Anything that isn't flat is what the speakers + room are doing to your mix.\n\nTop: Magnitude. 0 dB = unchanged.\n\nMiddle: Phase. Timing per frequency. A steady slope means leftover delay; flat is ideal.\n\nBottom: Coherence. How much to trust the trace. Low = probably a reflection or noise: don't EQ it.\n\nDelay finder lines up the reference with the mic. Stored traces are dashed.";
        case 2: return "The RTA shows the level in each 1/3 or 1/6 octave band at the mic.\n\nUse it to see what's loud in the room. To judge the system itself, prefer the transfer function: the RTA includes the spectrum of whatever is playing.";
        default: return "Guides walk you through one job at a time: read the step, press the one big button, check the result.\n\nThe app explains and suggests; you decide and make the change in your own gear.\n\nShaded band = acoustic crossover (where sub and mains are within 10 dB).";
    }
}

void MainComponent::timerCallback() {
    const double fs = engine.sampleRate.load();
    if (std::abs(fs - analysisRate) > 1) { analysisRate = fs; tf = std::make_unique<foh::TransferFunction>(16384, fs); rta = std::make_unique<foh::RTA>(16384, fs, rtaFracBox.getSelectedId());
        int a = avgBox.getSelectedId(); tf->setAverages(a == 1000 ? 0 : a); }
    static std::vector<float> m(16384), rr(16384);
    while (engine.micRing.available() >= 512 && engine.refRing.available() >= 512) {
        int n = std::min({engine.micRing.available(), engine.refRing.available(), 16384});
        engine.micRing.pop(m.data(), n); engine.refRing.pop(rr.data(), n);
        if (paused) continue;
        tf->process(rr.data(), m.data(), n); rta->process(m.data(), n);
        for (int i = 0; i < n; ++i) { micHist[(size_t) histPos] = m[(size_t) i]; refHist[(size_t) histPos] = rr[(size_t) i]; histPos = (histPos + 1) % (int) micHist.size(); }
    }
    float mp = engine.micPeak.exchange(0), rp = engine.refPeak.exchange(0);
    auto dbs = [](float p) { return p > 1e-6f ? String(20 * std::log10(p), 1) + " dBFS" : String("-inf"); };
    auto* dev = dm.getCurrentAudioDevice();
    status.setText(String(paused ? "\xe2\x96\xa0 Paused" : "\xe2\x97\x8f Measuring") + "     " + (dev ? dev->getName() + " \xc2\xb7 " + String((int) fs / 1000) + " kHz" : String("No device"))
                   + "     Mic " + dbs(mp) + "     Ref " + dbs(rp) + "     Cal " + String(calOn ? "\xe2\x9c\x93" : "off")
                   + "     Ref delay " + String(tf->referenceDelaySamples() / fs * 1000, 2) + " ms" + (mp > 0.99f || rp > 0.99f ? "     \xe2\x96\xb2 CLIPPING" : ""), dontSendNotification);
    if (page == 0) levelLabel.setText(String(mp > 0.01f ? "\xe2\x9c\x93 Mic signal     " : "\xe2\x96\xa0 No mic signal     ") + String(rp > 0.01f ? "\xe2\x9c\x93 Reference signal" : "\xe2\x96\xa0 No reference signal (check Out 2 -> In 2 loopback)"), dontSendNotification);
    if (page == 1 || page == 3) updateTF();
    if (page == 2) updateRTA();
    if (page == 3) updateGuidePlots();
}

void MainComponent::updateTF() {
    int sm = smoothBox.getSelectedId() - 1;
    lastTF = tf->result(sm); if (calOn) cal.apply(lastTF);
    if (page != 1) return;
    for (auto* p : {&magPlot, &phasePlot, &cohPlot}) p->series.clear();
    const Colour tc[] = {C::traceStored, C::warning, Colour(0xffa080ff), C::ok, Colour(0xffff7090), Colour(0xff70a0ff), Colour(0xffd0d060), Colour(0xffff9050)};
    for (int i = 0; i < traces.count(); ++i) { auto& t = traces.at(i); if (!t.visible) continue;
        PlotComponent::Series s; s.c = tc[i % 8]; s.dash = th::stroke::dashStored; s.width = th::stroke::traceStored;
        logDecimate(t.tf, t.tf.magDb, s.f, s.v); magPlot.series.push_back(s);
        logDecimate(t.tf, t.tf.phaseDeg, s.f, s.v); phasePlot.series.push_back(s); }
    PlotComponent::Series s; s.c = C::accent;
    logDecimate(lastTF, lastTF.magDb, s.f, s.v); magPlot.series.push_back(s);
    logDecimate(lastTF, lastTF.phaseDeg, s.f, s.v); phasePlot.series.push_back(s);
    s.c = C::traceCoherence; s.width = th::stroke::traceSecondary; logDecimate(lastTF, lastTF.coherence, s.f, s.v); cohPlot.series.push_back(s);
    magPlot.note = "averages " + String(lastTF.averages) + (calOn ? " \xc2\xb7 cal applied" : "");
    for (auto* p : {&magPlot, &phasePlot, &cohPlot}) p->repaint();
}

void MainComponent::updateGuidePlots() {
    auto& st = kGuides[(size_t) guide][(size_t) step];
    auto* main = findTrace("Main"); auto* sub = findTrace("Sub");
    auto add = [](PlotComponent& p, const foh::TFResult& r, const std::vector<double>& v, Colour c, const float* dash, double maxF = 1e9, float w = th::stroke::trace) {
        PlotComponent::Series s; s.c = c; s.dash = dash; s.maxF = maxF; s.width = w; logDecimate(r, v, s.f, s.v); p.series.push_back(s); };
    gPlotA.series.clear(); gPlotB.series.clear();
    std::vector<std::pair<double,double>> band; if (xover.found) band = {{xover.lo, xover.hi}};
    String bl = xover.found ? "crossover ~" + String((int) std::sqrt(xover.lo * xover.hi)) + " Hz" : String();
    for (auto* p : {&gPlotA, &gPlotB}) { p->shade = guide == 1 ? band : decltype(band){}; p->shadeLabel = bl; p->centreNote = {}; p->wrapPhase = false; }
    auto magSetup = [](PlotComponent& p, double lo, double hi) { p.yMin = lo; p.yMax = hi; p.yStep = 6; p.wrapPhase = false; };
    auto phSetup = [](PlotComponent& p) { p.yMin = -180; p.yMax = 180; p.yStep = 90; p.wrapPhase = true; };
    switch (st.chart) {
        case 1: magSetup(gPlotA, -24, 6); add(gPlotA, lastTF, lastTF.magDb, C::accent, nullptr); break;
        case 2: magSetup(gPlotA, -24, 6); if (main) add(gPlotA, main->tf, main->tf.magDb, C::traceStored, th::stroke::dashStored, 1e9, th::stroke::traceStored);
                add(gPlotA, lastTF, lastTF.magDb, C::warning, nullptr); break;
        case 3: phSetup(gPlotA); gPlotA.centreNote = "phase hidden above 400 Hz";
                if (main) add(gPlotA, main->tf, main->tf.phaseDeg, C::accent, nullptr, 400); if (sub) add(gPlotA, sub->tf, sub->tf.phaseDeg, C::warning, nullptr, 400); break;
        case 4: phSetup(gPlotA); gPlotA.centreNote = "phase hidden above 400 Hz"; magSetup(gPlotB, -24, 12);
                if (main) add(gPlotA, main->tf, main->tf.phaseDeg, C::accent, nullptr, 400);
                if (sub) add(gPlotA, sub->tf, sub->tf.phaseDeg, C::warning, th::stroke::dashLimit, 400);
                if (main) add(gPlotB, main->tf, main->tf.magDb, C::traceMainsMuted, th::stroke::dashComponent, 1e9, th::stroke::traceSecondary);
                if (sub) add(gPlotB, sub->tf, sub->tf.magDb, C::traceSubMuted, th::stroke::dashComponent, 1e9, th::stroke::traceSecondary);
                add(gPlotB, lastTF, lastTF.magDb, C::traceSum, nullptr); break;
        case 6: magSetup(gPlotA, -24, 12); add(gPlotA, lastTF, lastTF.magDb, C::accent, nullptr); break;
        default: break;
    }
    gPlotA.repaint(); gPlotB.repaint();
}

void MainComponent::updateRTA() {
    if (calOn) rta->setCalibrationDb(cal.correctionDb(8193, analysisRate / 16384)); else rta->setCalibrationDb({});
    auto b = rta->bandsDb(); rtaPlot.series.clear(); rtaPlot.barLo.clear(); rtaPlot.barHi.clear();
    PlotComponent::Series s; s.c = C::accent;
    for (size_t i = 0; i < b.size(); ++i) { auto& bd = rta->bands()[i]; rtaPlot.barLo.push_back(bd.fLo); rtaPlot.barHi.push_back(bd.fHi); s.f.push_back(bd.fCenter); s.v.push_back(b[i]); }
    rtaPlot.series.push_back(s); rtaPlot.note = calOn ? "cal applied" : ""; rtaPlot.repaint();
}

void MainComponent::findDelay() {
    const int L = 48000; std::vector<float> r((size_t) L), m((size_t) L); int H = (int) micHist.size();
    for (int i = 0; i < L; ++i) { int idx = (histPos - L + i + H) % H; r[(size_t) i] = refHist[(size_t) idx]; m[(size_t) i] = micHist[(size_t) idx]; }
    lastDelay = foh::findDelay(r, m, analysisRate, true);
    if (lastDelay.arrivals.empty()) { delayValue.setText("--", dontSendNotification); delayDetail.setText("no signal: is pink noise playing?", dontSendNotification); return; }
    auto& a0 = lastDelay.arrivals[0];
    delayValue.setText(String(a0.delayMs, 2) + " ms", dontSendNotification);
    String t = "\xe2\x89\x88 " + String(a0.distanceM, 2) + " m \xc2\xb7 confidence " + String(lastDelay.confidence > 0.7 ? "high" : lastDelay.confidence > 0.35 ? "medium" : "LOW");
    for (size_t i = 1; i < lastDelay.arrivals.size(); ++i) t << "  \xc2\xb7  #" << (int) i + 1 << " " << String(lastDelay.arrivals[i].delayMs, 2) << " ms (" << String(lastDelay.arrivals[i].relLevelDb, 1) << " dB)";
    if (lastDelay.arrivals.size() > 1 && lastDelay.arrivals[1].relLevelDb > -3) t << "\n\xe2\x96\xb2 Two similar spikes: is only one speaker on?";
    delayDetail.setText(t, dontSendNotification);
}

bool MainComponent::storeTrace(const String& name) {
    updateTF();
    bool ok = traces.store(name.toStdString(), lastTF, tf->referenceDelaySamples() / analysisRate * 1000);
    if (!ok) AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Trace store full", "Up to 8 traces. Delete one on the Transfer Function page first.");
    refreshTraceList(); return ok;
}
const foh::Trace* MainComponent::findTrace(const char* n) const { for (int i = traces.count() - 1; i >= 0; --i) if (traces.at(i).name == n) return &traces.at(i); return nullptr; }
void MainComponent::refreshTraceList() { traceList.updateContent(); traceList.repaint(); }
int MainComponent::TraceModel::getNumRows() { return o->traces.count(); }
void MainComponent::TraceModel::paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) {
    if (row >= o->traces.count()) return; auto& t = o->traces.at(row);
    if (sel) g.fillAll(C::navItemActiveBg);
    g.setColour(t.visible ? C::textPrimary : C::textAxis); g.setFont(FohLookAndFeel::font(T::small));
    g.drawText(String(t.visible ? "[x] " : "[ ] ") + String(t.name) + "  (ref delay " + String(t.refDelayMs, 2) + " ms)", 6, 0, w - 6, h, Justification::centredLeft);
}
void MainComponent::TraceModel::listBoxItemClicked(int row, const MouseEvent& e) {
    if (row < o->traces.count() && e.x < 30) { o->traces.at(row).visible = !o->traces.at(row).visible; o->refreshTraceList(); } }

void MainComponent::loadCal() {
    chooser = std::make_unique<FileChooser>("Mic calibration file", File(), "*.csv;*.txt;*.frd;*.cal");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
        auto f = fc.getResult(); if (!f.existsAsFile()) return;
        cal = foh::loadMicCal(f.getFullPathName().toStdString());
        if (cal.ok()) { calOn = true; calToggle.setToggleState(true, dontSendNotification);
            calLabel.setText("\xe2\x9c\x93 " + f.getFileName() + ": " + String((int) cal.hz.size()) + " points, " + String(cal.hz.front(), 0) + "-" + String(cal.hz.back(), 0) + " Hz" + (cal.hasPhase ? ", with phase" : ""), dontSendNotification); }
        else { calOn = false; calLabel.setText("\xe2\x96\xb2 Could not read cal: " + String(cal.error), dontSendNotification); }
    });
}

void MainComponent::layoutGuide() {
    gChartCard = gActionCard = gDoneA = gDoneB = {};
    if (page != 3) { repaint(); return; }
    auto& steps = kGuides[(size_t) guide]; auto& st = steps[(size_t) step];
    for (auto* x : std::initializer_list<Component*>{&guideBox, &guideStepLabel, &guideBody, &guideResult, &guidePrev, &guideAct}) x->setVisible(true);
    for (auto* x : std::initializer_list<Component*>{&gPlotA, &gPlotB, &guideReadout, &doneSummary, &doneActions, &doneAgain, &doneHome}) x->setVisible(false);
    auto c = mainArea();
    guideStepLabel.setText(("GUIDED \xc2\xb7 " + String(kGuideNames[guide]) + " \xc2\xb7 STEP " + String(step + 1) + " OF " + String((int) steps.size())).toUpperCase(), dontSendNotification);
    guideStepLabel.setBounds(c.removeFromTop(16)); c.removeFromTop(6);
    pageTitle.setVisible(true); pageTitle.setText(st.title, dontSendNotification); pageTitle.setBounds(c.removeFromTop(29)); c.removeFromTop(14);
    gProgress = c.removeFromTop(36); c.removeFromTop(16);
    if (st.chart == 5) { // Done
        gDoneA = c.removeFromLeft(c.getWidth() * 466 / 876).withHeight(437); c.removeFromLeft(14); gDoneB = c.withHeight(437);
        doneSummary.setText(w2Summary.isEmpty() ? String("Nothing measured yet in this guide.") : w2Summary, dontSendNotification);
        doneSummary.setBounds(gDoneA.reduced(22).withTrimmedTop(70)); doneActions.setBounds(gDoneB.reduced(22).removeFromTop(120));
        auto bc = gDoneB.reduced(22).withTrimmedTop(148); doneAgain.setBounds(bc.removeFromTop(68)); bc.removeFromTop(10); doneHome.setBounds(bc.removeFromTop(43));
        for (auto* x : std::initializer_list<Component*>{&doneSummary, &doneActions, &doneAgain, &doneHome}) x->setVisible(true);
        guideBody.setVisible(false); guidePrev.setVisible(false); guideAct.setVisible(false); guideResult.setVisible(false);
        repaint(); return;
    }
    int chartH = st.chart == 4 ? 352 + 30 : st.chart == 3 ? 300 + 24 + 50 : st.chart == 0 ? 0 : 330 + 24;
    if (st.chart != 0) {
        gChartCard = c.removeFromTop(chartH + 16); c.removeFromTop(14);
        auto in = gChartCard.reduced(8);
        if (st.chart == 4) { gPlotA.setBounds(in.removeFromTop(176)); gPlotB.setBounds(in.removeFromTop(176)); gPlotB.setVisible(true); }
        else if (st.chart == 3) { gPlotA.setBounds(in.removeFromTop(324)); guideReadout.setBounds(in.reduced(14, 4)); guideReadout.setVisible(true); }
        else gPlotA.setBounds(in.removeFromTop(354));
        gPlotA.setVisible(true);
    }
    gActionCard = c.removeFromTop(96);
    auto a = gActionCard.reduced(16);
    guideAct.setButtonText(st.action); int bw = std::max(175, guideAct.getBestWidthForHeight(48) + 52);
    guideAct.setBounds(a.removeFromRight(bw).withSizeKeepingCentre(bw, 48)); a.removeFromRight(18);
    guidePrev.setVisible(step > 0); if (step > 0) { guidePrev.setBounds(a.removeFromRight(87).withSizeKeepingCentre(87, 43)); a.removeFromRight(18); }
    guideBody.setText(st.body, dontSendNotification); guideBody.setBounds(a);
    c.removeFromTop(10); guideResult.setBounds(c);
    repaint();
}

void MainComponent::guideAction(int a) {
    String out; auto next = [this] { if (step + 1 < (int) kGuides[(size_t) guide].size()) ++step; };
    switch (a) {
        case 1: showPage(0); return;
        case 2: genBox.setSelectedId(2); out = "Pink noise on at " + String(genGain.getValue(), 1) + " dBFS. Watch the meters in the top bar."; next(); break;
        case 3: findDelay(); if (!lastDelay.arrivals.empty()) { tf->setReferenceDelaySamples((int) std::lround(lastDelay.arrivals[0].delaySamples)); lockedDelayMs = lastDelay.arrivals[0].delayMs;
                    out = "Locked reference delay " + String(lockedDelayMs, 2) + " ms. " + delayDetail.getText(); next(); }
                else out = "No signal found. Is pink noise playing and are both inputs showing level?"; break;
        case 4: showPage(1); return;
        case 5: if (storeTrace("Position 1")) out = "Stored 'Position 1'."; break;
        case 12: findDelay(); if (lastDelay.arrivals.empty()) { out = "No signal found. Is pink noise playing?"; break; }
                 tf->setReferenceDelaySamples((int) std::lround(lastDelay.arrivals[0].delaySamples)); lockedDelayMs = lastDelay.arrivals[0].delayMs;
                 out = "Locked delay " + String(lockedDelayMs, 2) + " ms. Averaging... capturing in 3 s."; guideResult.setText(out, dontSendNotification);
                 Timer::callAfterDelay(3000, [this] { if (storeTrace("Main")) { guideResult.setText("Stored 'Main' (ref delay " + String(lockedDelayMs, 2) + " ms).", dontSendNotification); ++step; layoutGuide(); } });
                 return;
        case 7: if (storeTrace("Sub")) { out = "Stored 'Sub' (same locked delay " + String(lockedDelayMs, 2) + " ms).";
                    auto* m = findTrace("Main"); auto* s = findTrace("Sub"); if (m && s) xover = foh::acousticCrossover(m->tf, s->tf, 10.0); next(); } break;
        case 8: { auto* m = findTrace("Main"); auto* s = findTrace("Sub"); if (!m || !s) { out = "Capture mains and sub first."; break; }
            xover = foh::acousticCrossover(m->tf, s->tf, 10.0); if (!xover.found) { out = "No overlap within 10 dB between 30-300 Hz. Check levels."; break; }
            auto po = foh::phaseOffset(m->tf, s->tf, xover.lo, xover.hi);
            double period = 1000.0 / po.centerHz, inv = foh::wrapDeg(po.meanDeg + 180.0), invMs = -inv / 360.0 / po.centerHz * 1000.0;
            String who = po.impliedMs > 0 ? "Mains" : "Sub";
            guideReadout.setText("Suggested: " + who + " +" + String(std::abs(po.impliedMs), 1) + " ms   \xc2\xb7  or flip sub polarity + " + (invMs > 0 ? "mains " : "sub ") + String(std::abs(invMs), 1) + " ms", dontSendNotification);
            out << "Crossover " << String(xover.lo, 0) << "-" << String(xover.hi, 0) << " Hz (centre ~" << String(po.centerHz, 0) << " Hz). Sub vs main phase " << String(po.meanDeg, 0) << "\xc2\xb0 = " << String(std::abs(po.impliedMs), 2) << " ms ("
                << (po.impliedMs > 0 ? "sub LATE" : "sub EARLY") << "). Other answers differ by whole cycles (" << String(period, 1) << " ms). Target: within ~60\xc2\xb0 and parallel across the band.";
            w2Summary = "Crossover: " + String(xover.lo, 0) + "-" + String(xover.hi, 0) + " Hz\nSuggested: " + who + " +" + String(std::abs(po.impliedMs), 1) + " ms\nAlternative: flip sub polarity, " + (invMs > 0 ? "mains " : "sub ") + String(std::abs(invMs), 1) + " ms\nReference delay: " + String(lockedDelayMs, 2) + " ms";
            next(); break; }
        case 9: { if (storeTrace("Both")) { auto* m = findTrace("Main"); auto* s = findTrace("Sub"); auto* b = findTrace("Both");
                    if (m && s && b && xover.found) { auto vs = foh::virtualSum(m->tf, s->tf); double gain = 0, ref = 0; int n = 0;
                        for (size_t k = 1; k < b->tf.freq.size(); ++k) if (b->tf.freq[k] >= xover.lo && b->tf.freq[k] <= xover.hi) { gain += b->tf.magDb[k] - std::max(m->tf.magDb[k], s->tf.magDb[k]); ref += vs.magDb[k] - std::max(m->tf.magDb[k], s->tf.magDb[k]); ++n; }
                        if (n) { gain /= n; ref /= n; w2Summary << "\nMeasured sum in crossover: " << String(gain, 1) << " dB vs louder of the two (virtual sum predicts " << String(ref, 1) << " dB)"; } }
                    out = "Stored 'Both'."; next(); } break; }
        case 10: { findDelay(); if (lastDelay.arrivals.empty()) { out = "No signal found."; break; }
            // findDelay works on the raw (uncompensated) reference, so arrivals are absolute
            double fillMs = lastDelay.arrivals[0].delayMs, diff = lockedDelayMs - fillMs;
            out << "Fill arrives at " << String(fillMs, 2) << " ms; mains at " << String(lockedDelayMs, 2) << " ms. "
                << (diff > 0 ? "Fill arrives " + String(diff, 2) + " ms BEFORE the mains here. Suggested: delay the fill by ~" + String(diff, 2) + " ms (optionally a few ms more)."
                             : "Fill already arrives after the mains (" + String(-diff, 2) + " ms). Check the seam position and which speaker is on.");
            next(); break; }
        case 11: if (storeTrace("Main+Fill")) out = "Stored 'Main+Fill'. Compare with 'Main' on the Transfer Function page."; break;
        case 13: step = 0; break;
        default: break;
    }
    guideResult.setText(out, dontSendNotification); layoutGuide();
}
