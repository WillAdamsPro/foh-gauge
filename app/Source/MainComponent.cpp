#include "MainComponent.h"
using namespace juce;

// ===================== Audio engine (real-time thread) =====================
void AudioEngine::audioDeviceAboutToStart(AudioIODevice* d) {
    sampleRate = d->getCurrentSampleRate();
    sweep.prepare(sampleRate, 20.0, 20000.0, 5.0, 1.0);
}
void AudioEngine::audioDeviceIOCallbackWithContext(const float* const* in, int nIn, float* const* out, int nOut, int n,
                                                   const AudioIODeviceCallbackContext&) {
    const float g = genGainDb.load(); if (g != lastGain) { pink.setGainDb(g + 20.f); sweep.setGainDb(g + 12.f); lastGain = g; }
    const int mode = gen.load(), oa = outA.load(), ob = outB.load();
    for (int c = 0; c < nOut; ++c) if (out[c]) FloatVectorOperations::clear(out[c], n);
    for (int i = 0; i < n; ++i) {
        float s = mode == Pink ? pink.next() : mode == Sweep ? sweep.next() : 0.f;
        if (oa >= 0 && oa < nOut && out[oa]) out[oa][i] = s;
        if (ob >= 0 && ob < nOut && ob != oa && out[ob]) out[ob][i] = s;
    }
    const int mc = micCh.load(), rc = refCh.load();
    for (int off = 0; off < n; off += 4096) {
        int m = std::min(4096, n - off);
        const float* mp = (mc < nIn && in[mc]) ? in[mc] + off : nullptr;
        const float* rp = (rc < nIn && in[rc]) ? in[rc] + off : nullptr;
        if (mp) micRing.push(mp, m); else { std::fill(scratch, scratch + m, 0.f); micRing.push(scratch, m); }
        if (rp) refRing.push(rp, m); else { std::fill(scratch, scratch + m, 0.f); refRing.push(scratch, m); }
        if (mp) micPeak.store(std::max(micPeak.load(), FloatVectorOperations::findMaximum(mp, m) ));
        if (rp) refPeak.store(std::max(refPeak.load(), FloatVectorOperations::findMaximum(rp, m) ));
    }
}

// ===================== Plot =====================
float PlotComponent::xOf(double f) const { return (float) (std::log(f / fMin) / std::log(fMax / fMin) * (getWidth() - 50) + 45); }
float PlotComponent::yOf(double v) const { return (float) (getHeight() - 18 - (v - yMin) / (yMax - yMin) * (getHeight() - 26)); }
void PlotComponent::paint(Graphics& g) {
    g.fillAll(Col::panel); g.setFont(11.f);
    for (auto& s : shade) { g.setColour(Col::amber.withAlpha(0.15f)); float a = xOf(s.first), b = xOf(s.second); g.fillRect(a, 8.f, b - a, getHeight() - 26.f); }
    g.setColour(Col::grid);
    for (double f : {20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) { if (f < fMin || f > fMax) continue;
        float x = xOf(f); g.drawVerticalLine((int) x, 8.f, getHeight() - 18.f);
        g.setColour(Col::dim); g.drawText(f >= 1000 ? String(f / 1000, 0) + "k" : String((int) f), (int) x - 15, getHeight() - 16, 30, 14, Justification::centred); g.setColour(Col::grid); }
    for (int i = 0; i <= 4; ++i) { double v = yMin + (yMax - yMin) * i / 4; float y = yOf(v); g.drawHorizontalLine((int) y, 45.f, (float) getWidth() - 5);
        g.setColour(Col::dim); g.drawText(String(v, (yMax - yMin) < 3 ? 1 : 0), 0, (int) y - 7, 40, 14, Justification::right); g.setColour(Col::grid); }
    g.setColour(Col::dim); g.drawText(yLabel, 50, 8, 200, 14, Justification::left);
    if (note.isNotEmpty()) { g.setColour(Col::amber); g.drawText(note, getWidth() - 360, 8, 350, 14, Justification::right); }
    g.reduceClipRegion(45, 8, getWidth() - 50, getHeight() - 26);
    if (bars) {
        for (auto& s : series) { if (s.dashed) continue; g.setColour(s.c.withAlpha(0.8f));
            for (size_t i = 0; i < s.v.size() && i < barLo.size(); ++i) { float a = xOf(barLo[i]) + 1, b = xOf(barHi[i]) - 1, y = yOf(jlimit(yMin, yMax, s.v[i]));
                g.fillRect(a, y, std::max(1.f, b - a), yOf(yMin) - y); } }
    }
    for (auto& s : series) {
        if (bars && !s.dashed) continue;
        Path p; bool started = false; double prev = 0;
        for (size_t i = 0; i < s.f.size(); ++i) { double f = s.f[i]; if (f < fMin || f > fMax) continue;
            float x = xOf(f), y = yOf(jlimit(yMin, yMax, s.v[i]));
            bool jump = started && logY == false && yMax - yMin >= 300 && std::abs(s.v[i] - prev) > 180; // phase wrap
            if (!started || jump) { p.startNewSubPath(x, y); started = true; } else p.lineTo(x, y); prev = s.v[i]; }
        g.setColour(s.c);
        if (s.dashed) { Path d; float dl[] = {6.f, 4.f}; PathStrokeType(s.width).createDashedStroke(d, p, dl, 2); g.fillPath(d); }
        else g.strokePath(p, PathStrokeType(s.width));
    }
}

// Plot-friendly decimation: log-spaced points (~ 1/96 oct), keeps plots fast with 8 traces
static void logDecimate(const foh::TFResult& r, const std::vector<double>& v, std::vector<double>& fo, std::vector<double>& vo) {
    fo.clear(); vo.clear(); double bin = r.sampleRate / r.fftSize; size_t last = 0;
    for (double f = 18; f < 22000; f *= std::pow(2.0, 1.0 / 96)) { size_t k = (size_t) std::lround(f / bin); if (k == last || k >= v.size()) continue; last = k; fo.push_back(k * bin); vo.push_back(v[k]); }
}

// ===================== Guides content =====================
struct Step { const char* title; const char* body; const char* action; int act; };
static const std::vector<std::vector<Step>> kGuides = {
 { // W1
  {"1. Connect your gear", "UMC404HD: Out 1 -> console/system input. Out 2 -> 1/4\" TRS cable -> In 2 (reference loopback; TRS keeps it off phantom). ECM8000 -> In 1 (XLR, +48 V on). Pick the device and channels on the Setup page. Use 48 kHz.", "Open Setup", 1},
  {"2. Signal check", "Start pink noise. Raise the console until the speaker is at a comfortable level. Both inputs should sit around -20 dBFS, never clipping. Mic about 1-3 m on-axis from ONE main, ear height, away from walls. Only one speaker on.", "Start pink noise", 2},
  {"3. Find the reference delay", "The app compares the reference with the mic and finds when the speaker's sound arrives. The tallest spike is the direct sound; smaller later spikes are reflections. If two spikes are similar, check only one speaker is on.", "Find + lock delay", 3},
  {"4. Check the trace", "Look at Transfer Function. Phase should be fairly flat (no steep slope) and coherence high (above ~0.8) above 200 Hz. Low coherence = don't trust that region.", "Show transfer function", 4},
  {"5. Save Position 1", "Store this trace so you can compare later. If you move the mic, find the delay again.", "Store 'Position 1'", 5}},
 { // W2
  {"1. Mains only", "Mute the subs. Mic at a spot where sub and mains are about equally loud (often FOH). Pink noise on. Find and LOCK the delay to the mains.", "Find + lock delay", 3},
  {"2. Store Main", "Wait for the average to settle (a few seconds), then store.", "Store 'Main'", 6},
  {"3. Subs only", "Mute the mains, unmute the subs. Do NOT find the delay again - the comparison depends on the same timing.", "Store 'Sub'", 7},
  {"4. Compare phase in the crossover", "The shaded band is where the two are within 10 dB (the acoustic crossover). The app reads the phase gap there and suggests options. Judge polarity from the phase traces, not the impulse (too smeared at LF). Prefer the option with least delay where the slopes match across the band.", "Analyse Main vs Sub", 8},
  {"5. Verify both on", "Apply the change in your DSP. Unmute both. Store and compare against the virtual sum: the crossover region should RISE (up to ~+6 dB), not dip. Walk a few seats.", "Store 'Both' + compare", 9}},
 { // W3
  {"1. Mains only", "Mains are already tuned. Put the mic in the seam: where main and fill are about equally loud, inside the fill's coverage. Fill muted. Find and LOCK the delay.", "Find + lock delay", 3},
  {"2. Fill only", "Mute the mains, unmute the fill. The app finds the fill's arrival and compares it with the locked mains time.", "Find fill arrival", 10},
  {"3. Set the fill delay", "Delay the fill by the shown amount. Many engineers add a few ms more so the mains arrive first and the image stays on stage (rule of thumb - trust your ears). Keep fill level only as loud as needed.", "Show transfer function", 4},
  {"4. Verify both on", "Unmute both. Mute/unmute the fill and watch which spike moves. Phase should overlap in the mid/HF where they overlap. Walk from mains-only to fill-only zones and listen.", "Store 'Main+Fill'", 11}},
};

// ===================== Main component =====================
MainComponent::MainComponent() {
    setSize(1280, 820);
    dm.initialiseWithDefaultDevices(4, 4);
    if (auto* d = dm.getCurrentAudioDevice()) { auto s = dm.getAudioDeviceSetup(); s.sampleRate = 48000; dm.setAudioDeviceSetup(s, true); (void) d; }
    dm.addAudioCallback(&engine);
    selector = std::make_unique<AudioDeviceSelectorComponent>(dm, 1, 4, 1, 4, false, false, false, false);
    addChildComponent(*selector);

    tf = std::make_unique<foh::TransferFunction>(16384, 48000.0); rta = std::make_unique<foh::RTA>(16384, 48000.0, 3);
    micHist.assign(96000, 0.f); refHist.assign(96000, 0.f);

    const char* pages[] = {"Setup", "Transfer Function", "RTA", "Guides"};
    for (int i = 0; i < 4; ++i) { auto* b = nav.add(new TextButton(pages[i])); b->setClickingTogglesState(false); b->onClick = [this, i] { showPage(i); }; addAndMakeVisible(b); }
    addAndMakeVisible(status); status.setColour(Label::textColourId, Col::text);
    addAndMakeVisible(help); help.setJustificationType(Justification::topLeft); help.setColour(Label::textColourId, Col::text); help.setFont(14.f);

    auto chanItems = [](ComboBox& b) { for (int i = 1; i <= 4; ++i) b.addItem("Ch " + String(i), i); };
    for (auto* b : {&micBox, &refBox, &outABox, &outBBox}) { chanItems(*b); addChildComponent(*b); }
    micBox.setSelectedId(1); refBox.setSelectedId(2); outABox.setSelectedId(1); outBBox.setSelectedId(2);
    micBox.onChange = [this] { engine.micCh = micBox.getSelectedId() - 1; };
    refBox.onChange = [this] { engine.refCh = refBox.getSelectedId() - 1; };
    outABox.onChange = [this] { engine.outA = outABox.getSelectedId() - 1; };
    outBBox.onChange = [this] { engine.outB = outBBox.getSelectedId() - 1; };
    genBox.addItem("Generator off", 1); genBox.addItem("Pink noise", 2); genBox.addItem("Log sweep 20-20k (5 s)", 3); genBox.setSelectedId(1);
    genBox.onChange = [this] { engine.gen = genBox.getSelectedId() - 1; }; addChildComponent(genBox);
    genGain.setRange(-60, 0, 0.5); genGain.setValue(-20); genGain.setTextValueSuffix(" dBFS"); genGain.onValueChange = [this] { engine.genGainDb = (float) genGain.getValue(); }; addChildComponent(genGain);
    calBtn.onClick = [this] { loadCal(); }; addChildComponent(calBtn);
    calToggle.setClickingTogglesState(true); calToggle.onClick = [this] { calOn = calToggle.getToggleState() && cal.ok(); }; addChildComponent(calToggle);
    for (auto* l : {&setupHelp, &calLabel, &levelLabel}) { l->setColour(Label::textColourId, Col::text); addChildComponent(*l); }
    setupHelp.setJustificationType(Justification::topLeft);
    setupHelp.setText("Mic input (ECM8000, In 1)    Reference input (loopback, In 2)    Output to console (Out 1)    Loopback output (Out 2)", dontSendNotification);
    calLabel.setText("No mic cal loaded", dontSendNotification);

    // TF page
    magPlot.yLabel = "Magnitude dB"; magPlot.yMin = -18; magPlot.yMax = 18;
    phasePlot.yLabel = "Phase deg"; phasePlot.yMin = -180; phasePlot.yMax = 180;
    cohPlot.yLabel = "Coherence"; cohPlot.yMin = 0; cohPlot.yMax = 1;
    for (auto* p : {&magPlot, &phasePlot, &cohPlot, &rtaPlot}) addChildComponent(*p);
    smoothBox.addItem("No smoothing", 1); smoothBox.addItem("1/3 oct", 4); smoothBox.addItem("1/6 oct", 7); smoothBox.addItem("1/12 oct", 13); smoothBox.addItem("1/24 oct", 25); smoothBox.addItem("1/48 oct", 49); smoothBox.setSelectedId(13);
    avgBox.addItem("Avg 4", 4); avgBox.addItem("Avg 8", 8); avgBox.addItem("Avg 16", 16); avgBox.addItem("Avg 32", 32); avgBox.addItem("Avg infinite", 1000); avgBox.setSelectedId(16);
    avgBox.onChange = [this] { int a = avgBox.getSelectedId(); tf->setAverages(a == 1000 ? 0 : a); };
    tf->setAverages(16);
    for (auto* c : std::initializer_list<Component*>{&smoothBox, &avgBox, &storeBtn, &findBtn, &clearAvgBtn, &pauseBtn, &delayLabel, &insertBtn, &traceList, &delTraceBtn}) addChildComponent(c);
    storeBtn.onClick = [this] { storeTrace("Trace " + String(traces.count() + 1)); };
    findBtn.onClick = [this] { findDelay(); };
    insertBtn.onClick = [this] { if (!lastDelay.arrivals.empty()) { tf->setReferenceDelaySamples((int) std::lround(lastDelay.arrivals[0].delaySamples)); lockedDelayMs = lastDelay.arrivals[0].delayMs; } };
    clearAvgBtn.onClick = [this] { tf->reset(); rta->reset(); };
    pauseBtn.setClickingTogglesState(true); pauseBtn.onClick = [this] { paused = pauseBtn.getToggleState(); };
    delayLabel.setFont(18.f); delayLabel.setColour(Label::textColourId, Col::text); delayLabel.setText("Delay finder: press Find delay", dontSendNotification);
    traceModel.o = this; traceList.setModel(&traceModel); traceList.setColour(ListBox::backgroundColourId, Col::panel); traceList.setRowHeight(24);
    delTraceBtn.onClick = [this] { traces.remove(traceList.getSelectedRow()); refreshTraceList(); };

    // RTA page
    rtaPlot.yLabel = "Level dBFS (cal-corrected if Cal ON)"; rtaPlot.yMin = -100; rtaPlot.yMax = 0; rtaPlot.bars = true;
    rtaFracBox.addItem("1/3 octave", 3); rtaFracBox.addItem("1/6 octave", 6); rtaFracBox.setSelectedId(3);
    rtaFracBox.onChange = [this] { rta->setFraction(rtaFracBox.getSelectedId()); }; addChildComponent(rtaFracBox);

    // Guides
    guideBox.addItem("W1: Setup + reference delay", 1); guideBox.addItem("W2: Sub to mains", 2); guideBox.addItem("W3: Fills to mains", 3);
    guideBox.setSelectedId(1); guideBox.onChange = [this] { guide = guideBox.getSelectedId() - 1; step = 0; layoutGuide(); };
    for (auto* c : std::initializer_list<Component*>{&guideBox, &guideTitle, &guideBody, &guideResult, &guidePrev, &guideNext, &guideAct}) addChildComponent(c);
    guideTitle.setFont(Font(26.f, Font::bold)); guideBody.setFont(17.f); guideResult.setFont(17.f);
    guideBody.setJustificationType(Justification::topLeft); guideResult.setJustificationType(Justification::topLeft);
    guideResult.setColour(Label::textColourId, Col::cyan);
    guidePrev.onClick = [this] { if (step > 0) { --step; layoutGuide(); } };
    guideNext.onClick = [this] { if (step + 1 < (int) kGuides[(size_t) guide].size()) { ++step; layoutGuide(); } };
    guideAct.onClick = [this] { guideAction(kGuides[(size_t) guide][(size_t) step].act); };

    getLookAndFeel().setColour(ResizableWindow::backgroundColourId, Col::bg);
    showPage(0); startTimerHz(15);
}
MainComponent::~MainComponent() { stopTimer(); dm.removeAudioCallback(&engine); traceList.setModel(nullptr); }

void MainComponent::paint(Graphics& g) {
    g.fillAll(Col::bg); g.setColour(Col::panel); g.fillRect(0, 0, 180, getHeight()); g.fillRect(getWidth() - 280, 44, 280, getHeight());
    g.setColour(Col::text); g.setFont(Font(18.f, Font::bold)); g.drawText("FOH", 16, 10, 40, 24, Justification::left);
    g.setColour(Col::cyan); g.drawText("Gauge", 56, 10, 80, 24, Justification::left);
    g.setColour(Col::cyan); g.setFont(12.f); g.drawText("WHAT AM I LOOKING AT?", getWidth() - 266, 54, 250, 16, Justification::left);
}

void MainComponent::resized() {
    auto r = getLocalBounds(); auto left = r.removeFromLeft(180); left.removeFromTop(50);
    for (auto* b : nav) b->setBounds(left.removeFromTop(40).reduced(8, 4));
    auto top = r.removeFromTop(44); status.setBounds(top.reduced(12, 8));
    auto right = r.removeFromRight(280); right.removeFromTop(30); help.setBounds(right.reduced(12));
    auto c = r.reduced(16);
    // setup
    { auto s = c; selector->setBounds(s.removeFromTop(300));
      auto row = s.removeFromTop(24); setupHelp.setBounds(row);
      row = s.removeFromTop(30); int w = row.getWidth() / 4; for (auto* b : {&micBox, &refBox, &outABox, &outBBox}) b->setBounds(row.removeFromLeft(w).reduced(4, 2));
      s.removeFromTop(12); row = s.removeFromTop(30); genBox.setBounds(row.removeFromLeft(240)); genGain.setBounds(row.removeFromLeft(320).reduced(8, 0));
      s.removeFromTop(12); row = s.removeFromTop(30); calBtn.setBounds(row.removeFromLeft(160)); calToggle.setBounds(row.removeFromLeft(90).reduced(6, 0)); calLabel.setBounds(row);
      s.removeFromTop(12); levelLabel.setBounds(s.removeFromTop(30)); }
    // tf
    { auto s = c; auto bar = s.removeFromTop(32);
      for (auto* b : std::initializer_list<Component*>{&smoothBox, &avgBox}) b->setBounds(bar.removeFromLeft(130).reduced(3, 2));
      for (auto* b : std::initializer_list<Component*>{&storeBtn, &clearAvgBtn, &pauseBtn}) b->setBounds(bar.removeFromLeft(100).reduced(3, 2));
      auto bottom = s.removeFromBottom(110); auto tl = bottom.removeFromRight(260); delTraceBtn.setBounds(tl.removeFromBottom(26)); traceList.setBounds(tl);
      auto db = bottom.removeFromTop(40); findBtn.setBounds(db.removeFromLeft(110).reduced(3)); insertBtn.setBounds(db.removeFromLeft(120).reduced(3)); delayLabel.setBounds(bottom.withTrimmedTop(-40).withTrimmedLeft(240).removeFromTop(110));
      delayLabel.setBounds(Rectangle<int>(db.getX(), db.getY(), db.getWidth(), 110));
      int h = s.getHeight(); magPlot.setBounds(s.removeFromTop(h * 45 / 100)); phasePlot.setBounds(s.removeFromTop(h * 33 / 100)); cohPlot.setBounds(s); }
    // rta
    { auto s = c; rtaFracBox.setBounds(s.removeFromTop(32).removeFromLeft(160).reduced(3, 2)); rtaPlot.setBounds(s); }
    // guides
    { auto s = c; guideBox.setBounds(s.removeFromTop(32).removeFromLeft(300)); s.removeFromTop(16);
      guideTitle.setBounds(s.removeFromTop(40)); guideBody.setBounds(s.removeFromTop(160)); auto row = s.removeFromTop(50);
      guidePrev.setBounds(row.removeFromLeft(110).reduced(4)); guideAct.setBounds(row.removeFromLeft(240).reduced(4)); guideNext.setBounds(row.removeFromLeft(110).reduced(4));
      s.removeFromTop(10); guideResult.setBounds(s); }
}

void MainComponent::showPage(int p) {
    page = p;
    for (int i = 0; i < nav.size(); ++i) nav[i]->setColour(TextButton::buttonColourId, i == p ? Col::cyan.withAlpha(0.3f) : Col::panel);
    std::vector<Component*> setupC {selector.get(), &micBox, &refBox, &outABox, &outBBox, &genBox, &genGain, &calBtn, &calToggle, &setupHelp, &calLabel, &levelLabel};
    std::vector<Component*> tfC {&magPlot, &phasePlot, &cohPlot, &smoothBox, &avgBox, &storeBtn, &findBtn, &clearAvgBtn, &pauseBtn, &delayLabel, &insertBtn, &traceList, &delTraceBtn};
    std::vector<Component*> rtaC {&rtaPlot, &rtaFracBox};
    std::vector<Component*> gC {&guideBox, &guideTitle, &guideBody, &guideResult, &guidePrev, &guideNext, &guideAct};
    for (auto* v : {&setupC, &tfC, &rtaC, &gC}) for (auto* x : *v) x->setVisible(false);
    auto& on = p == 0 ? setupC : p == 1 ? tfC : p == 2 ? rtaC : gC; for (auto* x : on) x->setVisible(true);
    if (p == 3) { // guides show the TF plots smaller? keep simple: text-only + results
        layoutGuide(); }
    help.setText(explain(), dontSendNotification); repaint();
}

String MainComponent::explain() const {
    switch (page) {
        case 0: return "Setup. Pick your interface (UMC404HD) at 48 kHz.\n\nMic = In 1 (ECM8000, +48 V).\nReference = In 2: a 1/4\" TRS loopback from Out 2, so it carries exactly what goes to the system.\n\nOut 1 goes to the console. Both inputs should sit near -20 dBFS on pink noise, never clipping.\n\nLoad your mic cal file (CSV/TXT/FRD) and turn Cal ON.";
        case 1: return "This compares the console (reference) to the mic. Anything not flat is what the speakers + room are doing.\n\nTop: Magnitude. 0 dB = unchanged.\n\nMiddle: Phase. A steady slope = leftover delay; press Find delay then Insert delay.\n\nBottom: Coherence. How much to trust the trace. Low = probably a reflection or noise: don't EQ it.\n\nStored traces are dashed.";
        case 2: return "RTA shows the level in each 1/3 or 1/6 octave band at the mic.\n\nUse it to see what's loud in the room. To judge the system itself, prefer the transfer function: the RTA includes the music/noise spectrum.";
        default: return "Guides walk you through one job at a time. Each step: read, press the big button, check the result.\n\nThe app explains and suggests; you decide and make the change in your own gear.";
    }
}

void MainComponent::timerCallback() {
    const double fs = engine.sampleRate;
    if (std::abs(fs - analysisRate) > 1) { analysisRate = fs; tf = std::make_unique<foh::TransferFunction>(16384, fs); rta = std::make_unique<foh::RTA>(16384, fs, rtaFracBox.getSelectedId());
        int a = avgBox.getSelectedId(); tf->setAverages(a == 1000 ? 0 : a); }
    static thread_local std::vector<float> m(16384), rr(16384);
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
    status.setText((dev ? dev->getName() + " @ " + String((int) engine.sampleRate) + " Hz" : String("No device")) + "   |   Mic peak " + dbs(mp) + "   Ref peak " + dbs(rp)
                   + "   |   Cal " + String(calOn ? "ON" : "off") + "   |   Ref delay " + String(tf->referenceDelaySamples() / engine.sampleRate * 1000, 2) + " ms"
                   + (mp > 0.99f || rp > 0.99f ? "   ▲ CLIPPING" : ""), dontSendNotification);
    if (page == 0) levelLabel.setText(String(mp > 0.01f ? "✓ Mic signal  " : "■ No mic signal  ") + String(rp > 0.01f ? "✓ Reference signal" : "■ No reference signal (check Out 2 -> In 2 loopback)"), dontSendNotification);
    if (page == 1 || page == 3) updateTF();
    if (page == 2) updateRTA();
}

void MainComponent::updateTF() {
    int sm = smoothBox.getSelectedId() - 1;
    lastTF = tf->result(sm); if (calOn) cal.apply(lastTF);
    if (page != 1) return;
    auto mk = [](PlotComponent& p) { p.series.clear(); };
    mk(magPlot); mk(phasePlot); mk(cohPlot);
    const Colour tc[] = {Colour(0xffb0b6c0), Colour(0xffffb020), Colour(0xffa080ff), Colour(0xff60d080), Colour(0xffff7090), Colour(0xff70a0ff), Colour(0xffd0d060), Colour(0xffff9050)};
    for (int i = 0; i < traces.count(); ++i) { auto& t = traces.at(i); if (!t.visible) continue;
        PlotComponent::Series s; s.c = tc[i % 8]; s.dashed = true; s.width = 1.2f;
        logDecimate(t.tf, t.tf.magDb, s.f, s.v); magPlot.series.push_back(s);
        logDecimate(t.tf, t.tf.phaseDeg, s.f, s.v); phasePlot.series.push_back(s); }
    PlotComponent::Series s; s.c = Col::cyan;
    logDecimate(lastTF, lastTF.magDb, s.f, s.v); magPlot.series.push_back(s);
    logDecimate(lastTF, lastTF.phaseDeg, s.f, s.v); phasePlot.series.push_back(s);
    s.c = Col::text; logDecimate(lastTF, lastTF.coherence, s.f, s.v); cohPlot.series.push_back(s);
    magPlot.note = "averages: " + String(lastTF.averages); magPlot.repaint(); phasePlot.repaint(); cohPlot.repaint();
}

void MainComponent::updateRTA() {
    if (calOn) rta->setCalibrationDb(cal.correctionDb(8193, engine.sampleRate / 16384)); else rta->setCalibrationDb({});
    auto b = rta->bandsDb(); rtaPlot.series.clear(); rtaPlot.barLo.clear(); rtaPlot.barHi.clear();
    PlotComponent::Series s; s.c = Col::cyan;
    for (size_t i = 0; i < b.size(); ++i) { auto& bd = rta->bands()[i]; rtaPlot.barLo.push_back(bd.fLo); rtaPlot.barHi.push_back(bd.fHi); s.f.push_back(bd.fCenter); s.v.push_back(b[i]); }
    rtaPlot.series.push_back(s); rtaPlot.repaint();
}

void MainComponent::findDelay() {
    // last ~1 s of history, oldest first
    const int L = 48000; std::vector<float> r((size_t) L), m((size_t) L); int H = (int) micHist.size();
    for (int i = 0; i < L; ++i) { int idx = (histPos - L + i + H) % H; r[(size_t) i] = refHist[(size_t) idx]; m[(size_t) i] = micHist[(size_t) idx]; }
    lastDelay = foh::findDelay(r, m, engine.sampleRate, true);
    if (lastDelay.arrivals.empty()) { delayLabel.setText("Delay finder: no signal", dontSendNotification); return; }
    String t = "Delay finder: " + String(lastDelay.arrivals[0].delayMs, 2) + " ms  (~" + String(lastDelay.arrivals[0].distanceM, 2) + " m)   confidence "
               + String(lastDelay.confidence > 0.7 ? "high" : lastDelay.confidence > 0.35 ? "medium" : "LOW");
    for (size_t i = 1; i < lastDelay.arrivals.size(); ++i) t << "\n   arrival " << (int) i + 1 << ": " << String(lastDelay.arrivals[i].delayMs, 2) << " ms (" << String(lastDelay.arrivals[i].relLevelDb, 1) << " dB)";
    if (lastDelay.arrivals.size() > 1 && lastDelay.arrivals[1].relLevelDb > -3) t << "\n   ▲ Two similar spikes: is only one speaker on?";
    delayLabel.setText(t, dontSendNotification);
}

void MainComponent::storeTrace(const String& name) {
    updateTF();
    if (!traces.store(name.toStdString(), lastTF, tf->referenceDelaySamples() / engine.sampleRate * 1000))
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "Trace store full", "Up to 8 traces. Delete one first.");
    refreshTraceList();
}
void MainComponent::refreshTraceList() { traceList.updateContent(); traceList.repaint(); }
int MainComponent::TraceModel::getNumRows() { return o->traces.count(); }
void MainComponent::TraceModel::paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) {
    if (row >= o->traces.count()) return; auto& t = o->traces.at(row);
    if (sel) g.fillAll(Col::cyan.withAlpha(0.2f));
    g.setColour(t.visible ? Col::text : Col::dim); g.setFont(13.f);
    g.drawText(String(t.visible ? "[x] " : "[ ] ") + String(t.name) + "  (" + String(t.refDelayMs, 2) + " ms)", 6, 0, w - 6, h, Justification::centredLeft);
}
void MainComponent::TraceModel::listBoxItemClicked(int row, const MouseEvent& e) {
    if (row < o->traces.count() && e.x < 30) { o->traces.at(row).visible = !o->traces.at(row).visible; o->refreshTraceList(); } }

void MainComponent::loadCal() {
    chooser = std::make_unique<FileChooser>("Mic calibration file", File(), "*.csv;*.txt;*.frd;*.cal");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) {
        auto f = fc.getResult(); if (!f.existsAsFile()) return;
        cal = foh::loadMicCal(f.getFullPathName().toStdString());
        if (cal.ok()) { calOn = true; calToggle.setToggleState(true, dontSendNotification);
            calLabel.setText("✓ " + f.getFileName() + ": " + String((int) cal.hz.size()) + " points, " + String(cal.hz.front(), 0) + "-" + String(cal.hz.back(), 0) + " Hz" + (cal.hasPhase ? ", with phase" : ""), dontSendNotification); }
        else { calOn = false; calLabel.setText("▲ Could not read cal: " + String(cal.error), dontSendNotification); }
    });
}

void MainComponent::layoutGuide() {
    auto& st = kGuides[(size_t) guide][(size_t) step];
    guideTitle.setText(String(st.title) + "   (" + String(step + 1) + "/" + String((int) kGuides[(size_t) guide].size()) + ")", dontSendNotification);
    guideBody.setText(st.body, dontSendNotification); guideAct.setButtonText(st.action);
    guidePrev.setEnabled(step > 0); guideNext.setEnabled(step + 1 < (int) kGuides[(size_t) guide].size());
}

void MainComponent::guideAction(int a) {
    auto findTrace = [this](const char* n) -> const foh::Trace* { for (int i = traces.count() - 1; i >= 0; --i) if (traces.at(i).name == n) return &traces.at(i); return nullptr; };
    String out;
    switch (a) {
        case 1: showPage(0); return;
        case 2: genBox.setSelectedId(2); out = "Pink noise on at " + String(genGain.getValue(), 1) + " dBFS. Watch the peak meters in the top bar."; break;
        case 3: findDelay(); if (!lastDelay.arrivals.empty()) { tf->setReferenceDelaySamples((int) std::lround(lastDelay.arrivals[0].delaySamples)); lockedDelayMs = lastDelay.arrivals[0].delayMs;
                    out = "Locked reference delay: " + String(lockedDelayMs, 2) + " ms (~" + String(lastDelay.arrivals[0].distanceM, 2) + " m).\n" + delayLabel.getText(); }
                else out = "No signal found. Is pink noise playing and both inputs showing level?"; break;
        case 4: showPage(1); return;
        case 5: storeTrace("Position 1"); out = "Stored 'Position 1'."; break;
        case 6: storeTrace("Main"); out = "Stored 'Main'."; break;
        case 7: storeTrace("Sub"); out = "Stored 'Sub' (same locked delay " + String(lockedDelayMs, 2) + " ms)."; break;
        case 8: { auto* m = findTrace("Main"); auto* s = findTrace("Sub"); if (!m || !s) { out = "Store 'Main' and 'Sub' first."; break; }
            auto xo = foh::acousticCrossover(m->tf, s->tf, 10.0); if (!xo.found) { out = "No overlap within 10 dB between 30-300 Hz. Check levels."; break; }
            auto po = foh::phaseOffset(m->tf, s->tf, xo.lo, xo.hi);
            double period = 1000.0 / po.centerHz;
            double inv = foh::wrapDeg(po.meanDeg + 180.0); double invMs = -inv / 360.0 / po.centerHz * 1000.0;
            out << "Acoustic crossover: " << String(xo.lo, 0) << "-" << String(xo.hi, 0) << " Hz (centre ~" << String(po.centerHz, 0) << " Hz).\n"
                << "Sub phase vs main: " << String(po.meanDeg, 0) << " deg  =>  " << String(std::abs(po.impliedMs), 2) << " ms (" << (po.impliedMs > 0 ? "sub is LATE" : "sub is EARLY") << ").\n\n"
                << "A: delay the " << (po.impliedMs > 0 ? "MAINS" : "SUB") << " by ~" << String(std::abs(po.impliedMs), 2) << " ms\n"
                << "B: flip sub polarity, then " << (invMs > 0 ? "delay mains" : "delay sub") << " by ~" << String(std::abs(invMs), 2) << " ms\n"
                << "(Other answers differ by whole cycles of " << String(period, 1) << " ms. Prefer the one where slopes match across the band and needs least delay.)\n"
                << "Target: within ~60 deg and parallel across the band. Then verify with both on.";
            auto vs = foh::virtualSum(m->tf, s->tf); if (traces.count() < 8) traces.store("Virtual sum", vs, lockedDelayMs); refreshTraceList();
            magPlot.shade = {{xo.lo, xo.hi}}; phasePlot.shade = magPlot.shade; break; }
        case 9: storeTrace("Both"); out = "Stored 'Both'. Open Transfer Function: 'Both' should match or exceed 'Virtual sum' in the shaded band (no dip)."; break;
        case 10: { findDelay(); if (lastDelay.arrivals.empty()) { out = "No signal found."; break; }
            double fillMs = lastDelay.arrivals[0].delayMs + tf->referenceDelaySamples() * 0.0; // raw arrival vs reference
            double diff = lockedDelayMs - fillMs;
            out << "Fill arrives at " << String(fillMs, 2) << " ms; mains at " << String(lockedDelayMs, 2) << " ms.\n"
                << (diff > 0 ? "Fill arrives " + String(diff, 2) + " ms BEFORE the mains here.\nSuggested: delay the fill by ~" + String(diff, 2) + " ms (optionally a few ms more)."
                             : "Fill already arrives after the mains (" + String(-diff, 2) + " ms). Check the seam position / which speaker is on.");
            break; }
        case 11: storeTrace("Main+Fill"); out = "Stored 'Main+Fill'. Compare with 'Main' on the Transfer Function page."; break;
        default: break;
    }
    guideResult.setText(out, dontSendNotification);
}
