// Guide / inspector panel: a narrow stepper. Its charts are drawn as an overlay in the Transfer
// Function and Phase panels (AppModel::overlay), so the guide works inside any workspace layout.
#include "Panels.h"
#include "Diagrams.h"
#include "ScrollableForm.h"
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;
namespace ST = foh::theme::stroke;
struct Step { const char* progress; const char* title; const char* body; const char* action; int act; int chart; };
// The shared ending is deliberately manual: this panel has no DSP write action.
static const Step kStore = {"Store", "Store the before trace", "Before changing the DSP, measure the system in the configuration you will verify (both speakers on for alignment). Keep the mic and reference delay fixed. The guide reuses its 'Guide before' and 'Guide after' traces.", "Measure before change", 20, 6};
static const Step kApply = {"Apply", "Apply in your DSP", "Enter the change yourself in your DSP. FOH Gauge never applies it. Keep the same mic position, routing and speaker configuration, then confirm below.", "I entered the change", 21, 6};
static const Step kVerify = {"Verify", "Verify against the stored trace", "Re-measure with the same mic position and reference delay. Compare the before and after traces, listen, then record Pass or Fail with a note describing whether the intended change worked. An unsettled measurement must be repeated.", "Re-measure to verify", 22, 6};
static const std::vector<std::vector<Step>> kGuides = {
 { {"Connect", "Connect your gear", "UMC404HD: Out 1 to the console. Out 2 to In 2 with a 1/4\" TRS cable (reference loopback; TRS keeps it off phantom). ECM8000 to In 1 (XLR, +48 V on). Pick the device and channels on Setup at 48 kHz.", "Open Setup", 1, 0},
   {"Signal", "Signal check", "Start pink noise and bring the console up to a comfortable level. Both inputs should sit near -20 dBFS (top bar), never clipping. Mic 1-3 m on-axis from ONE main, ear height, away from walls.", "Start pink noise", 2, 6},
   {"Delay", "Find the reference delay", "The tallest spike is the direct sound; smaller later spikes are reflections. If two spikes are similar, check only one speaker is on.", "Find + lock delay", 3, 7},
   {"Check", "Check the trace", "Phase should be fairly flat and coherence high (above ~0.8) above 200 Hz. Low coherence = don't trust that region.", "Open transfer function", 4, 6},
   kStore, kApply, kVerify },
 { {"Measure mains", "Measure the mains alone", "Mute the subs. Mic where sub and mains are about equally loud (often FOH). Pink noise on. This finds and locks the delay to the mains, then stores 'Main'.", "Capture mains \xe2\x97\x8f", 12, 1},
   {"Measure sub", "Measure the sub alone", "Mute the mains, unmute the subs. Don't find the delay again: the comparison depends on the same timing. The dashed line is your stored mains.", "Capture sub \xe2\x97\x8f", 7, 2},
   {"Find delay", "Find the delay", "Compare phase across the shaded crossover band (up to 250 Hz). Choose matching slopes and the least delay. Judge polarity from phase, not the impulse.", "Use this delay", 8, 3},
   kStore, kApply, kVerify },
 { {"Mains", "Measure the mains", "Mains already tuned. Mic in the seam: where main and fill are about equally loud, inside the fill's area. Fill muted. This finds and locks the delay to the mains.", "Find + lock delay", 3, 6},
   {"Fill", "Measure the fill", "Mute the mains, unmute the fill. The app finds the fill's arrival and compares it with the locked mains time.", "Find fill arrival", 10, 7},
   {"Review", "Review the fill delay", "Review the suggested fill delay before changing the DSP. Many engineers add a few ms more so the mains arrive first and the image stays on stage (rule of thumb; trust your ears). Fill only as loud as needed.", "Open transfer function", 4, 6},
   kStore, kApply, kVerify },
 { {"SPL cal", "Calibrate SPL first", "Use SPL Cal on the toolbar or Setup: calibrator at 94 or 114 dB, or a known level. Confirm the selected mic input and gain; changing either requires a calibration check. Without calibration, levels are dBFS, not dB SPL. Remove the calibrator before measuring.", "Calibration checked", 23, 0},
   {"Time alignment", "Align before EQ", "Complete W1 reference delay, W2 sub-to-mains, then W3 fills if present, including each Verify. Fix routing, polarity and coverage problems first. At each new mic position, establish reference delay; keep it fixed for before/after comparisons. Trust only settled, coherent measurements.", "Alignment checked", 23, 6},
   {"Tuning order", "Tune one change at a time", "Check mains alone, then mains + subs, then fills and the full system. Check representative listening positions: fix aiming and relative levels before broad EQ. Compare with your target; avoid boosting deep cancellations or chasing low-coherence bins. Choose one change, then Store, Apply in DSP and Verify. Repeat W4 for further changes.", "Ready to store", 23, 6},
   kStore, kApply, kVerify },
 { {"0-1 min", "Five-minute check: signal path", "Use the last verified tuning as your baseline. Confirm device, mic/ref channels, sample rate, mic gain and SPL calibration. Check console/DSP preset, mutes and routing. At low level, confirm each main, sub and fill passes signal; watch mic/ref meters for missing signal or clipping.", "Signal path checked", 23, 0},
   {"1-3 min", "Check response and timing", "Return to the tuning mic position and speaker configuration. Use brief pink noise, check reference delay, then compare the live TF with the stored tuning trace. Check settled response, coherence and crossover summation; listen through main/fill transitions. Stop noise after measuring. Investigate changes before adding EQ.", "Response checked", 23, 6},
   {"3-5 min", "Symptoms: likely causes", "No signal: mute, patch or wrong channel.\nLow coherence: weak signal, noise or wrong reference.\nLF dip: sub polarity, delay or level.\nComb filtering: overlapping sources or timing.\nHarsh/clipped: gain or limiter.\nClicks/dropouts: clock, rate or buffer.\nCheck one cause at a time; finish with Store, Apply and Verify. Unresolved faults mean Fail, not ready.", "Ready to verify", 23, 6},
   kStore,
   {"Apply", "Confirm the DSP state", "For a correction, enter it yourself in your DSP. FOH Gauge never applies it. If all checks passed, leave the DSP unchanged. Keep the mic, routing, speaker configuration and reference delay fixed, then confirm below. If fault isolation takes longer than five minutes, resolve it before declaring ready.", "DSP state confirmed", 21, 6},
   kVerify },
};
static const char* kGuideNames[] = {"Setup + reference delay", "Align sub to mains", "Align fills to mains", "Tuning", "Pre-doors check"};
class GuidePanel : public Panel {
public:
    explicit GuidePanel(AppModel& m) : Panel(m, "GuideInspector", "Guide") {
        addAndMakeVisible(form);
        form.canvas.onPaint = [this](Graphics& g) { paintContent(g); };
        helpText = "Step-by-step guides. The charts for each step appear in the Transfer Function and Phase panels.";
        updateGuides();
        guideBox.setSelectedId(guide + 1, dontSendNotification);
        guideBox.onChange = [this] { const bool interrupted = frameCapturePending; guide = guideBox.getSelectedId() - 1; step = 0; resetVerification(); say(interrupted ? "Guide measurement interrupted by navigation. Repeat the measurement; no verdict recorded." : String()); refresh(); };
        for (auto* c : std::initializer_list<Component*>{&guideBox, &stepLabel, &titleLabel, &body, &result, &readout, &polarity, &prev, &act, &note, &pass, &fail, &micDiagram}) form.canvas.addChildComponent(c);
        for (size_t i = 0; i < delayChoices.size(); ++i) {
            auto& row = delayChoices[i];
            form.canvas.addChildComponent(row);
            row.setRadioGroupId(27);
            row.setClickingTogglesState(true);
            if (i == 3) row.setTooltip("Flip sub polarity and use the displayed delay. Verify the phase slopes across the crossover band.");
            row.onClick = [this, i] { if (delayChoices[i].getToggleState()) selectedDelay = i; };
        }
        note.setMultiLine(true);
        note.setTextToShowWhenEmpty("Verification note: what changed and what you heard", C::textSecondary());
        note.onTextChange = [this] { updateVerdictButtons(); };
        pass.onClick = [this] { recordVerdict("PASS"); };
        fail.onClick = [this] { recordVerdict("FAIL"); };
        delayChoices[0].setToggleState(true, dontSendNotification);
        guideBox.setVisible(true);
        prev.onClick = [this] { if (step > 0) { --step; refresh(); } };
        act.onClick = [this] { action(kGuides[(size_t) guide][(size_t) step].act); };
        restyle();
        model.overlayBuilder = [this] { buildOverlay(); };
        refresh();
    }
    ~GuidePanel() override { model.overlayBuilder = {}; model.overlay = {}; }
    fohws::Size minSize() const override { return { foh::theme::ws::inspectorMinWidth, 780 }; }
    int priority() const override { return 60; }
    fohws::Json saveState() const override { auto j = fohws::Json::object(); j.set("guide", guide); j.set("step", step); j.set("verificationNote", verificationRecord.toStdString()); return j; }
    void loadState(const fohws::Json& j) override {
        if (!j.isObject()) return;
        const bool interrupted = frameCapturePending; resetVerification();
        setGuide(j["guide"].asInt(guide), j["step"].asInt(0));
        if (step >= (int) kGuides[(size_t) guide].size() - 2) step = std::max(0, (int) kGuides[(size_t) guide].size() - 3);
        verificationRecord = String(j["verificationNote"].asString());
        say(interrupted ? "Guide measurement interrupted by workspace navigation. Repeat the measurement; no verdict recorded."
            : verificationRecord.isEmpty() ? "Store a new before trace before Apply and Verify." : "Previous verification: " + verificationRecord);
        refresh(); }
    void setGuide(int g, int s) {
        int requested = jlimit(0, 4, g);
        guide = availableGuide(requested);
        step = guide == requested ? jlimit(0, (int) kGuides[(size_t) guide].size() - 1, s) : 0;
        guideBox.setSelectedId(guide + 1, dontSendNotification); refresh();
    }

    void restyle() {
        stepLabel.setFont(FohLookAndFeel::font(T::label, true)); stepLabel.setColour(Label::textColourId, C::accent());
        titleLabel.setFont(FohLookAndFeel::font(T::cardTitle - 2, true)); titleLabel.setColour(Label::textColourId, C::textPrimary());
        body.setFont(FohLookAndFeel::font(T::body)); body.setJustificationType(Justification::topLeft); body.setColour(Label::textColourId, C::textBody());
        result.setFont(FohLookAndFeel::font(T::small)); result.setJustificationType(Justification::topLeft); result.setColour(Label::textColourId, C::textBody());
        readout.setFont(FohLookAndFeel::numeric(T::body)); readout.setColour(Label::textColourId, C::textPrimary()); readout.setJustificationType(Justification::topLeft);
        polarity.setFont(FohLookAndFeel::font(T::small)); polarity.setColour(Label::textColourId, C::textSecondary()); polarity.setJustificationType(Justification::topLeft);
        makePrimary(act);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Rig) { updateGuides(); refresh(); }
        if (w & AppModel::Theme) { restyle(); repaint(); }
        if (w & (AppModel::Capture | AppModel::Settings | AppModel::Traces | AppModel::Levels)) {
            if (frameCapturePending) checkOperationContext();
            if (baselineValid && !frameCapturePending && (!model.findTrace("Guide before") || contextChanged())) {
                baselineValid = verified = false; verificationRecord.clear();
                step = (int) kGuides[(size_t) guide].size() - 3;
                say("Stored reference missing, or reference delay, device, sample rate or routing changed. Store a new before trace.");
            }
            refresh();
        }
    }
    void visibilityChanged() override {
        if (!isShowing() && frameCapturePending) {
            ++frameGeneration; frameCapturePending = false; verified = false;
            say("Guide measurement interrupted while hidden. Repeat the measurement; no verdict recorded.");
            refresh();
        }
        buildOverlay(); model.notify(AppModel::Overlay);
    }
    void parentHierarchyChanged() override { visibilityChanged(); }

    void paintContent(Graphics& g) {
        auto& steps = kGuides[(size_t) guide];
        for (size_t i = 0; i < steps.size() && i < rows.size(); ++i) { auto r = rows[i].toFloat(); bool done = (int) i < step, cur = (int) i == step;
            auto d = r.removeFromLeft(r.getHeight()).reduced(3);
            g.setColour(cur ? C::accent() : done ? C::ok() : C::textAxis());
            if (cur || done) g.fillEllipse(d); else g.drawEllipse(d.reduced(0.5f), 1.f);
            g.setColour(cur || done ? C::textOnAccent() : C::textAxis()); g.setFont(FohLookAndFeel::font(T::axisTick, true));
            g.drawText(done ? u8("\xe2\x9c\x93") : String((int) i + 1), d, Justification::centred);
            g.setColour(cur ? C::textPrimary() : C::textSecondary()); g.setFont(FohLookAndFeel::font(T::small, cur));
            g.drawText(u8(steps[i].progress), r.withTrimmedLeft(8), Justification::centredLeft); }
        if (model.capture.active && !progress.isEmpty()) { // convergence-settled capture
            auto b = progress.toFloat(); g.setColour(C::divider()); g.fillRoundedRectangle(b.withHeight(4), 2.f);
            g.setColour(C::accent()); g.fillRoundedRectangle(b.withHeight(4).withWidth(b.getWidth() * (float) jlimit(0.0, 1.0, model.conv.progress)), 2.f);
            g.setColour(C::textSecondary()); g.setFont(FohLookAndFeel::font(T::axisTick));
            g.drawText("Averaging " + model.capture.name + u8(" \xc2\xb7 ") + String((int) (model.conv.progress * 100)) + "%", b.withTrimmedTop(6), Justification::centredLeft); }

    }
    void resized() override { refresh(); }

private:
    bool checkOperationContext() {
        if (!model.engine.deviceFailed.load() && model.engine.state.load() == AudioEngine::Ready
            && model.engine.captureEpoch.load() == operationEpoch && model.engine.sampleRate.load() == operationRate && !model.paused) return true;
        ++frameGeneration; frameCapturePending = false; baselineValid = verified = false; verificationRecord.clear();
        if (step >= (int) kGuides[(size_t) guide].size() - 3) step = (int) kGuides[(size_t) guide].size() - 3;
        say("Input device, routing or sample rate interrupted the guide measurement. Check Setup and repeat; no verdict recorded.");
        return false;
    }
    int availableGuide(int requested) const {
        if (requested >= 3 && requested <= 4) return requested; // W4 tuning and W5 pre-doors apply to every rig
        for (auto id : foh::rigSteps(model.rigProfile))
            if (static_cast<int>(id) == requested + 1) return requested;
        return 0;
    }
    void updateGuides() {
        guideBox.clear(dontSendNotification);
        for (auto id : foh::rigSteps(model.rigProfile)) {
            int i = static_cast<int>(id) - 1;
            guideBox.addItem(String("W") + String(i + 1) + ": " + kGuideNames[i], i + 1);
        }
        for (int i = 3; i < 5; ++i) guideBox.addItem(String("W") + String(i + 1) + ": " + kGuideNames[i], i + 1);
        int selected = availableGuide(guide);
        if (selected != guide) { resetVerification(); guide = selected; step = 0; result.setText({}, dontSendNotification); }
        guideBox.setSelectedId(guide + 1, dontSendNotification);
    }
    void refresh() {
        auto& steps = kGuides[(size_t) guide]; step = jlimit(0, (int) steps.size() - 1, step); auto& st = steps[(size_t) step];
        form.setBounds(getLocalBounds());
        const int width = form.layoutWidth(260);
        const bool verifying = st.act == 22;
        const String bodyText = u8(st.body) + (verifying && guide == 1 ? "\nThe sum should rise in the crossover band rather than dip. Walk a few seats and listen." : String())
            + (verifying && guide == 2 ? "\nCheck phase overlap through the seam and listen from mains-only to fill-only positions." : String())
            + (st.act == 21 && suggestedChange.isNotEmpty() ? "\n" + suggestedChange : String());
        auto textHeight = [width](const Label& label, const String& text) {
            GlyphArrangement layout;
            layout.addJustifiedText(label.getFont(), text, 0, label.getFont().getAscent(),
                (float) (width - 2 * pad - 10), Justification::left);
            return (int) std::ceil(layout.getBoundingBox(0, -1, true).getBottom()) + 8;
        };
        using Placement = MicPlacementDiagram::Placement;
        const auto placement = st.chart == 3 ? Placement::None : guide == 0 && step > 0 ? Placement::OnAxis
            : guide == 1 ? Placement::SubMainCrossover
            : guide == 2 ? Placement::Seam : Placement::None;
        if (st.chart == 3) say(updateSubChoices());
        const int readoutH = jmax(24, textHeight(readout, readout.getText()));
        const int polarityH = jmax(76, textHeight(polarity, polarity.getText()));
        const int bodyH = textHeight(body, bodyText);
        const int titleH = jmax(26, textHeight(titleLabel, u8(st.title)));
        const int resultH = jmax(80, textHeight(result, result.getText()));
        // Reserve the full wrapped text and every optional control before laying out.
        const int height = 16 + 36 + (int) steps.size() * 22 + 10 + 16 + titleH + 6
            + 44 + (verifying ? 100 : 0) + (placement != Placement::None ? 110 : 0) + bodyH + 6
            + (st.chart == 3 ? readoutH + 112 + polarityH + 4 : 0) + 30 + resultH;
        form.sizeCanvas(width, jmax(780, height));
        auto c = form.canvas.getLocalBounds().reduced(pad, 8);
        guideBox.setBounds(c.removeFromTop(26)); c.removeFromTop(10);
        rows.clear(); for (size_t i = 0; i < steps.size(); ++i) rows.push_back(c.removeFromTop(22)); c.removeFromTop(10);
        stepLabel.setVisible(true); stepLabel.setText(("STEP " + String(step + 1) + " OF " + String((int) steps.size())), dontSendNotification); stepLabel.setBounds(c.removeFromTop(16));
        titleLabel.setVisible(true); titleLabel.setText(u8(st.title), dontSendNotification); titleLabel.setBounds(c.removeFromTop(titleH)); c.removeFromTop(6);
        auto buttons = c.removeFromBottom(36); c.removeFromBottom(8);
        for (auto& row : delayChoices) row.setVisible(st.chart == 3);
        note.setVisible(verifying); pass.setVisible(verifying); fail.setVisible(verifying);
        if (verifying) {
            auto verdict = c.removeFromBottom(28); c.removeFromBottom(6);
            pass.setBounds(verdict.removeFromLeft(verdict.getWidth() / 2)); fail.setBounds(verdict);
            note.setBounds(c.removeFromBottom(60)); c.removeFromBottom(6);
        }
        updateVerdictButtons();
        // W4/W5 currently contain only the shared frame and prescribe no mic move.
        // Retain the W1-W3 placement through Store / Apply / Verify.
        micDiagram.setPlacement(placement);
        if (placement != Placement::None) {
            micDiagram.setBounds(c.removeFromTop(104)); c.removeFromTop(6);
        }
        body.setVisible(true); body.setText(bodyText, dontSendNotification);
        body.setBounds(c.removeFromTop(bodyH)); c.removeFromTop(6);
        bool ro = st.chart == 3; readout.setVisible(ro); polarity.setVisible(ro);
        if (ro) {
            readout.setBounds(c.removeFromTop(readoutH));
            for (auto& row : delayChoices) row.setBounds(c.removeFromTop(28));
            polarity.setBounds(c.removeFromTop(polarityH)); c.removeFromTop(4);
        }
        progress = c.removeFromBottom(26); c.removeFromBottom(4);
        result.setVisible(true); result.setBounds(c);
        prev.setVisible(step > 0); prev.setEnabled(!frameCapturePending); act.setVisible(true); act.setButtonText(u8(st.action));
        if (step > 0) { prev.setBounds(buttons.removeFromLeft(72)); buttons.removeFromLeft(8); }
        act.setBounds(buttons);
        act.setEnabled(!frameCapturePending && (!(st.act == 20 || st.act == 22) || !model.capture.active) && ((st.act != 21 && st.act != 22) || baselineValid));
        buildOverlay(); model.notify(AppModel::Overlay); form.canvas.repaint();
    }

    void buildOverlay() {
        auto& ov = model.overlay; ov = {};
        if (!isShowing()) return;
        auto& st = kGuides[(size_t) guide][(size_t) step];
        auto* main = model.findTrace("Main"); auto* sub = model.findTrace("Sub"); auto& live = model.tf;
        auto add = [](std::vector<PlotComponent::Series>& out, const foh::TFResult& r, const std::vector<double>& v, Colour c, const float* dash, double maxF, float w, const String& label) {
            PlotComponent::Series s; s.label = label; s.c = c; s.dash = dash; s.maxF = maxF; s.width = w; logDecimate(r, v, s.f, s.v); out.push_back(s); };
        if (guide == 1 && xover.found) { ov.shade = {{xover.lo, xover.hi}}; ov.shadeLabel = "crossover ~" + String((int) std::sqrt(xover.lo * xover.hi)) + " Hz"; }
        switch (guide == 1 && st.act == 22 ? 4 : st.chart) {
            case 2: if (main) add(ov.mag, main->tf, main->tf.magDb, C::traceStored(), ST::dashStored, 1e9, D::traceStored(), "Mains (stored)");
                    add(ov.mag, live, live.magDb, C::traceSecondary(), nullptr, 1e9, D::trace(), "Sub (live)"); break;
            case 4: if (main) add(ov.mag, main->tf, main->tf.magDb, C::traceMainsMuted(), ST::dashComponent, 1e9, D::traceSecondary(), "Mains");
                    if (sub) add(ov.mag, sub->tf, sub->tf.magDb, C::traceSubMuted(), ST::dashComponent, 1e9, D::traceSecondary(), "Sub");
                    add(ov.mag, live, live.magDb, C::traceSum(), nullptr, 1e9, D::trace(), "Sum (live)");
                    [[fallthrough]];
            case 3: if (main) add(ov.phase, main->tf, main->tf.phaseDeg, C::accent(), nullptr, 250, D::trace(), "Mains phase");
                    if (sub) add(ov.phase, sub->tf, sub->tf.phaseDeg, C::traceSecondary(), st.chart == 4 ? ST::dashLimit : nullptr, 250, D::trace(), "Sub phase");
                    ov.phaseNote = "phase shown up to 250 Hz"; ov.phaseOnly250 = true; break;
            default: break;
        }
        if (st.act == 21 || st.act == 22) {
            if (baselineValid) add(ov.mag, baseline, baseline.magDb, C::traceStored(), ST::dashStored, 1e9, D::traceStored(), "Before (stored)");
            if (verified) add(ov.mag, after, after.magDb, C::traceSum(), nullptr, 1e9, D::trace(), "After (verified)");
            if (baselineValid) add(ov.phase, baseline, baseline.phaseDeg, C::traceStored(), ST::dashStored, 1e9, D::traceStored(), "Before (stored)");
            if (verified) add(ov.phase, after, after.phaseDeg, C::traceSum(), nullptr, 1e9, D::trace(), "After (verified)");
        }
        ov.active = !ov.mag.empty() || !ov.phase.empty() || !ov.shade.empty();
    }

    void say(const String& s) { result.setText(s, dontSendNotification); }
    void next() { if (step + 1 < (int) kGuides[(size_t) guide].size()) ++step; }
    String delayText() const {
        auto& d = model.delay; if (!model.delayLocked()) return {}; auto& a0 = d.arrivals[0];
        return u8("\xe2\x89\x88 ") + String(a0.distanceM, 2) + u8(" m \xc2\xb7 confidence ") + (d.confidence > 0.7 ? "high" : d.confidence > 0.35 ? "medium" : "LOW") + u8(" \xc2\xb7 polarity ") + (a0.inverted ? "INVERTED" : "normal")
             + (d.twoSimilarArrivals ? u8(" \xc2\xb7 \xe2\x96\xb2 two similar arrivals: is only one speaker on?") : "");
    }
    // Message-thread only: phase analysis and row text never run in the audio callback.
    String updateSubChoices() {
        choicesValid = false;
        readout.setText("Choose a delay: --", dontSendNotification);
        polarity.setText({}, dontSendNotification);
        for (auto& row : delayChoices) { row.setEnabled(false); row.setButtonText("--"); }
        auto* m = model.findTrace("Main"); auto* s = model.findTrace("Sub"); if (!m || !s) { return "Capture mains and sub first."; }
        xover = foh::acousticCrossover(m->tf, s->tf, 10.0); if (!xover.found) { return "No overlap within 10 dB between 30-300 Hz. Check levels."; }
        auto estimate = foh::subDelayFromPhase(m->tf, s->tf, xover.lo, xover.hi, 0,
            model.delayLocked() && model.delayWasSub ? &model.delay : nullptr);
        if (!estimate.valid) { return "No reliable phase estimate in the crossover band. Check signal and coherence."; }
        double period = estimate.periodMs;
        // A polarity flip shifts the primary candidate by half a period; prefer the smaller change.
        double invMs = estimate.primaryMs > 0 ? estimate.primaryMs - period / 2 : estimate.primaryMs + period / 2;
        // QA (Vega): the W2 delay comes from the crossover-band phase offset; +/-1 period are the alternatives.
        // The raw impulse peak is only a hint (it is dominated by the mains HF energy).
        double d = consoleMs(std::abs(estimate.primaryMs));
        auto alt = [&](double ms) { double v = consoleMs(ms); return (v >= 0 ? String("Mains +") : String("Sub +")) + String(std::abs(v), 2) + " ms"; };
        String hint; if (estimate.hasHint) hint = "Raw impulse peak: " + String(consoleMs(estimate.hintMs), 2) + " ms (hint only).";
        choiceMs = {estimate.primaryMs, estimate.alternativesMs[0], estimate.alternativesMs[1], invMs};
        const char* names[] = {"Primary: ", "-1 period: ", "+1 period: ", "Flip sub: "};
        for (size_t i = 0; i < delayChoices.size(); ++i) {
            delayChoices[i].setEnabled(true);
            delayChoices[i].setButtonText(String(names[i]) + alt(choiceMs[i]));
        }
        choicesValid = true;
        readout.setText("Choose a delay (XO " + String(estimate.xoHz, 0) + " Hz)", dontSendNotification);
        polarity.setText("Judge polarity from the phase traces. A flip shifts phase by 180 degrees."
            + (hint.isNotEmpty() ? "\n" + hint : String()), dontSendNotification);
        String out;
        out << "Crossover " << String(xover.lo, 0) << "-" << String(xover.hi, 0) << " Hz (centre ~" << String(estimate.xoHz, 0) << " Hz). Sub vs main phase " << String(estimate.phaseDeg, 0) << u8("\xc2\xb0 = ") << String(d, 2) << " ms ("
            << (estimate.primaryMs > 0 ? "sub LATE" : "sub EARLY") << "). Other answers differ by whole cycles (" << String(period, 2) << u8(" ms). Target: within ~60\xc2\xb0 and parallel across the band.");
        return out;
    }

    // Navigation retires all asynchronous guide work, including the earlier delay steps.
    void guidedDelay(bool sub, std::function<void()> then) {
        const int generation = ++frameGeneration;
        operationEpoch = model.engine.captureEpoch.load(); operationRate = model.engine.sampleRate.load();
        frameCapturePending = true; refresh();
        SafePointer<GuidePanel> safe(this);
        model.requestDelay(sub, [safe, generation, then = std::move(then)] {
            if (!safe || safe->frameGeneration != generation) return;
            if (!safe->checkOperationContext()) { safe->refresh(); return; }
            safe->frameCapturePending = false; then(); safe->refresh();
        });
    }
    void guidedCapture(const String& name, std::function<void(bool)> then) {
        const int generation = ++frameGeneration;
        operationEpoch = model.engine.captureEpoch.load(); operationRate = model.engine.sampleRate.load();
        frameCapturePending = true; refresh();
        SafePointer<GuidePanel> safe(this);
        model.beginCapture(name, [safe, generation, then = std::move(then)](bool settled) {
            if (!safe || safe->frameGeneration != generation) return;
            if (!safe->checkOperationContext()) { safe->refresh(); return; }
            safe->frameCapturePending = false; then(settled); safe->refresh();
        });
    }

    void action(int a) {
        if (frameCapturePending) return;
        String out; SafePointer<GuidePanel> safe(this);
        switch (a) {
            case 1: if (model.requestWorkspace) model.requestWorkspace("setup"); return;
            case 2: model.engine.gen = AudioEngine::Pink; model.notify(AppModel::Settings);
                    out = "Pink noise on at " + String(model.engine.genGainDb.load(), 1) + " dBFS. Watch the signal meters."; next(); break;
            case 3: say("Finding the delay...");
                    guidedDelay(false, [safe] { if (!safe) return; auto& p = *safe; String o;
                        if (p.model.delayLocked()) { p.model.lockDelayFromLast(); o = "Locked reference delay " + String(consoleMs(p.model.lockedDelayMs), 2) + " ms. " + p.delayText(); p.next(); }
                        else o = "No lock. Is pink noise playing and are both inputs showing level?";
                        p.say(o); p.refresh(); });
                    return;
            case 4: next(); refresh(); if (model.requestWorkspace) model.requestWorkspace("alignment"); return;

            case 12: say("Finding the sub/mains delay (LF-weighted, 3 s window)...");
                     guidedDelay(true, [safe] { if (!safe) return; auto& p = *safe;
                         if (!p.model.delayLocked()) { p.say("No valid delay result. Is pink noise playing, and only the mains on?"); return; }
                         p.model.lockDelayFromLast();
                         p.say("Locked delay " + String(consoleMs(p.model.lockedDelayMs), 2) + " ms. Averaging until the trace settles...");
                         p.guidedCapture("Main", [safe](bool settled) { if (!safe) return; auto& q = *safe;
                             q.say("Stored 'Main' (ref delay " + String(consoleMs(q.model.lockedDelayMs), 2) + " ms)." + String(settled ? "" : u8(" \xe2\x96\xb2 Unsettled: the average did not settle within 10 s."))); q.next(); q.refresh(); }); });
                     return;
            case 7: say("Averaging the sub until the trace settles...");
                    guidedCapture("Sub", [safe](bool settled) { if (!safe) return; auto& q = *safe;
                        q.say("Stored 'Sub' (same locked delay " + String(consoleMs(q.model.lockedDelayMs), 2) + " ms)." + String(settled ? "" : u8(" \xe2\x96\xb2 Unsettled: the average did not settle within 10 s.")));
                        auto* m = q.model.findTrace("Main"); auto* s = q.model.findTrace("Sub"); if (m && s) q.xover = foh::acousticCrossover(m->tf, s->tf, 10.0);
                        q.selectedDelay = 0; q.delayChoices[0].setToggleState(true, dontSendNotification);
                        q.next(); q.refresh(); });
                    return;
            case 8: {
                out = updateSubChoices();
                if (!choicesValid) break;
                const double ms = choiceMs[selectedDelay];
                const String instruction = String(selectedDelay == 3 ? "Flip sub polarity; delay " : "Delay ")
                    + (ms >= 0 ? "mains " : "sub ") + String(consoleMs(std::abs(ms)), 2) + " ms";
                suggestedChange = instruction;
                out = instruction + ". Store a before trace first, then enter this in your DSP.";
                next(); break;
            }
            case 10: say("Finding the fill arrival...");
                guidedDelay(false, [safe] { if (!safe) return; auto& p = *safe;
                    if (!p.model.delayLocked()) { p.say("No valid delay result. Is only the fill on?"); return; }
                    // findDelay works on the raw (uncompensated) reference, so arrivals are absolute
                    double fillMs = p.model.delay.arrivals[0].delayMs, diff = p.model.lockedDelayMs - fillMs; String o;
                    o << "Fill arrives at " << String(consoleMs(fillMs), 2) << " ms; mains at " << String(consoleMs(p.model.lockedDelayMs), 2) << " ms. "
                      << (diff > 0 ? "Fill arrives " + String(consoleMs(diff), 2) + " ms BEFORE the mains here. Suggested: delay the fill by ~" + String(consoleMs(diff), 2) + " ms (optionally a few ms more)."
                                   : "Fill already arrives after the mains (" + String(consoleMs(-diff), 2) + " ms). Check the seam position and which speaker is on.");
                    p.suggestedChange = o; p.next(); p.say(o); p.refresh(); });
                return;

            case 23: next(); break; // Manual checklist acknowledgement; no engine or DSP changes.
            case 20: case 22: captureFrame(a == 22); return;
            case 21: if (baselineValid) { verified = false; note.clear(); out = "DSP state confirmed by you. Re-measure, compare and record a verdict."; next(); } break;
            default: break;
        }
        say(out); refresh();
    }

    // All capture completion and comparison work runs on the message thread.
    bool contextChanged() const {
        return model.engine.deviceFailed.load() || model.engine.state.load() != AudioEngine::Ready || model.paused
            || model.engine.captureEpoch.load() != baselineEpoch || model.engine.sampleRate.load() != baselineRate
            || model.refDelayMs != baselineDelayMs;
    }
    static bool validTrace(const foh::TFResult& t) {
        const auto n = t.freq.size();
        if (!n || t.magDb.size() != n || t.coherence.size() != n
            || !std::isfinite(t.sampleRate) || t.sampleRate <= 0 || t.fftSize < 0
            || (!t.phaseDeg.empty() && t.phaseDeg.size() != n) || (!t.H.empty() && t.H.size() != n)) return false;
        for (double phase : t.phaseDeg) if (!std::isfinite(phase)) return false;
        for (auto h : t.H) if (!std::isfinite(h.real()) || !std::isfinite(h.imag())) return false;
        for (size_t i = 0; i < n; ++i)
            if (!std::isfinite(t.freq[i]) || t.freq[i] < 0 || (i && t.freq[i] <= t.freq[i - 1])
                || !std::isfinite(t.magDb[i]) || !std::isfinite(t.coherence[i])
                || t.coherence[i] < 0 || t.coherence[i] > 1) return false;
        return true;
    }
    void resetVerification() {
        ++frameGeneration; frameCapturePending = false; baselineValid = verified = false; verificationRecord.clear();
        baseline = {}; after = {}; suggestedChange.clear(); note.clear();
    }
    void updateVerdictButtons() {
        const bool ready = verified && baselineValid && !contextChanged() && model.findTrace("Guide before") && !frameCapturePending && note.getText().trim().isNotEmpty();
        pass.setEnabled(ready); fail.setEnabled(ready);
    }
    void recordVerdict(const String& verdict) {
        if (!verified || !baselineValid || contextChanged() || !model.findTrace("Guide before") || frameCapturePending || note.getText().trim().isEmpty()) return;
        verificationRecord = verdict + ": " + note.getText().trim();
        say(verificationRecord);
    }
    void captureFrame(bool verify) {
        if (model.capture.active || frameCapturePending) return;
        if (verify && (!baselineValid || !model.findTrace("Guide before") || contextChanged())) {
            baselineValid = verified = false; verificationRecord.clear(); step = (int) kGuides[(size_t) guide].size() - 3;
            say("Stored reference missing, or reference delay, device, sample rate or routing changed. Store a new before trace."); refresh(); return;
        }
        if (model.paused || model.engine.deviceFailed.load() || model.engine.state.load() != AudioEngine::Ready) {
            verified = false; say("Input device unavailable or measurement paused. Check Setup and repeat; no verdict recorded."); refresh(); return;
        }
        if (!verify) baselineValid = false;
        const auto epoch = model.engine.captureEpoch.load();
        const auto rate = model.engine.sampleRate.load();
        const auto faults = model.engine.signalFaults.load();
        operationEpoch = epoch; operationRate = rate;
        verified = false; verificationRecord.clear(); frameCapturePending = true;
        const int generation = ++frameGeneration;
        const int captureGuide = guide, captureStep = step;
        const String name = verify ? "Guide after" : "Guide before";
        // Reuse the frame's two trace slots; never remove other user traces.
        for (int i = model.traces.count() - 1; i >= 0; --i)
            if (model.traces.at(i).name == name.toStdString()) model.traces.remove(i);
        model.notify(AppModel::Traces);
        say("Re-measuring until settled..."); refresh();
        SafePointer<GuidePanel> safe(this);
        model.beginCapture(name, [safe, generation, captureGuide, captureStep, name, verify, epoch, rate, faults](bool settled) {
            if (!safe || safe->frameGeneration != generation) return;
            auto& p = *safe; p.frameCapturePending = false;
            if (p.guide != captureGuide || p.step != captureStep) { p.refresh(); return; }
            if (p.model.engine.deviceFailed.load() || p.model.engine.state.load() != AudioEngine::Ready
                || epoch != p.model.engine.captureEpoch.load() || rate != p.model.engine.sampleRate.load()
                || p.model.paused || (verify && (p.contextChanged() || !p.model.findTrace("Guide before")))) {
                p.baselineValid = p.verified = false; p.verificationRecord.clear(); p.step = (int) kGuides[(size_t) p.guide].size() - 3;
                p.say("Reference delay or routing changed, or input device/sample rate interrupted. Store a new before trace and repeat Apply and Verify."); p.refresh(); return;
            }
            if (faults != p.model.engine.signalFaults.load()) {
                p.say("Silence, clipping or invalid input during measurement. Check both inputs and repeat; no verdict recorded."); p.refresh(); return;
            }
            auto* trace = p.model.findTrace(name.toRawUTF8());
            if (!settled || !trace || trace->tf.freq.empty()) { p.say("No settled measurement. Check signal and coherence, then repeat. No verdict recorded."); p.refresh(); return; }
            if (!verify && !validTrace(trace->tf)) { p.say("Invalid before trace. Check input and repeat; no verdict recorded."); p.refresh(); return; }
            if (!verify) { p.baseline = trace->tf; p.baselineDelayMs = trace->refDelayMs; p.baselineEpoch = epoch; p.baselineRate = rate; p.baselineValid = true; p.next(); p.say(p.guide == 4 ? "Before trace stored. Correct the DSP yourself if needed, or confirm it is unchanged." : "Before trace stored. Enter the change yourself in your DSP."); }
            else {
                if (trace->refDelayMs != p.baselineDelayMs || p.model.engine.captureEpoch.load() != p.baselineEpoch) {
                    p.baselineValid = false;
                    p.step = (int) kGuides[(size_t) p.guide].size() - 3;
                    p.say("Reference delay or routing changed. Store a new before trace and repeat Apply and Verify."); p.refresh(); return;
                }
                p.after = trace->tf;
                if (p.baseline.freq != p.after.freq || p.baseline.fftSize != p.after.fftSize || p.baseline.sampleRate != p.after.sampleRate
                    || !validTrace(p.baseline) || !validTrace(p.after)) {
                    p.say("Frequency resolution mismatch or invalid trace. Store a new before trace and repeat; no verdict recorded.");
                    p.baselineValid = false; p.step = (int) kGuides[(size_t) p.guide].size() - 3; p.refresh(); return;
                }
                double delta = 0; int bins = 0;
                const auto& a = p.baseline; const auto& b = p.after;
                const size_t n = std::min({a.freq.size(), b.freq.size(), a.magDb.size(), b.magDb.size(), a.coherence.size(), b.coherence.size()});
                for (size_t i = 0; i < n; ++i)
                    if (a.freq[i] > 0 && a.freq[i] == b.freq[i] && a.coherence[i] >= 0.6 && a.coherence[i] <= 1 && b.coherence[i] >= 0.6 && b.coherence[i] <= 1
                        && std::isfinite(a.magDb[i]) && std::isfinite(b.magDb[i])) { delta += b.magDb[i] - a.magDb[i]; ++bins; }
                if (!std::isfinite(delta)) bins = 0;
                p.verified = bins > 0;
                p.say(bins ? "Mean change: " + String(delta / bins, 2) + " dB across " + String(bins)
                    + " coherent bins. Compare overlays and listen; add a note, then Pass/Fail."
                    : "No comparable coherent bins. Repeat the measurement; no verdict recorded.");
            }
            p.refresh();
        });
    }

    ScrollableForm form;
    MicPlacementDiagram micDiagram;
    ComboBox guideBox; Label stepLabel, titleLabel, body, result, readout, polarity; TextButton prev {"Back"}, act {"Do it"};
    TextEditor note; TextButton pass {"Pass"}, fail {"Fail"};
    foh::TFResult baseline, after; String suggestedChange, verificationRecord;
    double baselineRate = 48000;
    double operationRate = 48000; uint64_t operationEpoch = 0;
    double baselineDelayMs = 0; uint64_t baselineEpoch = 0;
    bool baselineValid = false, verified = false, frameCapturePending = false;
    int frameGeneration = 0;
    std::array<ToggleButton, 4> delayChoices;
    std::array<double, 4> choiceMs {};
    size_t selectedDelay = 0;
    bool choicesValid = false;
    std::vector<Rectangle<int>> rows; Rectangle<int> progress;
    int guide = 1, step = 0; foh::Band2 xover;
};

std::unique_ptr<Panel> makeGuidePanel(AppModel& m) {
    auto p = std::make_unique<GuidePanel>(m);
    if (std::getenv("FOH_GUIDE") || std::getenv("FOH_STEP")) p->setGuide(std::getenv("FOH_GUIDE") ? std::atoi(std::getenv("FOH_GUIDE")) : 1, std::getenv("FOH_STEP") ? std::atoi(std::getenv("FOH_STEP")) : 0); // dev/screenshot aid
    return p;
}
