#include "Panels.h"
#include <fohcore/input.h>
#include "BinaryData.h"
#include "SetupPage.h"
#include "SettleStatus.h"
#include "ShowMonitoring.h"
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
        PlotComponent::Series s; s.c = m.traceColour(i); s.dash = foh::theme::stroke::dashStored; s.width = D::traceStored(); s.label = String(t.name);
        logDecimate(t.tf, mag ? t.tf.magDb : t.tf.phaseDeg, s.f, s.v); p.series.push_back(s); }
}
static std::vector<std::pair<double,double>> blankedRegions(const foh::TFResult& tf) { // from fohcore's TFResult::blanked
    std::vector<double> f, v; std::vector<uint8_t> bl; logDecimate(tf, tf.coherence, f, v, &bl); std::vector<std::pair<double,double>> lc;
    for (size_t i = 0; i < bl.size(); ++i) if (bl[i]) { double lo = i ? std::sqrt(f[i - 1] * f[i]) : f[i], hi = i + 1 < f.size() ? std::sqrt(f[i] * f[i + 1]) : f[i];
        if (!lc.empty() && lc.back().second >= lo * 0.999) lc.back().second = hi; else lc.push_back({lo, hi}); }
    return lc;
}

// Each plot owns its target controls and saved state. Imports and parsing run only
// on the message thread; targets never enter the audio/analysis pipeline.
class TargetControls : public Component {
public:
    explicit TargetControls(double initialOffset = 0) {
        choice.addItem("Target +6", 1); choice.addItem("Target +9", 2); choice.addItem("Target +12", 3);
        choice.setSelectedId(1, dontSendNotification);
        shown.setButtonText("Target"); band.setButtonText("Tolerance");
        band.setTooltip("Show the imported tolerance, or +/-3 dB when the file has none.");
        offset.setRange(-100, 100, 0.5); offset.setTextValueSuffix(" dB offset");
        offset.setSliderStyle(Slider::IncDecButtons); offset.setTextBoxStyle(Slider::TextBoxLeft, false, 106, 24);
        offset.setValue(initialOffset, dontSendNotification);
        offset.setTooltip("Shift the relative target to the measured level; does not change measurement data.");
        choice.onChange = [this] { selectBuiltin(); changed(); };
        shown.onClick = band.onClick = [this] { changed(); };
        offset.onValueChange = [this] { changed(); };
        import.onClick = [this] {
            chooser = std::make_unique<FileChooser>("Import Smaart target", File{}, "*.txt;*.asc;*.csv");
            chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                [safe = Component::SafePointer<TargetControls>(this)](const FileChooser& fc) {
                    if (!safe || fc.getResult() == File{}) return;
                    std::string text;
                    try { text = foh::input::read(fc.getResult().getFullPathName().toStdString()); }
                    catch (const std::exception& e) { AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Target import", String(e.what())); return; }
                    auto parsed = foh::parseTarget(text);
                    if (!parsed.ok()) {
                        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Target import", String(parsed.error)); return;
                    }
                    safe->source = std::move(text); safe->target = std::move(parsed);
                    safe->choice.clear(dontSendNotification);
                    safe->choice.addItem("Target +6", 1); safe->choice.addItem("Target +9", 2); safe->choice.addItem("Target +12", 3);
                    safe->choice.addItem(fc.getResult().getFileNameWithoutExtension(), 4);
                    safe->choice.setSelectedId(4, dontSendNotification);
                    safe->shown.setToggleState(true, dontSendNotification);
                    safe->band.setToggleState(safe->target.useTolerance, dontSendNotification); safe->changed();
                });
        };
        for (auto* c : std::initializer_list<Component*>{&choice, &import, &shown, &band, &offset}) addAndMakeVisible(c);
        selectBuiltin();
    }
    std::function<void()> onChange;
    void resized() override {
        auto r = getLocalBounds();
        choice.setBounds(r.removeFromLeft(jmin(150, getWidth() / 4))); r.removeFromLeft(4);
        import.setBounds(r.removeFromLeft(66)); shown.setBounds(r.removeFromLeft(70));
        band.setBounds(r.removeFromLeft(92)); offset.setBounds(r);
    }
    void appendTo(PlotComponent& plot) const {
        plot.toleranceBands.clear();
        if (!shown.getToggleState() || !target.ok()) return;
        PlotComponent::Series s; s.f = target.hz; s.v = target.db;
        for (auto& db : s.v) db += offset.getValue();
        s.c = C::warning(); s.dash = foh::theme::stroke::dashStored;
        s.label = choice.getText();
        if (band.getToggleState()) {
            PlotComponent::ToleranceBand b; b.f = s.f; b.lower = b.upper = s.v; b.colour = s.c;
            for (auto& db : b.lower) db -= target.toleranceDb > 0 ? target.toleranceDb : 3.0;
            for (auto& db : b.upper) db += target.toleranceDb > 0 ? target.toleranceDb : 3.0;
            plot.toleranceBands.push_back(std::move(b));
        }
        plot.series.push_back(std::move(s));
    }
    void save(Json& j) const {
        j.set("targetChoice", choice.getSelectedId()); j.set("targetShown", shown.getToggleState());
        j.set("targetBand", band.getToggleState()); j.set("targetOffset", offset.getValue());
        if (choice.getSelectedId() == 4) { j.set("targetText", source); j.set("targetName", choice.getText().toStdString()); }
    }
    void load(const Json& j) {
        int id = j["targetChoice"].asInt(1);
        if (id == 4) {
            auto text = j["targetText"].asString(); auto parsed = foh::parseTarget(text);
            if (parsed.ok()) { source = std::move(text); target = std::move(parsed);
                auto name = String(j["targetName"].asString("Imported target"));
                if (name.isEmpty()) name = "Imported target";
                if (choice.getNumItems() == 3) choice.addItem(name, 4);
                else choice.changeItemText(4, name);
                choice.setSelectedId(4, dontSendNotification); }
            else {
                source.clear();
                // Remove the rejected import as well as its visible overlay.
                // It must not remain selectable with empty source text.
                choice.clear(dontSendNotification);
                choice.addItem("Target +6", 1); choice.addItem("Target +9", 2); choice.addItem("Target +12", 3);
                choice.setSelectedId(1, dontSendNotification); selectBuiltin();
                shown.setToggleState(false, dontSendNotification);
                band.setToggleState(false, dontSendNotification);
                changed();
                AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Saved target", String(parsed.error)); return;
            }
        } else { choice.setSelectedId(jlimit(1, 3, id), dontSendNotification); selectBuiltin(); }
        shown.setToggleState(j["targetShown"].asBool(), dontSendNotification);
        band.setToggleState(j["targetBand"].asBool(target.useTolerance), dontSendNotification);
        double value = j["targetOffset"].asNumber(offset.getValue());
        if (std::isfinite(value)) offset.setValue(value, dontSendNotification);
        changed();
    }
private:
    void changed() { if (onChange) onChange(); }
    void selectBuiltin() {
        if (choice.getSelectedId() == 4) {
            target = foh::parseTarget(source);
            band.setToggleState(target.useTolerance, dontSendNotification); return;
        }
        const char* data[] = {BinaryData::targetplus6_txt, BinaryData::targetplus9_txt, BinaryData::targetplus12_txt};
        const int sizes[] = {BinaryData::targetplus6_txtSize, BinaryData::targetplus9_txtSize, BinaryData::targetplus12_txtSize};
        int i = jlimit(0, 2, choice.getSelectedId() - 1);
        target = foh::parseTarget(std::string(data[i], (size_t) sizes[i]));
        band.setToggleState(target.useTolerance, dontSendNotification);
    }
    ComboBox choice; TextButton import {"Import"}; ToggleButton shown, band; Slider offset;
    foh::TargetTrace target; std::string source; std::unique_ptr<FileChooser> chooser;
};

// ===================== Transfer function (magnitude) =====================
class TransferFunctionPanel : public Panel {
public:
    explicit TransferFunctionPanel(AppModel& m) : Panel(m, "TransferFunction", "Transfer Function") {
        helpText = "Settled: less than 0.5 dB RMS change for 3 consecutive updates, with at least 25% coherent bins (100 Hz-10 kHz). After 10 s, capture proceeds with an unsettled warning.\n\nThis compares the console (reference) to the mic. Anything that isn't flat is what the speakers + room are doing to your mix.\n\nMagnitude: 0 dB = unchanged.\n\nDimmed regions: coherence below the Dim threshold. Don't EQ there; move the mic or treat the reflection.\n\nStored traces are dashed.\n\nMulti-mic: move to each of 2-3 positions, Reset avg and wait for a fresh trace. Enter the total arrival delay (or Use found), then Capture. The aligned average uses coherence weights and can be compared with the target. Capture again to replace a position; Clear starts a new set.";
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
        addAndMakeVisible(target); target.onChange = [this] { modelChanged(AppModel::Overlay); };
        for (int i = 1; i <= 3; ++i) position.addItem("Position " + String(i), i);
        position.setSelectedId(1, dontSendNotification);
        positionDelay.setRange(-500, 3000, 0.001); positionDelay.setTextValueSuffix(" ms");
        positionDelay.setSliderStyle(Slider::IncDecButtons);
        positionDelay.setTextBoxStyle(Slider::TextBoxLeft, false, 94, 24);
        positionDelay.setTooltip("Total arrival delay for this position. Enter it or use the delay finder result. Reference compensation is subtracted automatically.");
        position.onChange = [this] { positionDelay.setValue(positionDelays[(size_t)(position.getSelectedId() - 1)], dontSendNotification); };
        positionDelay.onValueChange = [this] { positionDelays[(size_t)(position.getSelectedId() - 1)] = positionDelay.getValue(); };
        useDelay.onClick = [this] { if (model.delayLocked()) positionDelay.setValue(model.delay.arrivals[0].delayMs); };
        capturePosition.onClick = [this] { model.send(foh::Command::micPosition(position.getSelectedId() - 1, positionDelay.getValue())); };
        clearPositions.onClick = [this] { model.send(foh::Command::clearMicPositions()); };
        showPositions.setButtonText("Multi-mic"); showPositions.setToggleState(true, dontSendNotification); showPositions.onClick = [this] { modelChanged(AppModel::Snapshot); };
        capturePosition.setTooltip("Move the mic, Reset avg, wait for a fresh settled trace, then capture this position. Capture again to replace it.");
        for (auto* c : std::initializer_list<Component*>{&position, &positionDelay, &useDelay, &capturePosition, &clearPositions, &showPositions}) addAndMakeVisible(c);
        syncControls();
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::plotPanelMinHeight + 64 }; }
    int priority() const override { return 100; }
    Json saveState() const override { Json j = Json::object(); j.set("yMin", plot.yMin); j.set("yMax", plot.yMax); target.save(j); return j; }
    // State loads while detached/hidden defer plot work. Refresh on reattachment
    // even when the measurement is paused and no new snapshots arrive.
    void visibilityChanged() override { modelChanged(AppModel::Overlay); }
    void parentHierarchyChanged() override { visibilityChanged(); }
    void loadState(const Json& j) override { if (j.has("yMin")) { plot.yMin = j["yMin"].asNumber(-12); plot.yMax = j["yMax"].asNumber(12); plot.yStep = (plot.yMax - plot.yMin) / 4; } target.load(j); }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto row = controlRow(r);
        store.setBounds(row.removeFromRight(64)); row.removeFromRight(6); reset.setBounds(row.removeFromRight(84)); row.removeFromRight(6);
        avg.setBounds(row.removeFromRight(92)); row.removeFromRight(6); smooth.setBounds(row.removeFromRight(118)); row.removeFromRight(6); blank.setBounds(row.removeFromRight(108));
        auto multiRow = controlRow(r);
        position.setBounds(multiRow.removeFromLeft(100)); positionDelay.setBounds(multiRow.removeFromLeft(140));
        useDelay.setBounds(multiRow.removeFromLeft(86));
        multiRow = controlRow(r);
        capturePosition.setBounds(multiRow.removeFromLeft(68));
        clearPositions.setBounds(multiRow.removeFromLeft(54)); showPositions.setBounds(multiRow);
        target.setBounds(controlRow(r)); plot.setBounds(r);
    }
    void syncControls() { smooth.setSelectedId(model.smoothingFraction + 1, dontSendNotification); avg.setSelectedId(model.averages == 0 ? 1000 : model.averages, dontSendNotification);
                          blank.setSelectedId(roundToInt(model.blankThreshold * 100), dontSendNotification); }
    void modelChanged(int w) override {
        if (w & AppModel::Settings) syncControls();
        if (!(w & (AppModel::Snapshot | AppModel::Traces | AppModel::Overlay | AppModel::Theme | AppModel::Capture)) || !isShowing()) return;
        plot.series.clear(); plot.shade.clear(); plot.shadeLabel = {};
        auto& ov = model.overlay;
        if (ov.active && !ov.mag.empty()) { plot.series = ov.mag; plot.shade = ov.shade; plot.shadeLabel = ov.shadeLabel; }
        else { addStoredTraces(model, plot, true);
               PlotComponent::Series s; s.c = C::traceLive(); s.label = "Live"; logDecimate(model.tf, model.tf.magDb, s.f, s.v, &s.gap); plot.series.push_back(s); }
        if (showPositions.getToggleState() && model.positionCount >= 2) {
            PlotComponent::Series s; s.c = C::storedTrace(3); s.label = "Multi-mic average";
            logDecimate(model.multiPosition, model.multiPosition.magDb, s.f, s.v, &s.gap);
            plot.series.push_back(std::move(s));
        }
        capturePosition.setEnabled(model.tf.averages > 0 && !model.paused);
        useDelay.setEnabled(model.delayLocked());
        for (int i = 1; i <= 3; ++i) position.changeItemText(i, "Position " + String(i) + ((model.positionMask & (1 << (i - 1))) ? " *" : ""));
        target.appendTo(plot);
        plot.lowCoh = blankedRegions(model.tf);
        plot.note = "averages " + String(model.tf.averages) + (model.calApplied ? u8(" \xc2\xb7 cal applied") : "")
                    + u8(" \xc2\xb7 ") + u8(fohui::settleStatusText(model.conv, model.capture.active, model.capture.unsettled))
                    + " | Multi-mic: " + String(model.positionCount) + "/3 positions (2 required)";
        plot.repaint();
    }
private:
    ComboBox position; Slider positionDelay; std::array<double, 3> positionDelays {};
    TextButton useDelay {"Use found"}, capturePosition {"Capture"}, clearPositions {"Clear"}; ToggleButton showPositions;
    TargetControls target; PlotComponent plot; ComboBox smooth, avg, blank; TextButton reset {"Reset avg"}, store {"Store"};
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
        helpText = "Coherence: how much to trust the trace (0 to 1).\n\nGood: 0.9 or above. Fair: 0.6 up to 0.9. Poor: below 0.6. Low coherence is usually a reflection or noise: don't EQ it.";
        plot.yLabel = "COHERENCE"; plot.yMin = 0; plot.yMax = 1; plot.yStep = 0.5; addAndMakeVisible(plot);
    }
    fohws::Size minSize() const override { return { foh::theme::ws::panelMinWidth, 90 }; } // Alignment gives it a 0.15 strip: the traffic light alone is useful there
    void resized() override { auto r = getLocalBounds().reduced(pad, 6); light = r.removeFromTop(22); plot.setVisible(r.getHeight() > 60); plot.setBounds(r); }
    void paint(Graphics& g) override { // traffic light: shape + word, never colour only
        auto cl = foh::classifyCoherence(model.tf); double mean = cl.meanCoherence; int state = cl.state();
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
        plot.yLabel = "dBFS"; plot.yMin = -100; plot.yMax = 0; plot.yStep = 20; plot.bars = true; plot.showLegend = true; addAndMakeVisible(plot);
        frac.addItem("1/3 octave", 3); frac.addItem("1/6 octave", 6); frac.setSelectedId(model.rtaFraction, dontSendNotification);
        frac.onChange = [this] { model.setRtaFraction(frac.getSelectedId()); }; addAndMakeVisible(frac);
        addAndMakeVisible(target); target.onChange = [this] { modelChanged(AppModel::Overlay); };
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::plotPanelMinHeight }; }
    int priority() const override { return 90; }
    Json saveState() const override { Json j = Json::object(); j.set("fraction", model.rtaFraction); target.save(j); return j; }
    void visibilityChanged() override { modelChanged(AppModel::Overlay); }
    void parentHierarchyChanged() override { visibilityChanged(); }
    void loadState(const Json& j) override { if (j.has("fraction")) { int f = j["fraction"].asInt(3); if (f != model.rtaFraction) model.setRtaFraction(f); } target.load(j); }
    void resized() override { auto r = getLocalBounds().reduced(pad, 6); auto row = controlRow(r); frac.setBounds(row.removeFromRight(120)); target.setBounds(controlRow(r)); plot.setBounds(r); }
    void modelChanged(int w) override {
        if (w & AppModel::Settings) frac.setSelectedId(model.rtaFraction, dontSendNotification);
        if (!(w & (AppModel::Snapshot | AppModel::Theme | AppModel::Overlay | AppModel::Cal | AppModel::Settings)) || !isShowing()) return;
        const auto spl = model.splCal();
        plot.yLabel = spl.unit(); plot.yMin = spl.calibrated ? 0 : -100; plot.yMax = spl.calibrated ? 140 : 0;
        plot.series.clear(); plot.barLo.clear(); plot.barHi.clear(); PlotComponent::Series s; s.c = C::accent(); s.label = "Live";
        for (size_t i = 0; i < model.rtaDb.size() && i < model.rtaBands.size(); ++i) { auto& b = model.rtaBands[i]; plot.barLo.push_back(b.fLo); plot.barHi.push_back(b.fHi); s.f.push_back(b.fCenter); s.v.push_back(spl.level(model.rtaDb[i], model.splSensitivity())); }
        plot.series.push_back(s); target.appendTo(plot); plot.note = model.rtaDb.empty() ? "No valid spectrum" : (model.calApplied ? "cal applied" : ""); plot.repaint();
    }
private:
    TargetControls target {-40}; PlotComponent plot; ComboBox frac;
};

// ===================== W7 show-time monitoring =====================
class ShowMonitoringPanel : public Panel, private Timer {
public:
    explicit ShowMonitoringPanel(AppModel& m) : Panel(m, "ShowMonitoring", "W7 Show Monitoring") {
        helpText = "Feed the console programme into the reference input and leave the mic in its reference position. Select a stored TF, then Monitor. No generated output is allowed while monitoring. TF drift is RMS over 100 Hz-10 kHz, using coherence >=0.6 and at least 25% coverage. A 3 dB change for three one-second updates raises an alert. RTA shows live programme level; programme changes are not system drift.";
        reference.setTextWhenNothingSelected("Select stored reference");
        monitor.setTooltip(helpText);
        reference.onChange = [this] { stopMonitoring(); };
        monitor.onClick = [this] {
            if (active) { stopMonitoring(); return; }
            int index = reference.getSelectedId() - 1;
            if (index < 0 || index >= model.traces.count()) return;
            baseline = model.traces.at(index); // stable even if the trace store changes
            active = true; alert = {}; model.engine.gen = AudioEngine::Off;
            model.engine.monitoringSilent = true; model.engine.cancelConnectionTest();
            model.notify(AppModel::Settings); monitor.setButtonText("Stop monitoring");
            reference.setEnabled(false); status.setText("Monitoring: waiting for coherent programme", dontSendNotification);
            startTimerHz(1);
        };
        for (auto* c : std::initializer_list<Component*>{&reference, &monitor, &status, &tfPlot, &rtaPlot}) addAndMakeVisible(c);
        tfPlot.yLabel = "TF DB"; tfPlot.yMin = -18; tfPlot.yMax = 18; tfPlot.yStep = 6; tfPlot.showLegend = true;
        rtaPlot.yLabel = "RTA dBFS"; rtaPlot.yMin = -100; rtaPlot.yMax = 0; rtaPlot.yStep = 20; rtaPlot.showLegend = true;
        styleLabel(status, T::small, C::textPrimary()); refreshReferences(); stopMonitoring();
    }
    ~ShowMonitoringPanel() override { stopTimer(); if (active) { model.engine.gen = AudioEngine::Off; model.engine.monitoringSilent = false; } }
    int priority() const override { return 100; }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto row = controlRow(r);
        monitor.setBounds(row.removeFromRight(140)); row.removeFromRight(6); reference.setBounds(row);
        status.setBounds(controlRow(r, 44)); tfPlot.setBounds(r.removeFromTop(r.getHeight() / 2)); rtaPlot.setBounds(r);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Session) { stopMonitoring(); reference.setSelectedId(0, dontSendNotification); refreshReferences(); }
        if ((w & AppModel::Traces) && !active) refreshReferences();
    }
private:
    void refreshReferences() {
        auto id = reference.getSelectedId(); reference.clear(dontSendNotification);
        for (int i = 0; i < model.traces.count(); ++i) reference.addItem(String(model.traces.at(i).name), i + 1);
        if (id <= model.traces.count()) reference.setSelectedId(id, dontSendNotification);
        monitor.setEnabled(active || reference.getSelectedId() > 0);
    }
    void stopMonitoring() {
        stopTimer(); if (active) { model.engine.gen = AudioEngine::Off; model.engine.monitoringSilent = false; }
        active = false; alert = {}; monitor.setButtonText("Monitor"); reference.setEnabled(true); refreshReferences();
        status.setColour(Label::textColourId, C::textPrimary());
        status.setText("W7: select a stored TF and Monitor. Use console programme as reference.", dontSendNotification);
    }
    void timerCallback() override {
        if (!active) return;
        const bool signal = !model.paused && model.micLevelValid && model.refPeak > 0.00001f
            && !model.engine.deviceFailed.load() && model.engine.state.load() == AudioEngine::Ready;
        const auto drift = signal ? fohui::showDrift(model.tf, baseline.tf) : fohui::ShowDrift{};
        const bool warning = alert.update(drift);
        status.setText(!drift.valid ? "Waiting: insufficient coherent programme, paused, or input changed"
            : (warning ? "DRIFT ALERT: " : alert.consecutive > 0 ? "Checking drift: " : "Monitoring: ")
              + String(drift.rmsDb, 1) + " dB RMS vs " + String(baseline.name), dontSendNotification);
        status.setColour(Label::textColourId, warning ? Colours::orange : C::textPrimary());
        if (!isShowing()) return; // keep alert state, avoid hidden rendering
        tfPlot.series.clear();
        PlotComponent::Series stored; stored.c = C::storedTrace(0); stored.label = String(baseline.name); stored.dash = foh::theme::stroke::dashStored;
        logDecimate(baseline.tf, baseline.tf.magDb, stored.f, stored.v, &stored.gap); tfPlot.series.push_back(std::move(stored));
        if (signal) { PlotComponent::Series live; live.c = C::traceLive(); live.label = "Live TF";
            logDecimate(model.tf, model.tf.magDb, live.f, live.v, &live.gap); tfPlot.series.push_back(std::move(live)); }
        tfPlot.repaint(); rtaPlot.series.clear();
        PlotComponent::Series rta; rta.c = C::accent(); rta.label = "Live programme";
        if (signal) for (size_t i = 0; i < model.rtaDb.size() && i < model.rtaBands.size(); ++i) {
            rta.f.push_back(model.rtaBands[i].fCenter); rta.v.push_back(model.rtaDb[i]);
        }
        rtaPlot.series.push_back(std::move(rta)); rtaPlot.repaint();
    }
    ComboBox reference; TextButton monitor {"Monitor"}; Label status;
    PlotComponent tfPlot, rtaPlot; foh::Trace baseline; fohui::ShowDriftAlert alert; bool active = false;
};

// ===================== Delay finder =====================
class DelayFinderPanel : public Panel {
public:
    explicit DelayFinderPanel(AppModel& m) : Panel(m, "DelayFinder", "Delay Finder") {
        helpText = "The delay finder lines up the reference with the mic (cross-correlation in fohcore).\n\nInsert delay applies it to the reference so the transfer function phase flattens.\n\nTwo similar spikes: arrivals within 3 dB and more than 0.5 ms apart. Check that only one speaker is on.\n\nConfidence 0 means no lock: check signal and that only one speaker is on.";
        value.setFont(FohLookAndFeel::numeric(34)); value.setText("--", dontSendNotification);
        styleLabel(detail, T::small, C::textSecondary()); detail.setJustificationType(Justification::topLeft); detail.setText("Press Find", dontSendNotification);
        styleLabel(twoSpikeBadge, T::small, C::warning(), true);
        twoSpikeBadge.setText(u8("\xe2\x96\xb2 Two similar spikes"), dontSendNotification);
        twoSpikeBadge.setColour(Label::backgroundColourId, C::warning().withAlpha(0.12f));
        twoSpikeBadge.setColour(Label::outlineColourId, C::warning());
        twoSpikeBadge.setTooltip("Two arrivals within 3 dB and more than 0.5 ms apart: only one speaker on? Confidence is multiplied by 0.75 in the delay finder.");
        addChildComponent(twoSpikeBadge);
        makePrimary(insert); find.onClick = [this] { model.requestDelay(sub.getToggleState()); }; insert.onClick = [this] { model.lockDelayFromLast(); };
        sub.setButtonText("Sub (LF)");
        for (auto* c : std::initializer_list<Component*>{&value, &detail, &find, &insert, &sub}) addAndMakeVisible(c);
        if (model.delayPending || model.delayLocked()) modelChanged(AppModel::Delay);
    }
    fohws::Size minSize() const override { return { 300, 150 }; }
    int priority() const override { return 70; }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto right = r.removeFromRight(110);
        insert.setBounds(right.removeFromTop(28)); right.removeFromTop(6); find.setBounds(right.removeFromTop(28)); right.removeFromTop(4); sub.setBounds(right.removeFromTop(22));
        if (twoSpikeBadge.isVisible()) { twoSpikeBadge.setBounds(r.removeFromTop(22)); r.removeFromTop(4); }
        value.setBounds(r.removeFromTop(44)); detail.setBounds(r);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Theme) {
            makePrimary(insert); detail.setColour(Label::textColourId, C::textSecondary());
            twoSpikeBadge.setColour(Label::textColourId, C::warning());
            twoSpikeBadge.setColour(Label::backgroundColourId, C::warning().withAlpha(0.12f));
            twoSpikeBadge.setColour(Label::outlineColourId, C::warning());
        }
        if (!(w & (AppModel::Delay | AppModel::Theme))) return;
        auto& d = model.delay;
        twoSpikeBadge.setVisible(!model.delayPending && model.delayLocked() && d.twoSimilarArrivals);
        resized();
        if (model.delayPending) { detail.setText(model.delayWasSub ? "Finding sub delay (3 s window)..." : "Finding delay...", dontSendNotification); return; }
        if (!model.delayLocked()) { value.setText("--", dontSendNotification); detail.setText(d.arrivals.empty() ? "No signal: is pink noise playing?" : "No valid delay result: check signal and that only one speaker is on", dontSendNotification); return; }
        auto& a0 = d.arrivals[0];
        value.setText(String(consoleMs(a0.delayMs), 2) + " ms", dontSendNotification);
        // The core has already applied the two-spike penalty; display it only once.
        String t = String(d.confidence > 0.7 ? "High" : d.confidence > 0.35 ? "Medium" : "LOW") + " confidence " + String(roundToInt(d.confidence * 100.0)) + "%"
                 + (d.twoSimilarArrivals ? u8(" (\xc3\x97" "0.75)") : "")
                 + "\npolarity " + (a0.inverted ? "INVERTED" : "normal")
                 + u8("\n\xe2\x89\x88 ") + String(a0.distanceM, 2) + " m" + (model.delayWasSub ? " (sub, LF-weighted)" : "");
        for (size_t i = 1; i < d.arrivals.size(); ++i) t << u8("  \xc2\xb7  #") << (int) i + 1 << " " << String(consoleMs(d.arrivals[i].delayMs), 2) << " ms (" << String(d.arrivals[i].relLevelDb, 1) << " dB)";
        detail.setText(t, dontSendNotification);
    }
private:
    Label value, detail, twoSpikeBadge; TextButton find {"Find"}, insert {"Insert delay"}; ToggleButton sub;
    TooltipWindow tooltip { nullptr, 700 };
};

// ===================== Impulse =====================
class ImpulsePanel : public Panel {
public:
    explicit ImpulsePanel(AppModel& m) : Panel(m, "Impulse", "Impulse") {
        helpText = "Measure a 5-second log sweep with a safety ramp, followed by silence for decay. The impulse is deconvolved against the recorded loopback reference. Start at a low generator level and check your routing. ETC shows envelope energy relative to its peak.";
        addAndMakeVisible(imp); addAndMakeVisible(measure); addAndMakeVisible(etc); addAndMakeVisible(status);
        measure.onClick = [this] { foh::Command c; c.type = foh::CommandType::MeasureSweep; model.send(c); };
        etc.onClick = [this] { imp.showEtc = etc.getToggleState(); imp.repaint(); };
    }
    Json saveState() const override { Json j = Json::object(); j.set("postMs", imp.postMs); j.set("etc", etc.getToggleState()); return j; }
    void loadState(const Json& j) override { imp.postMs = j["postMs"].asNumber(45); etc.setToggleState(j["etc"].asBool(false), dontSendNotification); imp.showEtc = etc.getToggleState(); }
    fohws::Size minSize() const override { return { 300, 180 }; }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6); auto row = r.removeFromTop(26);
        measure.setBounds(row.removeFromLeft(150)); etc.setBounds(row.removeFromLeft(75));
        status.setBounds(r.removeFromTop(22)); imp.setBounds(r);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Snapshot | AppModel::Delay | AppModel::Theme))) return;
        measure.setEnabled(!model.sweepCapturing);
        String note = "Delay correlation";
        if (model.sweepCapturing) note = "Measuring sweep...";
        else if (!model.sweep.response.error.empty()) note = String(model.sweep.response.error);
        else if (model.sweep.response.ok()) note = "Sweep impulse: " + String(model.sweep.response.arrivals[0].delayMs, 2) + " ms";
        status.setText(note, dontSendNotification);
        etc.setEnabled(!model.sweep.etcDb.empty());
        imp.etcDb = model.sweep.etcDb;
        imp.setResult(model.sweep.response.impulse.empty() ? model.delay : model.sweep.response, model.sampleRate);
    }
private:
    ImpulseComponent imp;
    TextButton measure {"Measure sweep"}; ToggleButton etc {"ETC"}; Label status;
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
            String st = db > -1 ? u8("\xe2\x96\xa0 CLIP") : i == 0 && model.splCal().calibrated ? (model.micLevelReady() ? String(model.micLevel(), 1) + " dB SPL" : String("-- dB SPL")) : db > -40 ? u8("\xe2\x97\x8f ") + String(db, 1) + " dBFS" : u8("\xe2\x96\xb2 no signal");
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::numeric(T::small, false)); g.drawText(st, val, Justification::centredRight); }
        g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::axisTick)); g.drawText("target -20 dBFS on pink noise", r.removeFromTop(16), Justification::left);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Levels | AppModel::Theme | AppModel::Cal | AppModel::Settings))) return;
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
        helpText = "Pink noise for the transfer function and RTA; the log sweep lasts 5 s followed by a 1 s silence gap.\n\nOut 1 goes to the console, Out 2 loops back to In 2 as the reference.";
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
    explicit InputsAndDevicesPanel(AppModel& m) : Panel(m, "InputsAndDevices", "Inputs & Devices"), setup(m, false) {
        helpText = "Select your input and output devices, sample rate, microphone and reference inputs, and generator outputs. Play a short chime to check the outputs. Use Audio Setup in the toolbar and Done to save these settings.";
        addAndMakeVisible(setup);
    }
    fohws::Size minSize() const override { return { 360, 420 }; }
    void resized() override { setup.setBounds(getLocalBounds()); }
    Json saveState() const override { Json j = Json::object(); j.set("mic", model.engine.micCh.load()); j.set("ref", model.engine.refCh.load()); j.set("outA", model.engine.outA.load()); j.set("outB", model.engine.outB.load()); return j; }
    void loadState(const Json& j) override {
        if (!j.isObject()) return;
        model.engine.micCh = j["mic"].asInt(0);
        model.engine.refCh = j["ref"].asInt(1);
        model.engine.outA = j["outA"].asInt(0);
        model.engine.outB = j["outB"].asInt(1);
        // Refresh every SetupPage only after the complete routing map is restored.
        model.notify(AppModel::Settings);
    }
private:
    SetupPage setup;
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
        helpText = "Up to 8 stored traces. Select a row to edit its name or colour; click the box to show/hide it. Choose two sources for a complex virtual sum preview, including the same trace twice. The preview does not store or change measurements.";
        list.setModel(this); list.setRowHeight(24);
        name.setTextToShowWhenEmpty("Select a trace to rename", C::textAxis());
        name.onReturnKey = [this] { model.renameTrace(list.getSelectedRow(), name.getText()); };
        rename.onClick = name.onReturnKey;
        const char* names[] = {"Red", "Orange", "Yellow", "Green", "Cyan", "Blue", "Purple", "Pink"};
        for (int i = 0; i < 8; ++i) colour.addItem(names[i], i + 1);
        colour.setTextWhenNothingSelected("Colour");
        colour.onChange = [this] { if (colour.getSelectedId() > 0) model.colourTrace(list.getSelectedRow(), palette(colour.getSelectedId() - 1)); };
        del.onClick = [this] { model.removeTrace(list.getSelectedRow()); };
        sourceA.setTextWhenNothingSelected("Sum source A"); sourceB.setTextWhenNothingSelected("Sum source B");
        sourceA.onChange = sourceB.onChange = [this] { model.selectSum(sourceA.getSelectedId() - 1, sourceB.getSelectedId() - 1); };
        preview.yLabel = "SUM DB"; preview.yMin = -12; preview.yMax = 12; preview.yStep = 6; preview.showLegend = true;
        for (auto* c : std::initializer_list<Component*>{&list, &name, &rename, &colour, &del, &sourceA, &sourceB, &preview}) addAndMakeVisible(c);
        modelChanged(AppModel::Traces);
    }
    ~TraceListPanel() override { list.setModel(nullptr); }
    fohws::Size minSize() const override { return {300, 420}; }
    int priority() const override { return 5; }
    void resized() override {
        auto r = getLocalBounds().reduced(pad, 6);
        list.setBounds(r.removeFromTop(120)); r.removeFromTop(6);
        auto row = controlRow(r); rename.setBounds(row.removeFromRight(76)); row.removeFromRight(6); name.setBounds(row);
        row = controlRow(r); del.setBounds(row.removeFromRight(110)); row.removeFromRight(6); colour.setBounds(row);
        row = controlRow(r); sourceA.setBounds(row.removeFromLeft(row.getWidth() / 2)); row.removeFromLeft(6); sourceB.setBounds(row);
        preview.setBounds(r);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Traces | AppModel::Theme))) return;
        list.updateContent(); list.repaint(); selectedRowsChanged(list.getSelectedRow());
        sourceA.clear(dontSendNotification); sourceB.clear(dontSendNotification);
        for (int i = 0; i < model.traces.count(); ++i) {
            auto title = String(i + 1) + ": " + String(model.traces.at(i).name);
            sourceA.addItem(title, i + 1); sourceB.addItem(title, i + 1);
        }
        sourceA.setSelectedId(model.sumA + 1, dontSendNotification); sourceB.setSelectedId(model.sumB + 1, dontSendNotification);
        auto sum = model.sumPreview(); preview.series.clear();
        preview.centreNote = sum.freq.empty() ? (model.validTrace(model.sumA) && model.validTrace(model.sumB)
            ? "Sources need matching frequency grids and complete TF data" : "Choose two stored traces to preview their sum") : String{};
        preview.note = {};
        if (!sum.freq.empty()) {
            PlotComponent::Series s; s.c = C::accent(); s.label = "Virtual sum";
            logDecimate(sum, sum.magDb, s.f, s.v, &s.gap); preview.series.push_back(std::move(s));
            // A numerical readout makes the preview useful even in a small panel.
            auto bin = std::min_element(sum.freq.begin(), sum.freq.end(), [](double a, double b) { return std::abs(a - 1000) < std::abs(b - 1000); });
            auto k = (size_t) std::distance(sum.freq.begin(), bin);
            preview.note = String(*bin, 0) + " Hz: " + (sum.magDb[k] >= 0 ? "+" : "") + String(sum.magDb[k], 2) + " dB";
        }
        preview.repaint();
    }
    int getNumRows() override { return model.traces.count(); }
    void paintListBoxItem(int row, Graphics& g, int w, int h, bool sel) override {
        if (!model.validTrace(row)) return; auto& t = model.traces.at(row);
        if (sel) g.fillAll(C::navItemActiveBg());
        auto box = Rectangle<float>(6, h / 2.f - 6, 12, 12); g.setColour(model.traceColour(row)); g.drawRect(box, 1.f); if (t.visible) g.fillRect(box.reduced(3));
        g.setColour(t.visible ? C::textPrimary() : C::textAxis()); g.setFont(FohLookAndFeel::font(T::small));
        g.drawText(String(t.name) + "   ref delay " + String(t.refDelayMs, 2) + " ms", 26, 0, w - 30, h, Justification::centredLeft);
    }
    void selectedRowsChanged(int row) override {
        bool valid = model.validTrace(row);
        name.setEnabled(valid); rename.setEnabled(valid); colour.setEnabled(valid); del.setEnabled(valid);
        name.setText(valid ? String(model.traces.at(row).name) : String{}, false);
        int id = 0;
        if (valid) for (int i = 0; i < 8; ++i) if (model.traceColour(row) == palette(i)) id = i + 1;
        colour.setSelectedId(id, dontSendNotification);
    }
    void listBoxItemClicked(int row, const MouseEvent& e) override { if (e.x < 24) model.toggleTrace(row); }
private:
    static Colour palette(int i) { return Colour::fromHSV((float) i / 8.0f, 0.7f, 0.9f, 1.0f); }
    ListBox list; TextEditor name; ComboBox colour, sourceA, sourceB; TextButton rename {"Rename"}, del {"Delete trace"}; PlotComponent preview;
};

// ===================== Indicative SPL / Leq =====================
class SplLeqPanel : public Panel {
public:
    explicit SplLeqPanel(AppModel& m) : Panel(m, "SPLNumbers", "SPL / Leq (indicative)") {
        addAndMakeVisible(weighting); weighting.addItemList({"A", "C", "Z"}, 1); weighting.setSelectedId(1);
        addAndMakeVisible(preset); preset.addItemList({"WHO", "NIOSH"}, 1); preset.setSelectedId(1);
        weighting.onChange = preset.onChange = [this] { repaint(); };
        helpText = "Indicative calibrated SPL. Leq averages energy, with rolling 1 min and 15 min windows in 100 ms buckets. Before a window fills it uses elapsed measured time. Session starts with the input and resets on input or calibration changes. Paused audio is excluded. Warnings always use A weighting. WHO: 100 dBA LAeq-15. NIOSH: 85 dBA for 8 h, 3 dB exchange. This is not a certified sound level meter.";
    }
    fohws::Size minSize() const override { return {230, 240}; }
    void resized() override { auto r=getLocalBounds().reduced(pad).removeFromTop(26); weighting.setBounds(r.removeFromLeft(70)); r.removeFromLeft(8); preset.setBounds(r); }
    void modelChanged(int) override { repaint(); }
    void paint(Graphics& g) override {
        g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::font(T::small));
        auto r=getLocalBounds().reduced(pad); r.removeFromTop(34);
        auto line=[&](const String& text) { g.drawText(text,r.removeFromTop(24),Justification::centredLeft); };
        const auto c=model.splCal();
        if (!c.calibrated) { line("Calibrate SPL in Setup"); return; }
        if (!model.micLevelReady() || model.leqRevision != model.analysis.leqRevision.load() || !model.leq.valid) { line("-- SPL / Leq (invalid or no live signal)"); return; }
        size_t w=static_cast<size_t>(weighting.getSelectedId()-1);
        auto level=[&](double db) { return c.level(db-(model.calOn ? model.cal.dbAt(1000) : 0), model.splSensitivity()); };
        auto value=[&](const char* label, double db) { line(String(label)+String(level(db),1)+" dB"+weighting.getText()); };
        value("SPL  ",model.leq.level[w]); value("Leq 1 min  ",model.leq.minute[w]);
        value("Leq 15 min  ",model.leq.fifteenMinutes[w]); value("Leq session  ",model.leq.session[w]);
        line("Measured " + String(model.leq.seconds,1) + " s" + (model.leq.seconds<900 ? " (partial windows)" : ""));
        auto selected=preset.getSelectedId()==1 ? foh::ExposurePreset::WHO : foh::ExposurePreset::NIOSH;
        auto warning=foh::exposureWarning(selected,level(model.leq.fifteenMinutes[0]),level(model.leq.session[0]),model.leq.seconds);
        if (model.micPeak>=0.99f) line("WARNING: input clipping");
        else if (warning.exceeded) line("WARNING: " + preset.getText() + " limit reached");
        else line(preset.getText()+" limit not reached");
        line(selected==foh::ExposurePreset::WHO ? "100 dBA Leq-15" : "85 dBA / 8 h, 3 dB exchange");
    }
private:
    ComboBox weighting, preset;
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
        add("ShowMonitoring", "W7 Show Monitoring", [](AppModel& m) { return std::make_unique<ShowMonitoringPanel>(m); });
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
        ph("LimitPreset", "Limit Preset", "SPL limit preset for exposure monitoring.");
        ph("EQSuggestions", "EQ Suggestions", "Suggested EQ moves from the transfer function, coherence-weighted.");
        ph("SPLHistory", "SPL History", "SPL over time with LAeq / LCeq traces.");
        add("SPLNumbers", "SPL / Leq", [](AppModel& m) { return std::make_unique<SplLeqPanel>(m); });
        ph("Dose", "Dose", "Noise dose against the selected limit.");
        ph("Events", "Events", "Log of clipping, limit breaches and captures.");
        return x; }();
    return r;
}
