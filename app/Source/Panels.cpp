#include "Panels.h"
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;
namespace WS = foh::theme::ws;
using fohws::Json;

static void styleLabel(Label& l, float size, Colour c, bool bold = false) { l.setFont(FohLookAndFeel::font(size, bold)); l.setColour(Label::textColourId, c); }
static Rectangle<int> controlRow(Rectangle<int>& r, int h = 26) { auto row = r.removeFromTop(h); r.removeFromTop(6); return row; }

// ---- helpers shared by the plot panels ----
static void addStoredTraces(const AppModel& m, PlotComponent& p, bool mag) {
    for (int i = 0; i < m.traces.count(); ++i) { auto& t = m.traces.at(i); if (!t.visible) continue;
        PlotComponent::Series s; s.c = C::storedTrace(i % 8); s.dash = foh::theme::stroke::dashStored; s.width = D::traceStored(); s.label = String(t.name);
        logDecimate(t.tf, mag ? t.tf.magDb : t.tf.phaseDeg, s.f, s.v); p.series.push_back(s); }
}
static std::vector<std::pair<double,double>> blankedRegions(const foh::TFResult& tf) { // from fohcore's TFResult::blanked
    std::vector<double> f, v; std::vector<uint8_t> bl; logDecimate(tf, tf.coherence, f, v, &bl); std::vector<std::pair<double,double>> lc;
    for (size_t i = 0; i < bl.size(); ++i) if (bl[i]) { double lo = i ? std::sqrt(f[i - 1] * f[i]) : f[i], hi = i + 1 < f.size() ? std::sqrt(f[i] * f[i + 1]) : f[i];
        if (!lc.empty() && lc.back().second >= lo * 0.999) lc.back().second = hi; else lc.push_back({lo, hi}); }
    return lc;
}

// ===================== Transfer function (magnitude) =====================
class TransferFunctionPanel : public Panel {
public:
    explicit TransferFunctionPanel(AppModel& m) : Panel(m, "TransferFunction", "Transfer Function") {
        helpText = "This compares the console (reference) to the mic. Anything that isn't flat is what the speakers + room are doing to your mix.\n\nMagnitude: 0 dB = unchanged.\n\nDimmed regions: coherence below the Dim threshold. Don't EQ there; move the mic or treat the reflection.\n\nStored traces are dashed.";
        plot.yLabel = "MAGNITUDE DB"; plot.yMin = -12; plot.yMax = 12; plot.yStep = 6; plot.showLegend = true; plot.showLowCohCallout = true;
        addAndMakeVisible(plot);
        smooth.addItem("Smooth off", 1); smooth.addItem("Smooth 1/3", 4); smooth.addItem("Smooth 1/6", 7); smooth.addItem("Smooth 1/12", 13); smooth.addItem("Smooth 1/24", 25); smooth.addItem("Smooth 1/48", 49);
        avg.addItem("Avg 4", 4); avg.addItem("Avg 8", 8); avg.addItem("Avg 16", 16); avg.addItem("Avg 32", 32); avg.addItem(u8("Avg \xe2\x88\x9e"), 1000);
        for (int t : {40, 50, 60, 70, 80}) blank.addItem("Dim < " + String(t / 100.0, 2), t);
        smooth.onChange = [this] { model.setSmoothing(smooth.getSelectedId() - 1); };
        avg.onChange = [this] { int a = avg.getSelectedId(); model.setAverages(a == 1000 ? 0 : a); };
        blank.onChange = [this] { model.setBlankThreshold(blank.getSelectedId() / 100.0); };
        reset.onClick = [this] { model.send(foh::Command::resetAverages()); };
        store.onClick = [this] { model.storeTrace("Trace " + String(model.traces.count() + 1)); };
        for (auto* c : std::initializer_list<Component*>{&smooth, &avg, &blank, &reset, &store}) addAndMakeVisible(c);
        syncControls();
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::plotPanelMinHeight }; }
    int priority() const override { return 100; }
    Json saveState() const override { Json j = Json::object(); j.set("yMin", plot.yMin); j.set("yMax", plot.yMax); return j; }
    void loadState(const Json& j) override { if (j.has("yMin")) { plot.yMin = j["yMin"].asNumber(-12); plot.yMax = j["yMax"].asNumber(12); plot.yStep = (plot.yMax - plot.yMin) / 4; } }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto row = controlRow(r);
        store.setBounds(row.removeFromRight(64)); row.removeFromRight(6); reset.setBounds(row.removeFromRight(84)); row.removeFromRight(6);
        avg.setBounds(row.removeFromRight(92)); row.removeFromRight(6); smooth.setBounds(row.removeFromRight(118)); row.removeFromRight(6); blank.setBounds(row.removeFromRight(108));
        plot.setBounds(r);
    }
    void syncControls() { smooth.setSelectedId(model.smoothingFraction + 1, dontSendNotification); avg.setSelectedId(model.averages == 0 ? 1000 : model.averages, dontSendNotification);
                          blank.setSelectedId(roundToInt(model.blankThreshold * 100), dontSendNotification); }
    void modelChanged(int w) override {
        if (w & AppModel::Settings) syncControls();
        if (!(w & (AppModel::Snapshot | AppModel::Traces | AppModel::Overlay | AppModel::Theme)) || !isShowing()) return;
        plot.series.clear(); plot.shade.clear(); plot.shadeLabel = {};
        auto& ov = model.overlay;
        if (ov.active && !ov.mag.empty()) { plot.series = ov.mag; plot.shade = ov.shade; plot.shadeLabel = ov.shadeLabel; }
        else { addStoredTraces(model, plot, true);
               PlotComponent::Series s; s.c = C::traceLive(); s.label = "Live"; logDecimate(model.tf, model.tf.magDb, s.f, s.v, &s.gap); plot.series.push_back(s); }
        plot.lowCoh = blankedRegions(model.tf);
        plot.note = "averages " + String(model.tf.averages) + (model.calApplied ? u8(" \xc2\xb7 cal applied") : "") + (qa::unsettled(model.conv) ? u8(" \xc2\xb7 \xe2\x96\xb2 unsettled") : "");
        plot.repaint();
    }
private:
    PlotComponent plot; ComboBox smooth, avg, blank; TextButton reset {"Reset avg"}, store {"Store"};
};

// ===================== Phase =====================
class PhasePanel : public Panel {
public:
    explicit PhasePanel(AppModel& m) : Panel(m, "Phase", u8("Phase (\xc2\xb0)")) {
        helpText = "Phase: timing per frequency. A steady slope means leftover delay; flat is ideal.\n\nIn the sub-to-mains guide, phase is shown up to 250 Hz, where it matters for the crossover.";
        plot.yLabel = u8("PHASE \xc2\xb0"); plot.yMin = -180; plot.yMax = 180; plot.yStep = 90; plot.wrapPhase = true; plot.showLegend = true; addAndMakeVisible(plot);
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::panelMinHeight }; } // secondary plot: generic min height (spec Alignment gives it 0.27)
    int priority() const override { return 80; }
    void resized() override { plot.setBounds(getLocalBounds().reduced(pad, 6)); }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Snapshot | AppModel::Traces | AppModel::Overlay | AppModel::Theme)) || !isShowing()) return;
        plot.series.clear(); plot.shade.clear(); plot.centreNote = {};
        auto& ov = model.overlay;
        if (ov.active && !ov.phase.empty()) { plot.series = ov.phase; plot.shade = ov.shade; plot.shadeLabel = ov.shadeLabel; plot.centreNote = ov.phaseNote; }
        else { addStoredTraces(model, plot, false);
               PlotComponent::Series s; s.c = C::tracePhase(); s.label = "Live"; logDecimate(model.tf, model.tf.phaseDeg, s.f, s.v, &s.gap); plot.series.push_back(s); }
        plot.lowCoh = blankedRegions(model.tf); plot.repaint();
    }
private:
    PlotComponent plot;
};

// ===================== Coherence (+ traffic light) =====================
class CoherencePanel : public Panel {
public:
    explicit CoherencePanel(AppModel& m) : Panel(m, "Coherence", "Coherence") {
        helpText = "Coherence: how much to trust the trace (0 to 1).\n\nGood: above 0.9. Fair: 0.6 to 0.9. Poor: below 0.6. Low coherence is usually a reflection or noise: don't EQ it.";
        plot.yLabel = "COHERENCE"; plot.yMin = 0; plot.yMax = 1; plot.yStep = 0.5; addAndMakeVisible(plot);
    }
    fohws::Size minSize() const override { return { foh::theme::ws::panelMinWidth, 90 }; } // Alignment gives it a 0.15 strip: the traffic light alone is useful there
    void resized() override { auto r = getLocalBounds().reduced(pad, 6); light = r.removeFromTop(22); plot.setVisible(r.getHeight() > 60); plot.setBounds(r); }
    void paint(Graphics& g) override { // traffic light: shape + word, never colour only
        auto cl = qa::coherenceLight(model.tf); double mean = cl.mean; int state = cl.state;
        const char* words[] = {"Good", "Fair", "Poor"}; Colour cols[] = {C::ok(), C::warning(), C::danger()};
        float x = (float) light.getX(); g.setFont(FohLookAndFeel::font(T::small, true));
        for (int i = 0; i < 3; ++i) { bool on = i == state; auto d = Rectangle<float>(x, light.getCentreY() - 6.f, 12, 12); g.setColour(on ? cols[i] : C::textAxis());
            if (i == 0) on ? g.fillEllipse(d) : g.drawEllipse(d.reduced(0.5f), 1.f);
            else if (i == 1) { Path t; t.addTriangle(d.getCentreX(), d.getY(), d.getRight(), d.getBottom(), d.getX(), d.getBottom()); on ? g.fillPath(t) : g.strokePath(t, PathStrokeType(1.f)); }
            else on ? g.fillRect(d) : g.drawRect(d, 1.f);
            g.setColour(on ? C::textPrimary() : C::textAxis()); g.drawText(words[i], (int) x + 16, light.getY(), 50, light.getHeight(), Justification::centredLeft); x += 70; }
        g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::small));
        g.drawText("Average " + String(mean, 2) + " (200 Hz-10 kHz)", (int) x + 6, light.getY(), light.getRight() - (int) x - 6, light.getHeight(), Justification::centredLeft);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Snapshot | AppModel::Theme)) || !isShowing()) return;
        plot.series.clear(); PlotComponent::Series s; s.c = C::traceCoherence(); s.width = D::traceSecondary(); logDecimate(model.tf, model.tf.coherence, s.f, s.v); plot.series.push_back(s);
        plot.repaint(); repaint(light);
    }
private:
    PlotComponent plot; Rectangle<int> light;
};

// ===================== RTA =====================
class RtaPanel : public Panel {
public:
    explicit RtaPanel(AppModel& m) : Panel(m, "RTA", "RTA") {
        helpText = "The RTA shows the level in each 1/3 or 1/6 octave band at the mic.\n\nUse it to see what's loud in the room. To judge the system itself, prefer the transfer function: the RTA includes the spectrum of whatever is playing.";
        plot.yLabel = "dBFS"; plot.yMin = -100; plot.yMax = 0; plot.yStep = 20; plot.bars = true; addAndMakeVisible(plot);
        frac.addItem("1/3 octave", 3); frac.addItem("1/6 octave", 6); frac.setSelectedId(model.rtaFraction, dontSendNotification);
        frac.onChange = [this] { model.setRtaFraction(frac.getSelectedId()); }; addAndMakeVisible(frac);
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::plotPanelMinHeight }; }
    int priority() const override { return 90; }
    Json saveState() const override { Json j = Json::object(); j.set("fraction", model.rtaFraction); return j; }
    void loadState(const Json& j) override { if (j.has("fraction")) { int f = j["fraction"].asInt(3); if (f != model.rtaFraction) model.setRtaFraction(f); } }
    void resized() override { auto r = getLocalBounds().reduced(pad, 6); auto row = controlRow(r); frac.setBounds(row.removeFromRight(120)); plot.setBounds(r); }
    void modelChanged(int w) override {
        if (w & AppModel::Settings) frac.setSelectedId(model.rtaFraction, dontSendNotification);
        if (!(w & (AppModel::Snapshot | AppModel::Theme)) || !isShowing()) return;
        plot.series.clear(); plot.barLo.clear(); plot.barHi.clear(); PlotComponent::Series s; s.c = C::accent();
        for (size_t i = 0; i < model.rtaDb.size() && i < model.rtaBands.size(); ++i) { auto& b = model.rtaBands[i]; plot.barLo.push_back(b.fLo); plot.barHi.push_back(b.fHi); s.f.push_back(b.fCenter); s.v.push_back(model.rtaDb[i]); }
        plot.series.push_back(s); plot.note = model.calApplied ? "cal applied" : ""; plot.repaint();
    }
private:
    PlotComponent plot; ComboBox frac;
};

// ===================== Delay finder =====================
class DelayFinderPanel : public Panel {
public:
    explicit DelayFinderPanel(AppModel& m) : Panel(m, "DelayFinder", "Delay Finder") {
        helpText = "The delay finder lines up the reference with the mic (cross-correlation in fohcore).\n\nInsert delay applies it to the reference so the transfer function phase flattens.\n\nConfidence 0 means no lock: check signal and that only one speaker is on.";
        value.setFont(FohLookAndFeel::numeric(34)); value.setText("--", dontSendNotification);
        styleLabel(detail, T::small, C::textSecondary()); detail.setJustificationType(Justification::topLeft); detail.setText("Press Find", dontSendNotification);
        makePrimary(insert); find.onClick = [this] { model.requestDelay(sub.getToggleState()); }; insert.onClick = [this] { model.lockDelayFromLast(); };
        sub.setButtonText("Sub (LF)");
        for (auto* c : std::initializer_list<Component*>{&value, &detail, &find, &insert, &sub}) addAndMakeVisible(c);
    }
    fohws::Size minSize() const override { return { 300, 90 }; }
    int priority() const override { return 70; }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto right = r.removeFromRight(110);
        insert.setBounds(right.removeFromTop(28)); right.removeFromTop(6); find.setBounds(right.removeFromTop(28)); right.removeFromTop(4); sub.setBounds(right.removeFromTop(22));
        value.setBounds(r.removeFromTop(44)); detail.setBounds(r);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Theme) { makePrimary(insert); detail.setColour(Label::textColourId, C::textSecondary()); }
        if (!(w & (AppModel::Delay | AppModel::Theme))) return;
        auto& d = model.delay;
        if (model.delayPending) { detail.setText(model.delayWasSub ? "Finding sub delay (3 s window)..." : "Finding delay...", dontSendNotification); return; }
        if (!model.delayLocked()) { value.setText("--", dontSendNotification); detail.setText(d.arrivals.empty() ? "No signal: is pink noise playing?" : "No lock (confidence 0): check signal and that only one speaker is on", dontSendNotification); return; }
        auto& a0 = d.arrivals[0];
        value.setText(String(qa::consoleMs(a0.delayMs), 2) + " ms", dontSendNotification);
        String t = String(d.confidence > 0.7 ? "High" : d.confidence > 0.35 ? "Medium" : "LOW") + u8(" confidence \xc2\xb7 polarity ") + (a0.inverted ? "INVERTED" : "normal")
                 + u8("\n\xe2\x89\x88 ") + String(a0.distanceM, 2) + " m" + (model.delayWasSub ? " (sub, LF-weighted)" : "");
        for (size_t i = 1; i < d.arrivals.size(); ++i) t << u8("  \xc2\xb7  #") << (int) i + 1 << " " << String(qa::consoleMs(d.arrivals[i].delayMs), 2) << " ms (" << String(d.arrivals[i].relLevelDb, 1) << " dB)";
        if (qa::twoSimilarArrivals(d)) t << u8("\n\xe2\x96\xb2 Two similar spikes: is only one speaker on?");
        detail.setText(t, dontSendNotification);
    }
private:
    Label value, detail; TextButton find {"Find"}, insert {"Insert delay"}; ToggleButton sub;
};

// ===================== Impulse =====================
class ImpulsePanel : public Panel {
public:
    explicit ImpulsePanel(AppModel& m) : Panel(m, "Impulse", "Impulse") {
        helpText = "The impulse is the delay finder's correlation around the first arrival. The tallest spike is the direct sound; later spikes are reflections.\n\nJudge polarity from the phase traces, not from the impulse.";
        addAndMakeVisible(imp);
    }
    Json saveState() const override { Json j = Json::object(); j.set("postMs", imp.postMs); return j; }
    void loadState(const Json& j) override { imp.postMs = j["postMs"].asNumber(45); }
    void resized() override { imp.setBounds(getLocalBounds().reduced(pad, 6)); }
    void modelChanged(int w) override { if (w & (AppModel::Delay | AppModel::Theme)) imp.setResult(model.delay, model.sampleRate); }
private:
    ImpulseComponent imp;
};

// ===================== Signal meters =====================
class SignalMetersPanel : public Panel {
public:
    explicit SignalMetersPanel(AppModel& m) : Panel(m, "SignalMeters", "Signal Meters") {
        helpText = "Mic = In 1 (ECM8000, +48 V). Reference = In 2, looped back from Out 2.\n\nBoth inputs should sit near -20 dBFS on pink noise, never clipping.";
    }
    fohws::Size minSize() const override { return { 240, 120 }; }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().reduced(pad, 10); const char* names[] = {"Mic", "Ref"}; float* lv[] = {&micDb, &refDb};
        for (int i = 0; i < 2; ++i) { auto row = r.removeFromTop(34); r.removeFromTop(8);
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::font(T::small, true)); g.drawText(names[i], row.removeFromLeft(36), Justification::centredLeft);
            auto val = row.removeFromRight(110); auto bar = row.reduced(4, 10).toFloat();
            g.setColour(C::divider()); g.fillRoundedRectangle(bar, 3.f);
            float db = *lv[i], frac = jlimit(0.f, 1.f, (db + 60.f) / 60.f);
            Colour c = db > -1 ? C::danger() : db > -12 ? C::warning() : db > -40 ? C::ok() : C::textAxis();
            g.setColour(c); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * frac), 3.f);
            g.setColour(C::textSecondary()); float x = bar.getX() + bar.getWidth() * (40.f / 60.f); g.fillRect(x, bar.getY() - 3, 1.f, bar.getHeight() + 6); // -20 dBFS target mark
            String st = db > -1 ? u8("\xe2\x96\xa0 CLIP") : db > -40 ? u8("\xe2\x97\x8f ") + String(db, 1) + " dBFS" : u8("\xe2\x96\xb2 no signal");
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::numeric(T::small, false)); g.drawText(st, val, Justification::centredRight); }
        g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::axisTick)); g.drawText("target -20 dBFS on pink noise", r.removeFromTop(16), Justification::left);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Levels | AppModel::Theme))) return;
        auto upd = [](float& v, float lin) { float db = lin > 1e-6f ? 20.f * std::log10(lin) : -120.f; v = db > v ? db : v - 1.5f; v = std::max(v, -120.f); }; // fast attack, slow release
        upd(micDb, model.micPeak); upd(refDb, model.refPeak); repaint();
    }
private:
    float micDb = -120, refDb = -120;
};

// ===================== Generator =====================
class GeneratorPanel : public Panel {
public:
    explicit GeneratorPanel(AppModel& m) : Panel(m, "Generator", "Generator") {
        helpText = "Pink noise for the transfer function and RTA; the log sweep loops every 5 s.\n\nOut 1 goes to the console, Out 2 loops back to In 2 as the reference.";
        type.addItem("Generator off", 1); type.addItem("Pink noise", 2); type.addItem("Log sweep 20 Hz-20 kHz (5 s, loops)", 3);
        type.onChange = [this] { model.engine.gen = type.getSelectedId() - 1; };
        gain.setRange(-60, 0, 0.5); gain.setTextValueSuffix(" dBFS"); gain.setTextBoxStyle(Slider::TextBoxRight, false, 90, 22);
        gain.onValueChange = [this] { model.engine.genGainDb = (float) gain.getValue(); };
        addAndMakeVisible(type); addAndMakeVisible(gain); sync();
    }
    fohws::Size minSize() const override { return { 240, 80 }; }
    void sync() { type.setSelectedId(model.engine.gen.load() + 1, dontSendNotification); gain.setValue(model.engine.genGainDb.load(), dontSendNotification); }
    void resized() override { auto r = getLocalBounds().reduced(pad, 8); type.setBounds(controlRow(r)); gain.setBounds(controlRow(r)); }
    void modelChanged(int w) override { if (w & AppModel::Settings) sync(); }
private:
    ComboBox type; Slider gain;
};

// ===================== Inputs & devices (mic / channel list) =====================
class InputsAndDevicesPanel : public Panel {
public:
    explicit InputsAndDevicesPanel(AppModel& m) : Panel(m, "InputsAndDevices", "Inputs & Devices"),
        selector(m.dm, 1, 4, 1, 4, false, false, false, false) {
        helpText = "Pick your interface (UMC404HD) at 48 kHz.\n\nMic = In 1 (ECM8000, +48 V).\n\nReference = In 2: a 1/4\" TRS loopback from Out 2, so it carries exactly what goes to the system.\n\nOut 1 goes to the console.";
        addAndMakeVisible(selector);
        auto items = [](ComboBox& b, const char* pre) { for (int i = 1; i <= 4; ++i) b.addItem(String(pre) + String(i), i); };
        items(mic, "Mic: In "); items(ref, "Ref: In "); items(outA, "To console: Out "); items(outB, "Loopback: Out ");
        mic.onChange = [this] { model.engine.micCh = mic.getSelectedId() - 1; }; ref.onChange = [this] { model.engine.refCh = ref.getSelectedId() - 1; };
        outA.onChange = [this] { model.engine.outA = outA.getSelectedId() - 1; }; outB.onChange = [this] { model.engine.outB = outB.getSelectedId() - 1; };
        for (auto* b : {&mic, &ref, &outA, &outB}) addAndMakeVisible(b);
        styleLabel(routing, T::label, C::textSecondary()); routing.setText("ROUTING (UMC404HD defaults)", dontSendNotification); addAndMakeVisible(routing);
        sync();
    }
    fohws::Size minSize() const override { return { 360, 220 }; }
    void sync() { mic.setSelectedId(model.engine.micCh + 1, dontSendNotification); ref.setSelectedId(model.engine.refCh + 1, dontSendNotification);
                  outA.setSelectedId(model.engine.outA + 1, dontSendNotification); outB.setSelectedId(model.engine.outB + 1, dontSendNotification); }
    Json saveState() const override { Json j = Json::object(); j.set("mic", model.engine.micCh.load()); j.set("ref", model.engine.refCh.load()); j.set("outA", model.engine.outA.load()); j.set("outB", model.engine.outB.load()); return j; }
    void loadState(const Json& j) override { if (!j.isObject()) return; model.engine.micCh = j["mic"].asInt(0); model.engine.refCh = j["ref"].asInt(1); model.engine.outA = j["outA"].asInt(0); model.engine.outB = j["outB"].asInt(1); sync(); }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto bottom = r.removeFromBottom(56);
        routing.setBounds(bottom.removeFromTop(20)); auto row = bottom.removeFromTop(28); int w = row.getWidth() / 4;
        for (auto* b : {&mic, &ref, &outA, &outB}) b->setBounds(row.removeFromLeft(w).reduced(3, 0));
        selector.setBounds(r);
    }
    void modelChanged(int w) override { if (w & AppModel::Theme) routing.setColour(Label::textColourId, C::textSecondary()); }
private:
    AudioDeviceSelectorComponent selector; ComboBox mic, ref, outA, outB; Label routing;
};

// ===================== Calibration =====================
class CalibrationPanel : public Panel {
public:
    explicit CalibrationPanel(AppModel& m) : Panel(m, "Calibration", "Mic Calibration") {
        helpText = "Load your mic cal file (CSV/TXT/FRD) and turn Cal on. It is applied to the transfer function and RTA by the analysis engine.";
        load.onClick = [this] { chooser = std::make_unique<FileChooser>("Mic calibration file", File(), "*.csv;*.txt;*.frd;*.cal");
            chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this](const FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) model.loadCalFile(f); }); };
        toggle.setButtonText("Cal on"); toggle.onClick = [this] { model.setCalEnabled(toggle.getToggleState()); };
        styleLabel(info, T::small, C::textSecondary()); info.setJustificationType(Justification::topLeft);
        for (auto* c : std::initializer_list<Component*>{&load, &toggle, &info}) addAndMakeVisible(c); sync();
    }
    fohws::Size minSize() const override { return { 240, 100 }; }
    void sync() {
        toggle.setToggleState(model.calOn, dontSendNotification);
        auto& c = model.cal;
        info.setText(model.calFile.isEmpty() ? String("No mic cal loaded") : c.ok() ? u8("\xe2\x9c\x93 ") + model.calFile + ": " + String((int) c.hz.size()) + " points, " + String(c.hz.front(), 0) + "-" + String(c.hz.back(), 0) + " Hz" + (c.hasPhase ? ", with phase" : "") + u8(" \xc2\xb7 Sens Factor ") + String(c.sensitivityOffsetDb, 2) + " dB"
                                                                       : u8("\xe2\x96\xb2 Could not read cal: ") + String(c.error), dontSendNotification); }
    void resized() override { auto r = getLocalBounds().reduced(pad, 8); auto row = controlRow(r, 28); load.setBounds(row.removeFromLeft(140)); row.removeFromLeft(10); toggle.setBounds(row.removeFromLeft(90)); info.setBounds(r); }
    void modelChanged(int w) override { if (w & (AppModel::Cal | AppModel::Theme)) { info.setColour(Label::textColourId, C::textSecondary()); sync(); } }
private:
    TextButton load {"Load mic cal..."}; ToggleButton toggle; Label info; std::unique_ptr<FileChooser> chooser;
};

// ===================== Trace list =====================
class TraceListPanel : public Panel, private ListBoxModel {
public:
    explicit TraceListPanel(AppModel& m) : Panel(m, "TraceList", "Traces") {
        helpText = "Up to 8 stored traces. Click the box to show or hide a trace on the plots. Stored traces are dashed.";
        list.setModel(this); list.setRowHeight(24); addAndMakeVisible(list); del.onClick = [this] { if (list.getSelectedRow() >= 0) model.removeTrace(list.getSelectedRow()); }; addAndMakeVisible(del);
    }
    ~TraceListPanel() override { list.setModel(nullptr); }
    int priority() const override { return 5; }
    void resized() override { auto r = getLocalBounds().reduced(pad, 6); del.setBounds(r.removeFromBottom(28).removeFromRight(110)); r.removeFromBottom(6); list.setBounds(r); }
    void modelChanged(int w) override { if (w & (AppModel::Traces | AppModel::Theme)) { list.updateContent(); list.repaint(); } }
    int getNumRows() override { return model.traces.count(); }
    void paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) override {
        if (row >= model.traces.count()) return; auto& t = model.traces.at(row);
        if (sel) g.fillAll(C::navItemActiveBg());
        auto box = Rectangle<float>(6, h / 2.f - 6, 12, 12); g.setColour(C::storedTrace(row % 8)); g.drawRect(box, 1.f); if (t.visible) g.fillRect(box.reduced(3));
        g.setColour(t.visible ? C::textPrimary() : C::textAxis()); g.setFont(FohLookAndFeel::font(T::small));
        g.drawText(String(t.name) + "   ref delay " + String(t.refDelayMs, 2) + " ms", 26, 0, w - 30, h, Justification::centredLeft);
    }
    void listBoxItemClicked(int row, const MouseEvent& e) override { if (row < model.traces.count() && e.x < 24) model.toggleTrace(row); }
private:
    ListBox list; TextButton del {"Delete trace"};
};

// ===================== Inspector =====================
class InspectorPanel : public Panel {
public:
    explicit InspectorPanel(AppModel& m) : Panel(m, "Inspector", "Inspector") { help.overlay = false; addAndMakeVisible(help); refresh(); }
    fohws::Size minSize() const override { return { WS::inspectorMinWidth, 160 }; }
    int priority() const override { return 0; } // first to fold into a tab (spec §7)
    void resized() override { help.setBounds(getLocalBounds()); }
    void refresh() { help.caption = model.inspectorTitle.isEmpty() ? String("INSPECTOR") : "INSPECTOR " + u8("\xc2\xb7 ") + model.inspectorTitle.toUpperCase(); help.setText(model.inspectorText); }
    void modelChanged(int w) override { if (w & (AppModel::Overlay | AppModel::Theme)) refresh(); }
private:
    HelpPanel help;
};

// ===================== Placeholder panels (in the preset trees, not in build #1) =====================
class PlaceholderPanel : public Panel {
public:
    PlaceholderPanel(AppModel& m, std::string id, String title, String what) : Panel(m, std::move(id), std::move(title)), text(std::move(what)) { helpText = text; }
    bool isPlaceholder() const override { return true; }
    int priority() const override { return panelId() == "Events" ? 5 : 10; } // spec §7: Inspector, then Events/TraceList, then the rest
    void paint(Graphics& g) override { g.setColour(C::textAxis()); g.setFont(FohLookAndFeel::font(T::small)); g.drawFittedText(text + "\n\nNot in build #1.", getLocalBounds().reduced(16), Justification::centred, 6); }
private:
    String text;
};

// Guide panel lives in GuidePanel.cpp
std::unique_ptr<Panel> makeGuidePanel(AppModel&);

PanelRegistry& PanelRegistry::standard() {
    static PanelRegistry r = [] {
        PanelRegistry x;
        auto add = [&x](std::string id, String title, std::function<std::unique_ptr<Panel>(AppModel&)> f) { x.add({ std::move(id), std::move(title), std::move(f) }); };
        add("TransferFunction", "Transfer Function", [](AppModel& m) { return std::make_unique<TransferFunctionPanel>(m); });
        add("Phase", "Phase", [](AppModel& m) { return std::make_unique<PhasePanel>(m); });
        add("Coherence", "Coherence", [](AppModel& m) { return std::make_unique<CoherencePanel>(m); });
        add("RTA", "RTA", [](AppModel& m) { return std::make_unique<RtaPanel>(m); });
        add("DelayFinder", "Delay Finder", [](AppModel& m) { return std::make_unique<DelayFinderPanel>(m); });
        add("Impulse", "Impulse", [](AppModel& m) { return std::make_unique<ImpulsePanel>(m); });
        add("SignalMeters", "Signal Meters", [](AppModel& m) { return std::make_unique<SignalMetersPanel>(m); });
        add("Generator", "Generator", [](AppModel& m) { return std::make_unique<GeneratorPanel>(m); });
        add("InputsAndDevices", "Inputs & Devices", [](AppModel& m) { return std::make_unique<InputsAndDevicesPanel>(m); });
        add("Calibration", "Mic Calibration", [](AppModel& m) { return std::make_unique<CalibrationPanel>(m); });
        add("TraceList", "Traces", [](AppModel& m) { return std::make_unique<TraceListPanel>(m); });
        add("Inspector", "Inspector", [](AppModel& m) { return std::make_unique<InspectorPanel>(m); });
        add("GuideInspector", "Guide", [](AppModel& m) { return makeGuidePanel(m); });
        auto ph = [&add](std::string id, String title, String what) { add(id, title, [id, title, what](AppModel& m) { return std::make_unique<PlaceholderPanel>(m, id, title, what); }); };
        ph("LimitPreset", "Limit Preset", "SPL limit preset (venue / regulation) for exposure monitoring.");
        ph("EQSuggestions", "EQ Suggestions", "Suggested EQ moves from the transfer function, coherence-weighted.");
        ph("SPLHistory", "SPL History", "SPL over time with LAeq / LCeq traces.");
        ph("SPLNumbers", "SPL Numbers", "LAeq-15 / LCeq-15 / LAeq-1 readouts.");
        ph("Dose", "Dose", "Noise dose against the selected limit.");
        ph("Events", "Events", "Log of clipping, limit breaches and captures.");
        return x; }();
    return r;
}
