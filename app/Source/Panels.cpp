#include "Strings.h"
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

static void styleLabel(Label& l, T::Role size, Colour c, bool bold = false) { l.setFont(FohLookAndFeel::font(size, bold)); l.setColour(Label::textColourId, c); }
static const foh::theme::TraceSlot& traceSlot(const AppModel& model, int row) {
    const auto& slots = foh::ui::theme().traceSlots;
    for (const auto& slot : slots) if (slot.colour == model.traceColour(row)) return slot;
    return slots[(size_t) model.traceSlotIndex(row)];
}
static Rectangle<int> controlRow(Rectangle<int>& r, int h = 26) { auto row = r.removeFromTop(h); r.removeFromTop(6); return row; }

// ---- helpers shared by the plot panels ----
static void addStoredTraces(const AppModel& m, PlotComponent& p, bool mag) {
    for (int i = 0; i < m.traces.count(); ++i) { auto& t = m.traces.at(i); if (!t.visible) continue;
        PlotComponent::Series s; s.c = m.traceColour(i); const auto& slot = traceSlot(m, i); s.dash = slot.dashCount ? slot.dash.data() : nullptr;
        s.dashCount = slot.dashCount; s.width = D::traceStored(); s.label = String(t.name);
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
        offset.setRange(-100, 100, 0.5); offset.setTextValueSuffix(" dB");
        offset.setSliderStyle(Slider::IncDecButtons); offset.setTextBoxStyle(Slider::TextBoxLeft, false, 106, 24);
        offset.setValue(initialOffset, dontSendNotification);
        offset.setTooltip("Shift the relative target to the measured level; does not change measurement data.");
        choice.onChange = [this] { selectBuiltin(); if (!shown.isVisible()) shown.setToggleState(true, dontSendNotification); changed(); };
        shown.onClick = band.onClick = [this] { changed(); };
        offset.onValueChange = [this] { changed(); };
        import.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>(juce::String(juce::String::fromUTF8("Import target curve\xe2\x80\xa6")),
                juce::File(), juce::String("*.txt;*.asc;*.csv"));
            const juce::Component::SafePointer<TargetControls> safe(this);
            chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                [safe](const juce::FileChooser& fc) {
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
        for (auto* c : std::initializer_list<Component*>{&choice, &import, &shown, &band, &offset}) editor.addAndMakeVisible(c);
        addChildComponent(editor); editor.setSize(320, 152);
        addAndMakeVisible(disclosure);
        disclosure.onClick = [this] {
            struct Content : Component {
                explicit Content(TargetControls& owner) : safe(&owner) {
                    addAndMakeVisible(owner.editor); setSize(320, 152);
                }
                ~Content() override { if (safe) { safe->addChildComponent(safe->editor); safe->editor.setVisible(false); } }
                void resized() override { if (safe) { safe->editor.setBounds(getLocalBounds()); safe->layoutEditor(); } }
                Component::SafePointer<TargetControls> safe;
            };
            if (callout) { callout->dismiss(); return; }
            callout = &CallOutBox::launchAsynchronously(std::make_unique<Content>(*this), disclosure.getScreenBounds(), nullptr);
            callout->setLookAndFeel(&getLookAndFeel());
        };
        selectBuiltin();
    }
    void hideTargetToggle() { shown.setVisible(false); }
    ~TargetControls() override { if (callout) { callout->setLookAndFeel(nullptr); callout->dismiss(); } }
    std::function<void()> onChange;
    void resized() override { disclosure.setBounds(getLocalBounds().withWidth(jmin(88, getWidth()))); layoutEditor(); }
    void layoutEditor() {
        auto r = editor.getLocalBounds().reduced(12);
        auto row = r.removeFromTop(28); choice.setBounds(row.removeFromLeft(132)); row.removeFromLeft(8); import.setBounds(row);
        r.removeFromTop(8); row = r.removeFromTop(28); if (shown.isVisible()) { shown.setBounds(row.removeFromLeft(120)); row.removeFromLeft(8); } band.setBounds(row);
        r.removeFromTop(8); offset.setBounds(r.removeFromTop(28));
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
            for (auto& db : b.lower) db -= target.hasTolerance ? target.toleranceDb : 3.0;
            for (auto& db : b.upper) db += target.hasTolerance ? target.toleranceDb : 3.0;
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
    Component::SafePointer<CallOutBox> callout;
    Component editor; TextButton disclosure {u8("Target \xe2\x96\xbe")};
    ComboBox choice; TextButton import {u8("Import target curve\xe2\x80\xa6")}; ToggleButton shown, band; Slider offset;
    foh::TargetTrace target; std::string source; std::unique_ptr<FileChooser> chooser;
};

// ===================== Transfer function (magnitude) =====================
class TransferFunctionPanel : public Panel {
public:
    explicit TransferFunctionPanel(AppModel& m) : Panel(m, "TransferFunction", "Transfer function") {
        helpText = "This plot shows how the speakers and room change the reference signal at the mic. Magnitude: 0 dB = unchanged.\n\nHatched regions have coherence below the hiding threshold. Avoid EQ decisions there; check for noise or reflections. Stored traces are dashed.\n\nSettled means less than 0.5 dB RMS change for 3 consecutive updates, with at least 25% coherent bins (100 Hz-10 kHz). After 10 s, capture proceeds with an unsettled warning.\n\nMulti-mic: move to each of 2-3 positions, Reset averaging and wait for a fresh trace. Enter the reference delay or press Lock delay, then Capture position. The aligned average uses coherence weights. Capture position again to replace a position; Clear starts a new set.";
        plot.yLabel = "Magnitude (dB)"; plot.yMin = -12; plot.yMax = 12; plot.yStep = 6; plot.showLegend = true; plot.topLegend = true; plot.showLowCohCallout = true;
        addAndMakeVisible(plot);
        smooth.addItem("Smoothing off", 1); smooth.addItem("Smoothing 1/3", 4); smooth.addItem("Smoothing 1/6", 7); smooth.addItem("Smoothing 1/12", 13); smooth.addItem("Smoothing 1/24", 25); smooth.addItem("Smoothing 1/48", 49);
        avg.addItem("Averaging 4", 4); avg.addItem("Averaging 8", 8); avg.addItem("Averaging 16", 16); avg.addItem("Averaging 32", 32); avg.addItem(u8("Averaging \xe2\x88\x9e"), 1000);
        for (int t : {40, 50, 60, 70, 80}) blank.addItem("Hide below coh. " + String(t / 100.0, 2), t);
        smooth.onChange = [this] { model.setSmoothing(smooth.getSelectedId() - 1); };
        avg.onChange = [this] { int a = avg.getSelectedId(); model.setAverages(a == 1000 ? 0 : a); };
        blank.onChange = [this] { model.setBlankThreshold(blank.getSelectedId() / 100.0); };
        smooth.setTooltip("Smoothing: choose the frequency resolution of the live trace.");
        avg.setTooltip("Averaging: choose how many measurements contribute to the live trace.");
        blank.setTooltip("Hide magnitude bins below this coherence threshold. Low coherence can indicate noise or reflections; avoid EQ decisions there.");
        makePrimary(store);
        overflow.setTooltip("More transfer function controls");
        overflow.onClick = [this] { showOverflow(); };
        multiDisclosure.onClick = [this] { multiExpanded = !multiExpanded; resized(); };
        addAndMakeVisible(overflow); addAndMakeVisible(multiDisclosure);
        reset.onClick = [this] { model.send(foh::Command::resetAverages()); };
        store.onClick = [this] { model.storeTrace("Trace " + String(model.traces.count() + 1)); };
        for (auto* c : std::initializer_list<Component*>{&smooth, &avg, &blank, &reset, &store}) addAndMakeVisible(c);
        addAndMakeVisible(target); target.onChange = [this] { modelChanged(AppModel::Overlay); };
        for (int i = 1; i <= 3; ++i) position.addItem("Position " + String(i), i);
        position.setSelectedId(1, dontSendNotification);
        positionDelay.setRange(-500, 3000, 0.001); positionDelay.setTextValueSuffix(" ms");
        positionDelay.setSliderStyle(Slider::IncDecButtons);
        positionDelay.setTextBoxStyle(Slider::TextBoxLeft, false, 94, 24);
        positionDelay.setTooltip("Reference delay for this position. Enter it or use the delay finder result. Reference compensation is subtracted automatically.");
        position.onChange = [this] { positionDelay.setValue(positionDelays[(size_t)(position.getSelectedId() - 1)], dontSendNotification); };
        positionDelay.onValueChange = [this] { positionDelays[(size_t)(position.getSelectedId() - 1)] = positionDelay.getValue(); };
        useDelay.onClick = [this] { if (model.delayLocked()) positionDelay.setValue(model.delay.arrivals[0].delayMs); };
        capturePosition.onClick = [this] { model.send(foh::Command::micPosition(position.getSelectedId() - 1, positionDelay.getValue())); };
        clearPositions.onClick = [this] { model.send(foh::Command::clearMicPositions()); };
        showPositions.setButtonText("Show positions"); showPositions.setToggleState(true, dontSendNotification); showPositions.onClick = [this] { modelChanged(AppModel::Snapshot); };
        capturePosition.setTooltip("Move the mic, Reset averages, wait for a fresh settled trace, then capture this position. Capture again to replace it.");
        for (auto* c : std::initializer_list<Component*>{&position, &positionDelay, &useDelay, &capturePosition, &clearPositions, &showPositions}) addAndMakeVisible(c);
        syncControls();
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, WS::plotPanelMinHeight + 64 }; }
    int priority() const override { return 100; }
    Json saveState() const override { Json j = Json::object(); j.set("yMin", plot.yMin); j.set("yMax", plot.yMax); target.save(j); j.set("multiMicExpanded", multiExpanded); return j; }
    // State loads while detached/hidden defer plot work. Refresh on reattachment
    // even when the measurement is paused and no new snapshots arrive.
    void visibilityChanged() override { modelChanged(AppModel::Overlay); }
    void parentHierarchyChanged() override { visibilityChanged(); }
    void loadState(const Json& j) override { if (j.has("yMin")) { plot.yMin = j["yMin"].asNumber(-12); plot.yMax = j["yMax"].asNumber(12); plot.yStep = (plot.yMax - plot.yMin) / 4; } target.load(j); multiExpanded = j["multiMicExpanded"].asBool(false); resized(); }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto row = r.removeFromTop(28);
        struct Control { Component* component; int width; };
        const Control controls[] {{&smooth, 136}, {&avg, 112}, {&blank, 180}, {&reset, 136}};
        int required = 112;
        for (auto c : controls) required += c.width + 8;
        const bool needsOverflow = row.getWidth() < required;
        int remaining = row.getWidth() - 112 - (needsOverflow ? 36 : 0);
        FlexBox flex; flex.alignItems = FlexBox::AlignItems::center;
        for (auto c : controls) {
            bool fits = remaining >= c.width + 8;
            c.component->setVisible(fits);
            if (fits) { flex.items.add(FlexItem(*c.component).withWidth((float)c.width).withHeight(24).withMargin({0, 8, 0, 0})); remaining -= c.width + 8; }
            else c.component->setBounds({});
        }
        flex.items.add(FlexItem().withFlex(1));
        overflow.setVisible(needsOverflow);
        if (needsOverflow) flex.items.add(FlexItem(overflow).withWidth(28).withHeight(24).withMargin({0, 8, 0, 0}));
        else overflow.setBounds({});
        flex.items.add(FlexItem(store).withWidth(112).withHeight(24));
        flex.performLayout(row);
        r.removeFromTop(8); row = r.removeFromTop(28);
        multiDisclosure.setButtonText(u8(multiExpanded ? "Multi-mic \xe2\x96\xbe" : "Multi-mic \xe2\x96\xb8"));
        multiDisclosure.setBounds(row.removeFromLeft(120)); row.removeFromLeft(8); target.setBounds(row.removeFromLeft(88));
        for (auto* c : std::initializer_list<Component*>{&position, &positionDelay, &useDelay, &capturePosition, &clearPositions, &showPositions}) {
            c->setVisible(multiExpanded); c->setBounds({});
        }
        if (multiExpanded) {
            // Wrap the advanced controls at the same minimum width as the plot.
            int used = 0; row = r.removeFromTop(28);
            for (auto c : std::initializer_list<Control>{{&position, 112}, {&positionDelay, 152}, {&useDelay, 104}, {&capturePosition, 144}, {&clearPositions, 64}, {&showPositions, 136}}) {
                if (used + c.width > r.getWidth()) { r.removeFromTop(8); row = r.removeFromTop(28); used = 0; }
                c.component->setBounds(row.getX() + used, row.getY() + 2, c.width, 24); used += c.width + 8;
            }
        }
        r.removeFromTop(8); plot.setBounds(r);
    }
    void showOverflow() {
        PopupMenu menu;
        auto choices = [&](ComboBox& combo, const String& title, int base) {
            if (combo.isVisible()) return;
            PopupMenu sub;
            for (int i = 0; i < combo.getNumItems(); ++i)
                sub.addItem(base + combo.getItemId(i), combo.getItemText(i), true, combo.getSelectedId() == combo.getItemId(i));
            menu.addSubMenu(title, sub);
        };
        choices(smooth, "Smoothing", 100); choices(avg, "Averaging", 2000); choices(blank, "Coherence", 4000);
        if (!reset.isVisible()) menu.addItem(1, "Reset averaging");
        Component::SafePointer<TransferFunctionPanel> safe(this);
        menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&overflow), [safe](int id) {
            if (!safe || id == 0) return;
            if (id == 1) safe->reset.onClick();
            else if (id >= 4000) safe->blank.setSelectedId(id - 4000);
            else if (id >= 2000) safe->avg.setSelectedId(id - 2000);
            else safe->smooth.setSelectedId(id - 100);
        });
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
        plot.legendChips = {"Averages " + String(model.tf.averages)};
        if (model.calApplied) plot.legendChips.add("Mic calibration on");
        if (multiExpanded || model.positionCount > 0) plot.legendChips.add("Positions " + String(model.positionCount) + "/3");
        plot.note = u8(fohui::settleStatusText(model.conv, model.capture.active, model.capture.unsettled));
        if (model.positionCount >= 2 && (model.multiPosition.H.empty()
            || std::none_of(model.multiPosition.coherence.begin(), model.multiPosition.coherence.end(),
                            [](double c) { return std::isfinite(c) && c > 0; })))
            plot.note += " | No coherent positions; check mic/ref routing and capture again";
        plot.repaint();
    }
private:
    bool multiExpanded = false; TextButton multiDisclosure, overflow {u8("\xe2\x8b\xaf")};
    ComboBox position; Slider positionDelay; std::array<double, 3> positionDelays {};
    TextButton useDelay {foh::str::lockDelay}, capturePosition {foh::str::capturePosition}, clearPositions {"Clear"}; ToggleButton showPositions;
    TargetControls target; PlotComponent plot; ComboBox smooth, avg, blank; TextButton reset {"Reset averaging"}, store {foh::str::storeTrace};
};

// ===================== Phase =====================
class PhasePanel : public Panel {
public:
    explicit PhasePanel(AppModel& m) : Panel(m, "Phase", u8("Phase (\xc2\xb0)")) {
        helpText = "Phase: timing per frequency. A steady slope means leftover delay; flat is ideal.\n\nIn the sub-to-mains guide, phase is shown up to 250 Hz, where it matters for the crossover.";
        plot.yLabel = u8("Phase \xc2\xb0"); plot.yMin = -180; plot.yMax = 180; plot.yStep = 90; plot.wrapPhase = true; plot.showLegend = true; addAndMakeVisible(plot);
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
    explicit CoherencePanel(AppModel& m) : Panel(m, "Coherence", foh::str::coherence) {
        helpText = "Coherence: how much to trust the trace (0 to 1).\n\nGood: 0.9 or above. Fair: 0.6 up to 0.9. Poor: below 0.6. Low coherence is usually a reflection or noise: don't EQ it.";
        plot.yLabel = foh::str::coherence; plot.yMin = 0; plot.yMax = 1; plot.yStep = 0.5; addAndMakeVisible(plot);
    }
    fohws::Size minSize() const override { return { foh::theme::ws::panelMinWidth, 160 }; }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto font = FohLookAndFeel::numeric(T::body, false);
        int rows = 1, x = 0;
        for (const auto& text : captions()) {
            const int width = (int) std::ceil(GlyphArrangement::getStringWidth(font, text)) + 24;
            if (x && x + width > r.getWidth()) { ++rows; x = 0; }
            x += width;
        }
        light = r.removeFromTop(rows * 28); plot.setVisible(r.getHeight() > 60); plot.setBounds(r);
    }
    static StringArray captions() { return {u8("● Good ≥ 0.9"), u8("▲ Fair 0.6–<0.9"), u8("■ Poor < 0.6"), "Average 0.00"}; }
    void paint(Graphics& g) override {
        auto cl = foh::classifyCoherence(model.tf);
        auto words = captions(); words.set(3, "Average " + String(cl.meanCoherence, 2));
        auto font = FohLookAndFeel::numeric(T::body, false); g.setFont(font);
        int x = light.getX(), y = light.getY(); Colour colours[] = {C::ok(), C::warning(), C::danger(), C::textSecondary()};
        for (int i = 0; i < words.size(); ++i) {
            int width = (int) std::ceil(GlyphArrangement::getStringWidth(font, words[i])) + 24;
            if (x > light.getX() && x + width > light.getRight()) { x = light.getX(); y += 28; }
            g.setColour(i == cl.state() || i == 3 ? colours[i] : C::textSecondary());
            g.drawText(words[i], x, y, jmin(width, light.getRight() - x), 28, Justification::centredLeft); x += width;
        }
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Snapshot | AppModel::Theme)) || !isShowing()) return;
        plot.series.clear(); PlotComponent::Series s; s.c = C::traceCoherence(); s.width = D::traceSecondary(); logDecimate(model.tf, model.tf.coherence, s.f, s.v); plot.series.push_back(s);
        resized(); plot.repaint(); repaint();
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
        addAndMakeVisible(target); target.hideTargetToggle(); target.onChange = [this] { modelChanged(AppModel::Overlay); };
    }
    fohws::Size minSize() const override { return { WS::plotPanelMinWidth, 300 }; }
    int priority() const override { return 90; }
    Json saveState() const override { Json j = Json::object(); j.set("fraction", model.rtaFraction); target.save(j); return j; }
    void visibilityChanged() override { modelChanged(AppModel::Overlay); }
    void parentHierarchyChanged() override { visibilityChanged(); }
    void loadState(const Json& j) override { if (j.has("fraction")) { int f = j["fraction"].asInt(3); if (f != model.rtaFraction) model.setRtaFraction(f); } target.load(j); }
    void resized() override { auto r = getLocalBounds().reduced(12); auto row = controlRow(r, 28); frac.setBounds(row.removeFromRight(120)); row.removeFromRight(8); target.setBounds(row); plot.setBounds(r); }
    void modelChanged(int w) override {
        if (w & AppModel::Settings) frac.setSelectedId(model.rtaFraction, dontSendNotification);
        if (!(w & (AppModel::Snapshot | AppModel::Theme | AppModel::Overlay | AppModel::Cal | AppModel::Settings)) || !isShowing()) return;
        const auto spl = model.splCal();
        plot.yLabel = spl.unit(); plot.yMin = spl.calibrated ? 0 : -100; plot.yMax = spl.calibrated ? 140 : 0;
        plot.series.clear(); plot.barLo.clear(); plot.barHi.clear(); PlotComponent::Series s; s.c = C::accent(); s.label = "Live";
        for (size_t i = 0; i < model.rtaDb.size() && i < model.rtaBands.size(); ++i) { auto& b = model.rtaBands[i]; plot.barLo.push_back(b.fLo); plot.barHi.push_back(b.fHi); s.f.push_back(b.fCenter); s.v.push_back(spl.level(model.rtaDb[i], model.splSensitivity())); }
        plot.series.push_back(s); target.appendTo(plot); plot.centreNote = model.rtaDb.empty() || !model.micLevelValid || model.micPeak < 0.001f ? "No signal at the mic. Check the mic input and phantom power." : ""; plot.note = model.calApplied ? "Mic calibration on" : ""; plot.repaint();
    }
private:
    TargetControls target {-40}; PlotComponent plot; ComboBox frac;
};

// ===================== W7 show-time monitoring =====================
class ShowMonitoringPanel : public Panel, private Timer {
public:
    explicit ShowMonitoringPanel(AppModel& m) : Panel(m, "ShowMonitoring", foh::str::showMonitor) {
        helpText = "Feed the console program into the reference input and leave the mic in its reference position. Select a stored TF, then Monitor. No generated output is allowed while monitoring. TF drift is RMS over 100 Hz-10 kHz, using coherence >=0.6 and at least 25% coverage. A 3 dB change for three one-second updates raises an alert. RTA shows live program level; program changes are not system drift.";
        reference.setTextWhenNothingSelected("Select stored reference");
        monitor.setTooltip(helpText);
        reference.onChange = [this] { stopMonitoring(); };
        monitor.onClick = [this] {
            if (active) { stopMonitoring(); return; }
            int index = reference.getSelectedId() - 1;
            if (index < 0 || index >= model.traces.count()) return;
            baseline = model.traces.at(index); // stable even if the trace store changes
            active = true; alert = {};
            model.engine.monitoringSilent = true; model.engine.cancelGeneratedPlayback();
            model.notify(AppModel::Settings); monitor.setButtonText("Stop monitoring");
            reference.setEnabled(false); setStatus(u8("▲ Waiting: collecting coherent program"));
            startTimerHz(1);
        };
        for (auto* c : std::initializer_list<Component*>{&reference, &monitor, &status, &tfPlot, &rtaPlot}) addAndMakeVisible(c);
        tfPlot.yLabel = "TF (dB)"; tfPlot.yMin = -18; tfPlot.yMax = 18; tfPlot.yStep = 6; tfPlot.showLegend = true;
        rtaPlot.yLabel = "RTA dBFS"; rtaPlot.yMin = -100; rtaPlot.yMax = 0; rtaPlot.yStep = 20; rtaPlot.showLegend = true;
        styleLabel(status, T::body, C::textPrimary()); refreshReferences(); stopMonitoring();
    }
    void paint(Graphics& g) override {
        g.setColour(driftWarning ? C::danger() : C::pillBackground());
        g.fillRoundedRectangle(status.getBounds().toFloat(), 10.0f);
    }
    ~ShowMonitoringPanel() override { stopTimer(); if (active) { model.engine.cancelGeneratedPlayback(); model.engine.monitoringSilent = false; } }
    fohws::Size minSize() const override { return { 480, 480 }; }
    int priority() const override { return 100; }
    void resized() override {
        auto r = getLocalBounds().reduced(12); auto row = controlRow(r);
        monitor.setBounds(row.removeFromRight(140)); row.removeFromRight(6); reference.setBounds(row);
        status.setBounds(controlRow(r, 72)); tfPlot.setBounds(r.removeFromTop(r.getHeight() / 2)); rtaPlot.setBounds(r);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Session) { stopMonitoring(); reference.setSelectedId(0, dontSendNotification); refreshReferences(); }
        if ((w & AppModel::Traces) && !active) { refreshReferences(); showIdle(); }
        if (w & AppModel::Theme) { status.setFont(FohLookAndFeel::numeric(T::body, false)); setStatus(status.getText(), driftWarning); }
    }
private:
    void setStatus(const String& text, bool warning = false) {
        driftWarning = warning;
        status.setFont(FohLookAndFeel::numeric(T::body, false));
        status.setColour(Label::textColourId, warning ? C::textOnPoor() : C::textPrimary());
        status.setText(text, dontSendNotification); repaint();
    }
    void showIdle() {
        setStatus(model.traces.count() == 0 ? "No stored traces yet. Store one in Tuning first."
            : u8("■ Stopped: select a stored trace, then Monitor."));
    }
    void refreshReferences() {
        auto id = reference.getSelectedId(); reference.clear(dontSendNotification);
        for (int i = 0; i < model.traces.count(); ++i) reference.addItem(String(model.traces.at(i).name), i + 1);
        if (id <= model.traces.count()) reference.setSelectedId(id, dontSendNotification);
        monitor.setEnabled(active || reference.getSelectedId() > 0);
    }
    void stopMonitoring() {
        stopTimer(); if (active) { model.engine.cancelGeneratedPlayback(); model.engine.monitoringSilent = false; }
        active = false; alert = {}; monitor.setButtonText("Monitor"); reference.setEnabled(true); refreshReferences();
        showIdle();
    }
    void timerCallback() override {
        if (!active) return;
        const bool signal = !model.paused && model.micLevelValid && model.refPeak > 0.00001f
            && !model.engine.deviceFailed.load() && model.engine.state.load() == AudioEngine::Ready;
        const auto drift = signal ? fohui::showDrift(model.tf, baseline.tf) : fohui::ShowDrift{};
        const bool warning = alert.update(drift);
        String waiting;
        if (model.paused) waiting = "measuring is paused. Resume measuring.";
        else if (model.engine.deviceFailed.load() || model.engine.state.load() != AudioEngine::Ready) waiting = "audio device unavailable. Check Setup.";
        else if (!model.micLevelValid) waiting = "no valid mic signal. Check the mic input.";
        else if (model.refPeak <= 0.00001f) waiting = "no reference signal. Check the reference input.";
        else if (model.tf.sampleRate != baseline.tf.sampleRate || model.tf.fftSize != baseline.tf.fftSize) waiting = "reference format changed. Store a new trace.";
        else if (!drift.valid) waiting = "coherence coverage below 25%. Check program level and mic position.";
        if (waiting.isNotEmpty()) setStatus(u8("▲ Waiting: ") + waiting);
        else if (warning) {
            const String frequency = drift.peakHz >= 1000 ? String(drift.peakHz / 1000, 1) + " kHz" : String(drift.peakHz, 0) + " Hz";
            setStatus(u8("▲ Drift: ") + (drift.peakDb >= 0 ? "+" : "") + String(drift.peakDb, 1) + " dB at " + frequency, true);
        } else setStatus((alert.consecutive > 0 ? u8("▲ Checking drift: ") : u8("● Monitoring: ")) + String(drift.rmsDb, 1) + " dB RMS");
        if (!isShowing()) return; // keep alert state, avoid hidden rendering
        tfPlot.series.clear();
        PlotComponent::Series stored; stored.c = C::storedTrace(0); stored.label = String(baseline.name); stored.dash = foh::theme::stroke::dashStored;
        logDecimate(baseline.tf, baseline.tf.magDb, stored.f, stored.v, &stored.gap); tfPlot.series.push_back(std::move(stored));
        if (signal) { PlotComponent::Series live; live.c = C::traceLive(); live.label = "Live TF";
            logDecimate(model.tf, model.tf.magDb, live.f, live.v, &live.gap); tfPlot.series.push_back(std::move(live)); }
        tfPlot.repaint(); rtaPlot.series.clear();
        PlotComponent::Series rta; rta.c = C::accent(); rta.label = "Live program";
        if (signal) for (size_t i = 0; i < model.rtaDb.size() && i < model.rtaBands.size(); ++i) {
            rta.f.push_back(model.rtaBands[i].fCenter); rta.v.push_back(model.rtaDb[i]);
        }
        rtaPlot.series.push_back(std::move(rta)); rtaPlot.repaint();
    }
    ComboBox reference; TextButton monitor {"Monitor"}; Label status;
    PlotComponent tfPlot, rtaPlot; foh::Trace baseline; fohui::ShowDriftAlert alert; bool active = false, driftWarning = false;
};

// ===================== Delay finder =====================
class DelayFinderPanel : public Panel {
public:
    explicit DelayFinderPanel(AppModel& m) : Panel(m, "DelayFinder", "Delay finder") {
        helpText = "The delay finder compares the reference signal with the mic signal.\n\nLock delay applies it to the reference so the transfer function phase flattens.\n\nTwo similar spikes: arrivals within 3 dB and more than 0.5 ms apart. Check that only one speaker is on.\n\nConfidence 0 means no lock: check signal and that only one speaker is on.";
        value.setColour(Label::textColourId, C::textPrimary()); value.setFont(FohLookAndFeel::numeric(T::valueLg)); value.setText("--", dontSendNotification);
        detail.setFont(FohLookAndFeel::numeric(T::body, false)); detail.setColour(Label::textColourId, C::textSecondary()); detail.setJustificationType(Justification::topLeft); detail.setText("Start pink noise, then press Find delay.", dontSendNotification);
        styleLabel(twoSpikeBadge, T::body, C::warning(), true);
        twoSpikeBadge.setText(u8("\xe2\x96\xb2 Two similar spikes"), dontSendNotification);
        twoSpikeBadge.setColour(Label::backgroundColourId, C::warning().withAlpha(0.12f));
        twoSpikeBadge.setColour(Label::outlineColourId, C::warning());
        twoSpikeBadge.setTooltip("Two arrivals within 3 dB and more than 0.5 ms apart: only one speaker on? Check the speaker configuration before locking the delay.");
        addChildComponent(twoSpikeBadge);
        makePrimary(insert); find.onClick = [this] { model.requestDelay(sub.getToggleState()); }; insert.onClick = [this] { model.lockDelayFromLast(); };
        sub.setButtonText("Sub (LF)");
        for (auto* c : std::initializer_list<Component*>{&value, &detail, &find, &insert, &sub}) addAndMakeVisible(c);
        addAndMakeVisible(detailViewport); detailViewport.setViewedComponent(&detail, false);
        if (model.delayPending || model.delayLocked()) modelChanged(AppModel::Delay);
    }
    ~DelayFinderPanel() override { detailViewport.setViewedComponent(nullptr, false); }
    fohws::Size minSize() const override { return { 340, 260 }; }
    int priority() const override { return 70; }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
        auto row = r.removeFromBottom(28); int third = row.getWidth() / 3;
        find.setBounds(row.removeFromLeft(third)); insert.setBounds(row.removeFromLeft(third)); sub.setBounds(row);
        r.removeFromBottom(8);
        if (twoSpikeBadge.isVisible()) { twoSpikeBadge.setBounds(r.removeFromTop(28)); r.removeFromTop(4); }
        value.setBounds(r.removeFromTop(56)); detailViewport.setBounds(r);
        const int lines = jmax(3, detail.getText().isNotEmpty() ? StringArray::fromLines(detail.getText()).size() : 3);
        detail.setBounds(0, 0, jmax(0, r.getWidth() - detailViewport.getScrollBarThickness()), jmax(r.getHeight(), lines * 20));
    }
    void modelChanged(int w) override {
        if (w & AppModel::Theme) {
            value.setFont(FohLookAndFeel::numeric(T::valueLg)); value.setColour(Label::textColourId, C::textPrimary());
            makePrimary(insert); detail.setColour(Label::textColourId, C::textSecondary());
            twoSpikeBadge.setColour(Label::textColourId, C::warning());
            twoSpikeBadge.setColour(Label::backgroundColourId, C::warning().withAlpha(0.12f));
            twoSpikeBadge.setColour(Label::outlineColourId, C::warning());
        }
        if (!(w & (AppModel::Delay | AppModel::Theme))) return;
        auto& d = model.delay;
        twoSpikeBadge.setVisible(!model.delayPending && model.delayLocked() && d.twoSimilarArrivals);
        resized();
        if (model.delayPending) { detail.setText(model.delayWasSub ? juce::String::fromUTF8("Finding sub delay (3 s window)\xe2\x80\xa6") : juce::String::fromUTF8("Finding delay\xe2\x80\xa6"), dontSendNotification); return; }
        if (!model.delayLocked()) { value.setText("--", dontSendNotification); detail.setText(d.arrivals.empty() ? "No signal: is pink noise playing?" : "No valid delay result: check signal and that only one speaker is on", dontSendNotification); return; }
        auto& a0 = d.arrivals[0];
        value.setText(String(consoleMs(a0.delayMs), 2) + " ms", dontSendNotification);
        // Confidence already includes any ambiguity penalty.
        String t = (d.confidence > 0.7 ? u8("● High") : d.confidence > 0.35 ? u8("▲ Medium") : u8("■ Low")) + " confidence (" + String(roundToInt(d.confidence * 100.0)) + "%)"
                 + "\nPolarity: " + (a0.inverted ? "inverted" : "normal")
                 + u8("\n\xe2\x89\x88 ") + String(a0.distanceM, 2) + " m" + (model.delayWasSub ? " (sub, LF-weighted)" : "");
        for (size_t i = 1; i < d.arrivals.size(); ++i) t << "\nArrival " << (int) i + 1 << " " << String(consoleMs(d.arrivals[i].delayMs), 2) << " ms (" << String(d.arrivals[i].relLevelDb, 1) << " dB)";
        detail.setText(t, dontSendNotification); resized();
    }
private:
    Viewport detailViewport; Label value, detail, twoSpikeBadge; TextButton find {foh::str::findDelay}, insert {foh::str::lockDelay}; ToggleButton sub;
    TooltipWindow tooltip { nullptr, 700 };
};

// ===================== Impulse =====================
class ImpulsePanel : public Panel {
public:
    explicit ImpulsePanel(AppModel& m) : Panel(m, "Impulse", "Impulse") {
        helpText = "Measure a 5-second log sweep with a safety ramp, followed by silence for decay. The impulse is deconvolved against the recorded loopback reference. Start at a low generator level and check your routing. ETC shows envelope energy relative to its peak.";
        addAndMakeVisible(imp); addAndMakeVisible(measure); addAndMakeVisible(etc); addAndMakeVisible(status);
        measure.onClick = [this] {
            SafePointer<ImpulsePanel> safe(this);
            model.confirmGeneratorStart("Log sweep", [safe] {
                if (!safe) return;
                foh::Command c; c.type = foh::CommandType::MeasureSweep; safe->model.send(c);
            });
        };
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
        if (model.sweepCapturing) note = juce::String::fromUTF8("Measuring sweep\xe2\x80\xa6");
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
    explicit SignalMetersPanel(AppModel& m) : Panel(m, "SignalMeters", "Signal meters") {
        updateHelp();
    }
    fohws::Size minSize() const override { return { 340, 160 }; }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().reduced(12); const char* names[] = {"Mic", "Ref"}; float* lv[] = {&micDb, &refDb};
        for (int i = 0; i < 2; ++i) { auto row = r.removeFromTop(52); r.removeFromTop(8);
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::font(T::body, true)); g.drawText(names[i], row.removeFromLeft(36), Justification::centredLeft);
            auto val = row.removeFromRight(180); auto bar = row.reduced(4, 10).toFloat();
            g.setColour(C::divider()); g.fillRoundedRectangle(bar, 3.f);
            float db = *lv[i], frac = jlimit(0.f, 1.f, (db + 60.f) / 60.f);
            Colour c = db < -60 ? C::textSecondary() : db < -40 ? C::warning() : db <= -12 ? C::ok() : C::danger();
            g.setColour(c); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * frac), 3.f);
            g.setColour(C::textSecondary()); float x = bar.getX() + bar.getWidth() * (40.f / 60.f); g.fillRect(x, bar.getY() - 3, 1.f, bar.getHeight() + 6); // -20 dBFS target mark
            String st = db < -60 ? u8("■ No signal") : db < -40 ? u8("▲ Low") : db <= -12 ? u8("● Good") : u8("▲ Hot");
            g.setColour(c); g.setFont(FohLookAndFeel::font(T::body)); g.drawText(st, val.removeFromTop(24), Justification::centredRight);
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::numeric(T::body, false));
            g.drawText((db < -60 ? u8("—") : String(db, 1)) + " dBFS", val, Justification::centredRight); }
        g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::caption)); g.drawText("target -20 dBFS on pink noise", r.removeFromTop(16), Justification::left);
    }
    void modelChanged(int w) override {
        if (!(w & (AppModel::Levels | AppModel::Theme | AppModel::Cal | AppModel::Settings))) return;
        auto upd = [](float& v, float lin) { float db = lin > 1e-6f ? 20.f * std::log10(lin) : -120.f; v = db > v ? db : v - 1.5f; v = std::max(v, -120.f); }; // fast attack, slow release
        if (w & AppModel::Settings) updateHelp();
        upd(micDb, model.micPeak); upd(refDb, model.refPeak); repaint();
    }
private:
    void updateHelp() {
        auto channel = [](int index) { return index < 0 ? String("None") : String(index + 1); };
        helpText = "Mic input = " + channel(model.engine.micCh.load()) + ". Reference input = " + channel(model.engine.refCh.load())
            + ". Generator outputs = " + channel(model.engine.outA.load()) + " and " + channel(model.engine.outB.load())
            + ".\n\nCheck the selected device routing in Setup. Aim near -20 dBFS on pink noise; avoid clipping. Enable phantom power only if your mic requires it.";
    }
    float micDb = -120, refDb = -120;
};

// ===================== Generator =====================
class GeneratorPanel : public Panel {
public:
    explicit GeneratorPanel(AppModel& m) : Panel(m, "Generator", "Generator") {
        helpText = "Pink noise for the transfer function and RTA; the log sweep lasts 5 s followed by a 1 s silence gap.\n\nOut 1 goes to the console, Out 2 loops back to In 2 as the reference.";
        type.addItem("Generator off", 1); type.addItem("Pink noise", 2); type.addItem(u8("Log sweep 20 Hz\xe2\x80\x93" "20 kHz (5 s, repeating)"), 3);
        type.onChange = [this] {
            const int mode = type.getSelectedId() - 1;
            if (mode == AudioEngine::Off) { model.stopGenerator(); return; }
            sync(); // Keep the actual state visible until the user confirms.
            SafePointer<GeneratorPanel> safe(this);
            model.confirmGeneratorStart(mode == AudioEngine::Pink ? "Pink noise" : "Log sweep", [safe, mode] {
                if (!safe) return;
                safe->model.engine.gen = mode; safe->model.notify(AppModel::Settings);
            });
        };
        gain.setTextValueSuffix(" dBFS"); gain.setTextBoxStyle(Slider::TextBoxRight, false, 90, 22);
        gain.onValueChange = [this] { model.setGeneratorLevel((float) gain.getValue()); };
        fullLevel.setButtonText("Allow full level");
        fullLevel.setTooltip("Allow output up to 0 dBFS for this app session. Turning this off caps output at -6 dBFS.");
        fullLevel.onClick = [this] { model.allowFullGeneratorLevel(fullLevel.getToggleState()); };
        addAndMakeVisible(type); addAndMakeVisible(gain); addAndMakeVisible(fullLevel); sync();
    }
    fohws::Size minSize() const override { return { 360, 112 }; }
    void sync() {
        type.setSelectedId(model.engine.gen.load() + 1, dontSendNotification);
        gain.setRange(-60, model.allowsFullGeneratorLevel() ? 0 : -6, 0.5);
        gain.setValue(model.engine.genGainDb.load(), dontSendNotification);
        fullLevel.setToggleState(model.allowsFullGeneratorLevel(), dontSendNotification);
        fullLevel.setColour(ToggleButton::textColourId, C::textPrimary());
        fullLevel.setColour(ToggleButton::tickColourId, C::accent());
        fullLevel.setColour(ToggleButton::tickDisabledColourId, C::textSecondary());
    }
    void resized() override { auto r = getLocalBounds().reduced(pad, 8); type.setBounds(controlRow(r)); gain.setBounds(controlRow(r)); fullLevel.setBounds(controlRow(r)); }
    void modelChanged(int w) override { if (w & (AppModel::Settings | AppModel::Theme)) sync(); }
private:
    ComboBox type; Slider gain; ToggleButton fullLevel;
};

// ===================== Inputs & devices (mic / channel list) =====================
class InputsAndDevicesPanel : public Panel {
public:
    explicit InputsAndDevicesPanel(AppModel& m) : Panel(m, "InputsAndDevices", "Inputs and devices"), setup(m, false) {
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
    explicit CalibrationPanel(AppModel& m) : Panel(m, "Calibration", foh::str::micCalibration) {
        helpText = "Load your mic calibration file (.cal, .txt, .frd or .csv) and enable Mic calibration. It is applied to the transfer function and RTA by the analysis engine.";
        load.onClick = [this] { chooseFile(); };
        error.onDismiss = [this] { load.setVisible(true); resized(); };
        toggle.setButtonText(foh::str::micCalibration); toggle.onClick = [this] { model.setCalEnabled(toggle.getToggleState()); };
        styleLabel(info, T::body, C::textSecondary()); info.setJustificationType(Justification::topLeft);
        addAndMakeVisible(form);
        for (auto* c : std::initializer_list<Component*>{&load, &toggle, &info}) form.canvas.addAndMakeVisible(c);
        form.canvas.addChildComponent(error); sync();
    }
    fohws::Size minSize() const override { return { 240, 100 }; }
    void chooseFile() {
        chooser = std::make_unique<FileChooser>("Mic calibration file", File(), "*.csv;*.txt;*.frd;*.cal");
        const SafePointer<CalibrationPanel> safe(this);
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe](const FileChooser& fc) { auto f = fc.getResult(); if (safe && f.existsAsFile()) { safe->error.setMessage("", "", {}); safe->model.loadCalFile(f); } });
    }
    void sync() {
        toggle.setToggleState(model.calOn, dontSendNotification);
        auto& c = model.cal;
        info.setText(model.calFile.isEmpty() ? String("No mic calibration loaded") :
            model.calFile + ": " + String((int) c.hz.size()) + " points" +
            (c.ok() ? ", " + String(c.hz.front(), 0) + "-" + String(c.hz.back(), 0) + " Hz" : String()) +
            (c.hasPhase ? ", with phase" : "") +
            (c.hasSensFactor ? "\nSens factor: " + String(c.sensitivityOffsetDb, 2) + " dB (not used; set level in SPL calibration)" : String()), dontSendNotification);
        error.setMessage(model.calImportError, u8("Choose another file\xe2\x80\xa6"), [this] { chooseFile(); });
        load.setVisible(model.calImportError.isEmpty() || !error.isVisible());
        resized();
    }
    void resized() override {
        form.setBounds(getLocalBounds());
        const int width = form.layoutWidth(240);
        auto r = Rectangle<int>(12, 12, width - 24, 1000);
        int bottom = 12;
        if (error.isVisible()) {
            error.setBounds(r.removeFromTop(error.preferredHeight(r.getWidth())));
            r.removeFromTop(8); bottom = r.getY();
        }
        if (load.isVisible()) { load.setBounds(r.removeFromTop(24).removeFromLeft(140)); r.removeFromTop(8); }
        toggle.setBounds(r.removeFromTop(24)); r.removeFromTop(8);
        AttributedString copy; copy.append(info.getText(), FohLookAndFeel::font(T::body), C::textSecondary());
        TextLayout layout; layout.createLayout(copy, (float) jmax(1, r.getWidth() - 4));
        info.setBounds(r.removeFromTop((int) std::ceil(layout.getHeight()) + 16));
        form.sizeCanvas(width, jmax(bottom, r.getY()) + 12);
    }
    void modelChanged(int w) override { if (w & (AppModel::Cal | AppModel::Theme)) { info.setColour(Label::textColourId, C::textSecondary()); info.setFont(FohLookAndFeel::font(T::body)); sync(); } }
private:
    ScrollableForm form;
    TextButton load {juce::String::fromUTF8("Load file\xe2\x80\xa6")}; ToggleButton toggle; Label info; Banner error;
    std::unique_ptr<FileChooser> chooser;
};

// ===================== Trace list =====================
class TraceListPanel : public Panel, private ListBoxModel {
public:
    explicit TraceListPanel(AppModel& m) : Panel(m, "TraceList", "Traces") {
        helpText = "Up to 8 stored traces. Select a row to edit its name or color; click the box to show/hide it. Choose two sources for a complex virtual sum preview, including the same trace twice. The preview does not store or change measurements.";
        list.setModel(this); list.setRowHeight(56);
        name.setTextToShowWhenEmpty("Select a trace to rename", C::textAxis());
        name.onReturnKey = [this] { model.renameTrace(list.getSelectedRow(), name.getText()); };
        rename.onClick = name.onReturnKey;
        for (int i = 0; i < 8; ++i) colour.addItem(foh::ui::theme().traceSlots[(size_t) i].name, i + 1);
        colour.setTextWhenNothingSelected("Color");
        colour.onChange = [this] { if (colour.getSelectedId() > 0) model.colourTrace(list.getSelectedRow(), palette(colour.getSelectedId() - 1)); };
        del.onClick = [this] { model.removeTrace(list.getSelectedRow()); };
        sourceA.setTextWhenNothingSelected("Sum source A"); sourceB.setTextWhenNothingSelected("Sum source B");
        sourceA.onChange = sourceB.onChange = [this] { model.selectSum(sourceA.getSelectedId() - 1, sourceB.getSelectedId() - 1); };
        preview.yLabel = "Sum (dB)"; preview.yMin = -12; preview.yMax = 12; preview.yStep = 6; preview.showLegend = true;
        for (auto* c : std::initializer_list<Component*>{&list, &name, &rename, &colour, &del, &sourceA, &sourceB, &preview}) addAndMakeVisible(c);
        modelChanged(AppModel::Traces);
    }
    ~TraceListPanel() override { list.setModel(nullptr); }
    fohws::Size minSize() const override { return {300, 420}; }
    int priority() const override { return 5; }
    void resized() override {
        auto r = getLocalBounds().reduced(12);
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
            ? "Sources need matching sample rates and frequency grids, and valid TF data" : "Choose two traces for a sum") : String{};
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
    class TraceRow : public Component {
    public:
        TraceRow(TraceListPanel& p) : owner(p) {
            addAndMakeVisible(visible);
            visible.onClick = [this] { owner.list.selectRow(row); owner.model.toggleTrace(row); };
        }
        void update(int index) {
            row = index; const auto& trace = owner.model.traces.at(row);
            visible.setButtonText(String(trace.name)); visible.setTitle("Show trace " + String(trace.name));
            visible.setToggleState(trace.visible, dontSendNotification);
            visible.setColour(ToggleButton::textColourId, C::textPrimary());
            visible.setColour(ToggleButton::tickColourId, C::accent());
            visible.setTooltip("Show or hide this stored trace"); repaint();
        }
        void resized() override { visible.setBounds(0, 0, jmax(0, getWidth() - 64), 28); }
        void mouseDown(const MouseEvent&) override { owner.list.selectRow(row); }
        void paint(Graphics& g) override {
            if (!owner.model.validTrace(row)) return;
            if (owner.list.isRowSelected(row)) g.fillAll(C::navItemActiveBg());
            const auto& slot = traceSlot(owner.model, row); g.setColour(owner.model.traceColour(row));
            Path path; path.startNewSubPath((float)getWidth() - 60, 14); path.lineTo((float)getWidth() - 8, 14);
            PathStrokeType stroke(D::traceStored());
            if (slot.dashCount) { Path dashed; stroke.createDashedStroke(dashed, path, slot.dash.data(), slot.dashCount); g.fillPath(dashed); }
            else g.strokePath(path, stroke);
            g.setFont(FohLookAndFeel::numeric(T::body, false)); g.setColour(C::textSecondary());
            g.drawText("Reference delay " + String(owner.model.traces.at(row).refDelayMs, 2) + " ms", 28, 28, getWidth() - 36, 24, Justification::centredLeft);
        }
    private:
        TraceListPanel& owner; int row = 0; ToggleButton visible;
    };
    Component* refreshComponentForRow(int row, bool, Component* existing) override {
        if (!model.validTrace(row)) { delete existing; return nullptr; }
        auto* component = dynamic_cast<TraceRow*>(existing);
        if (!component) component = new TraceRow(*this);
        component->update(row); return component;
    }
    void paintListBoxItem(int, Graphics&, int, int, bool) override {}
    void selectedRowsChanged(int row) override {
        bool valid = model.validTrace(row);
        name.setEnabled(valid); rename.setEnabled(valid); colour.setEnabled(valid); del.setEnabled(valid);
        name.setText(valid ? String(model.traces.at(row).name) : String{}, false);
        int id = 0;
        if (valid) for (int i = 0; i < 8; ++i) if (model.traceColour(row) == palette(i)) id = i + 1;
        colour.setSelectedId(id, dontSendNotification);
    }
private:
    static Colour palette(int i) { return C::storedTrace(i); }
    ListBox list; TextEditor name; ComboBox colour, sourceA, sourceB; TextButton rename {"Rename"}, del {"Delete trace"}; PlotComponent preview;
};

// ===================== Indicative SPL / Leq =====================
class SplLeqPanel : public Panel {
public:
    explicit SplLeqPanel(AppModel& m) : Panel(m, "SPLNumbers", "SPL / Leq (indicative)") {
        addAndMakeVisible(weighting); weighting.addItemList({"A", "C", "Z"}, 1); weighting.setSelectedId(1);
        addAndMakeVisible(preset); preset.addItemList({"WHO", "NIOSH"}, 1); preset.setSelectedId(1);
        weighting.onChange = preset.onChange = [this] { repaint(); };
        addAndMakeVisible(calibrate); calibrate.onClick = [this] { if (model.requestSplCalibration) model.requestSplCalibration(); };
        helpText = "Indicative calibrated SPL. Leq averages energy, with rolling 1 min and 15 min windows in 100 ms buckets. Before a window fills it uses elapsed measured time. Session starts with the input and resets on input or calibration changes. Paused audio is excluded. Warnings always use A weighting. WHO: 100 dBA LAeq-15. NIOSH: 85 dBA for 8 h, 3 dB exchange. This is not a certified sound level meter.";
    }
    fohws::Size minSize() const override { return {400, 380}; }
    void resized() override {
        auto r = getLocalBounds().reduced(12); auto row = r.removeFromTop(28);
        weighting.setBounds(row.removeFromLeft(72)); row.removeFromLeft(8); preset.setBounds(row.removeFromLeft(96));
        calibrate.setBounds(r.removeFromTop(28).withTrimmedTop(4));
    }
    void modelChanged(int) override { repaint(); }
    void paint(Graphics& g) override {
        auto r = getLocalBounds().reduced(12); r.removeFromTop(64);
        auto line = [&](const String& text, Colour colour) {
            g.setColour(colour); g.setFont(FohLookAndFeel::numeric(T::body, false));
            g.drawFittedText(text, r.removeFromTop(28), Justification::centredLeft, 1);
        };
        const auto c = model.splCal();
        if (!c.calibrated) { line(u8("■ SPL calibration required"), C::textSecondary()); return; }
        if (!model.micLevelReady() || model.leqRevision != model.analysis.leqRevision.load() || !model.leq.valid) {
            if (model.micPeak >= 0.99f) line(u8("▲ Warning: input clipping"), C::danger());
            line(u8("■ No live SPL / Leq signal"), C::textSecondary()); return;
        }
        const size_t w = (size_t) jlimit(0, 2, weighting.getSelectedId() - 1);
        auto level = [&](double db) { return c.level(db - (model.calOn ? model.cal.dbAt(1000) : 0), model.splSensitivity()); };
        const String unit = " dB(" + weighting.getText() + ")";
        const char* labels[] = {"SPL", "Leq 1 min", "Leq 15 min", "Leq session"};
        const double values[] = {model.leq.level[w], model.leq.minute[w], model.leq.fifteenMinutes[w], model.leq.session[w]};
        for (int i = 0; i < 4; ++i) {
            auto row = r.removeFromTop(40); auto label = row.removeFromLeft(120);
            g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::body)); g.drawText(labels[i], label, Justification::centredLeft);
            g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::numeric(T::valueMd)); g.drawText(String(level(values[i]), 1) + unit, row, Justification::centredRight);
        }
        line("Measured " + String(model.leq.seconds, 1) + " s" + (model.leq.seconds < 900 ? " (partial windows)" : ""), C::textSecondary());
        const auto selected = preset.getSelectedId() == 1 ? foh::ExposurePreset::WHO : foh::ExposurePreset::NIOSH;
        const auto warning = foh::exposureWarning(selected, level(model.leq.fifteenMinutes[0]), level(model.leq.session[0]), model.leq.seconds);
        if (model.micPeak >= 0.99f) line(u8("▲ Warning: input clipping"), C::danger());
        else if (warning.exceeded) line(u8("▲ ") + preset.getText() + " limit reached", C::danger());
        else line(u8("● ") + preset.getText() + " limit not reached", C::ok());
        line(selected == foh::ExposurePreset::WHO ? "100 dB(A) Leq-15" : "85 dB(A) / 8 h, 3 dB exchange", C::textSecondary());
    }
private:
    ComboBox weighting, preset; TextButton calibrate {u8("Calibrate SPL…")};
};

// ===================== Inspector =====================
class InspectorPanel : public Panel {
public:
    explicit InspectorPanel(AppModel& m) : Panel(m, "Inspector", "Inspector") { help.overlay = false; addAndMakeVisible(help); refresh(); }
    fohws::Size minSize() const override { return { WS::inspectorMinWidth, 160 }; }
    int priority() const override { return 0; } // first to fold into a tab (spec §7)
    void resized() override { help.setBounds(getLocalBounds()); }
    void refresh() { help.caption = model.inspectorTitle.isEmpty() ? String("Inspector") : "Inspector " + u8("\xc2\xb7 ") + model.inspectorTitle; help.setText(model.inspectorText); }
    void modelChanged(int w) override { if (w & (AppModel::Overlay | AppModel::Theme)) refresh(); }
private:
    HelpPanel help;
};

// ===================== Unfinished panels (opt-in development builds only) =====================
#if FOH_SHOW_UNFINISHED
class PlaceholderPanel : public Panel {
public:
    PlaceholderPanel(AppModel& m, std::string id, String title, String what) : Panel(m, std::move(id), std::move(title)), text(std::move(what)) { helpText = text; }
    bool isPlaceholder() const override { return true; }
    int priority() const override { return panelId() == "Events" ? 5 : 10; } // spec §7: Inspector, then Events/TraceList, then the rest
    void paint(Graphics& g) override { g.setColour(C::textAxis()); g.setFont(FohLookAndFeel::font(T::body)); g.drawFittedText(text + "\n\nNot available yet.", getLocalBounds().reduced(16), Justification::centred, 6); }
private:
    String text;
};

#endif

// Guide panel lives in GuidePanel.cpp
std::unique_ptr<Panel> makeGuidePanel(AppModel&);

PanelRegistry& PanelRegistry::standard() {
    static PanelRegistry r = [] {
        PanelRegistry x;
        auto add = [&x](std::string id, String title, std::function<std::unique_ptr<Panel>(AppModel&)> f) { x.add({ std::move(id), std::move(title), std::move(f) }); };
        add("TransferFunction", "Transfer function", [](AppModel& m) { return std::make_unique<TransferFunctionPanel>(m); });
        add("Phase", "Phase", [](AppModel& m) { return std::make_unique<PhasePanel>(m); });
        add("Coherence", foh::str::coherence, [](AppModel& m) { return std::make_unique<CoherencePanel>(m); });
        add("ShowMonitoring", foh::str::showMonitor, [](AppModel& m) { return std::make_unique<ShowMonitoringPanel>(m); });
        add("RTA", "RTA", [](AppModel& m) { return std::make_unique<RtaPanel>(m); });
        add("DelayFinder", "Delay finder", [](AppModel& m) { return std::make_unique<DelayFinderPanel>(m); });
        add("Impulse", "Impulse", [](AppModel& m) { return std::make_unique<ImpulsePanel>(m); });
        add("SignalMeters", "Signal meters", [](AppModel& m) { return std::make_unique<SignalMetersPanel>(m); });
        add("Generator", "Generator", [](AppModel& m) { return std::make_unique<GeneratorPanel>(m); });
        add("InputsAndDevices", "Inputs and devices", [](AppModel& m) { return std::make_unique<InputsAndDevicesPanel>(m); });
        add("Calibration", foh::str::micCalibration, [](AppModel& m) { return std::make_unique<CalibrationPanel>(m); });
        add("TraceList", "Traces", [](AppModel& m) { return std::make_unique<TraceListPanel>(m); });
        add("Inspector", "Inspector", [](AppModel& m) { return std::make_unique<InspectorPanel>(m); });
        add("GuideInspector", "Guide", [](AppModel& m) { return makeGuidePanel(m); });
       #if FOH_SHOW_UNFINISHED
        auto ph = [&add](std::string id, String title, String what) { add(id, title, [id, title, what](AppModel& m) { return std::make_unique<PlaceholderPanel>(m, id, title, what); }); };
        ph("LimitPreset", "Limit preset", "SPL limit preset for exposure monitoring.");
        ph("EQSuggestions", "EQ suggestions", "Suggested EQ moves from the transfer function, coherence-weighted.");
        ph("SPLHistory", "SPL history", "SPL over time with LAeq / LCeq traces.");
        ph("Dose", "Dose", "Noise dose against the selected limit.");
        ph("Events", "Events", "Log of clipping, limit breaches and captures.");
       #endif
        add("SPLNumbers", "SPL / Leq", [](AppModel& m) { return std::make_unique<SplLeqPanel>(m); });
        return x; }();
    return r;
}
