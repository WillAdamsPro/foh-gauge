// Guide / inspector panel: a narrow stepper. Its charts are drawn as an overlay in the Transfer
// Function and Phase panels (AppModel::overlay), so the guide works inside any workspace layout.
#include "Panels.h"
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;
namespace ST = foh::theme::stroke;
struct Step { const char* progress; const char* title; const char* body; const char* action; int act; int chart; };
static const std::vector<std::vector<Step>> kGuides = {
 { {"Connect", "Connect your gear", "UMC404HD: Out 1 to the console. Out 2 to In 2 with a 1/4\" TRS cable (reference loopback; TRS keeps it off phantom). ECM8000 to In 1 (XLR, +48 V on). Pick the device and channels on Setup at 48 kHz.", "Open Setup", 1, 0},
   {"Signal", "Signal check", "Start pink noise and bring the console up to a comfortable level. Both inputs should sit near -20 dBFS (top bar), never clipping. Mic 1-3 m on-axis from ONE main, ear height, away from walls.", "Start pink noise", 2, 6},
   {"Delay", "Find the reference delay", "The tallest spike is the direct sound; smaller later spikes are reflections. If two spikes are similar, check only one speaker is on.", "Find + lock delay", 3, 7},
   {"Check", "Check the trace", "Phase should be fairly flat and coherence high (above ~0.8) above 200 Hz. Low coherence = don't trust that region.", "Open transfer function", 4, 6},
   {"Save", "Save Position 1", "Store this trace so you can compare later. If you move the mic, find the delay again.", "Store 'Position 1'", 5, 6} },
 { {"Measure mains", "Measure the mains alone", "Mute the subs. Mic where sub and mains are about equally loud (often FOH). Pink noise on. This finds and locks the delay to the mains, then stores 'Main'.", "Capture mains \xe2\x97\x8f", 12, 1},
   {"Measure sub", "Measure the sub alone", "Mute the mains, unmute the subs. Don't find the delay again: the comparison depends on the same timing. The dashed line is your stored mains.", "Capture sub \xe2\x97\x8f", 7, 2},
   {"Find delay", "Find the delay", "Compare phase across the shaded crossover band (up to 250 Hz). Choose matching slopes and the least delay. Judge polarity from phase, not the impulse.", "Use this delay", 8, 3},
   {"Apply & verify", "Apply and verify", "Enter the change in your DSP and unmute both. The sum (white) should RISE in the crossover band, up to ~+6 dB where the two are equal, not dip. Walk a few seats.", "Capture both \xe2\x97\x8f", 9, 4},
   {"Done", "Sub and mains aligned", "", "Measure again", 13, 5} },
 { {"Mains", "Measure the mains", "Mains already tuned. Mic in the seam: where main and fill are about equally loud, inside the fill's area. Fill muted. This finds and locks the delay to the mains.", "Find + lock delay", 3, 6},
   {"Fill", "Measure the fill", "Mute the mains, unmute the fill. The app finds the fill's arrival and compares it with the locked mains time.", "Find fill arrival", 10, 7},
   {"Set delay", "Set the fill delay", "Delay the fill by the amount shown. Many engineers add a few ms more so the mains arrive first and the image stays on stage (rule of thumb; trust your ears). Fill only as loud as needed.", "Open transfer function", 4, 6},
   {"Verify", "Verify both on", "Unmute both. Mute/unmute the fill and watch which spike moves. Phase should overlap in the mid/HF where they overlap. Walk from mains-only to fill-only zones and listen.", "Store 'Main+Fill'", 11, 6} },
};
static const char* kGuideNames[] = {"Setup + reference delay", "Align sub to mains", "Align fills to mains"};
class GuidePanel : public Panel {
public:
    explicit GuidePanel(AppModel& m) : Panel(m, "GuideInspector", "Guide") {
        helpText = "Step-by-step guides. The charts for each step appear in the Transfer Function and Phase panels.";
        updateGuides();
        guideBox.setSelectedId(guide + 1, dontSendNotification);
        guideBox.onChange = [this] { guide = guideBox.getSelectedId() - 1; step = 0; result.setText({}, dontSendNotification); refresh(); };
        for (auto* c : std::initializer_list<Component*>{&guideBox, &stepLabel, &titleLabel, &body, &result, &readout, &polarity, &prev, &act, &doneActions}) addChildComponent(c);
        for (size_t i = 0; i < delayChoices.size(); ++i) {
            auto& row = delayChoices[i];
            addChildComponent(row);
            row.setRadioGroupId(27);
            row.setClickingTogglesState(true);
            if (i == 3) row.setTooltip("Flip sub polarity and use the displayed delay. Verify the phase slopes across the crossover band.");
            row.onClick = [this, i] { if (delayChoices[i].getToggleState()) selectedDelay = i; };
        }
        delayChoices[0].setToggleState(true, dontSendNotification);
        guideBox.setVisible(true);
        prev.onClick = [this] { if (step > 0) { --step; refresh(); } };
        act.onClick = [this] { action(kGuides[(size_t) guide][(size_t) step].act); };
        doneActions.setText("1. Walk a few seats and listen.\n2. Save the DSP settings.\n3. Note the delay values in your log.", dontSendNotification);
        restyle();
        model.overlayBuilder = [this] { buildOverlay(); };
        refresh();
    }
    ~GuidePanel() override { model.overlayBuilder = {}; model.overlay = {}; }
    fohws::Size minSize() const override { return { foh::theme::ws::inspectorMinWidth, guide == 1 ? 660 : 360 }; }
    int priority() const override { return 60; }
    fohws::Json saveState() const override { auto j = fohws::Json::object(); j.set("guide", guide); j.set("step", step); return j; }
    void loadState(const fohws::Json& j) override {
        if (j.isObject()) setGuide(j["guide"].asInt(guide), j["step"].asInt(0));
    }
    void setGuide(int g, int s) {
        int requested = jlimit(0, 2, g);
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
        doneActions.setFont(FohLookAndFeel::font(T::body)); doneActions.setColour(Label::textColourId, C::textBody()); doneActions.setJustificationType(Justification::topLeft);
        makePrimary(act);
    }
    void modelChanged(int w) override {
        if (w & AppModel::Rig) { updateGuides(); refresh(); }
        if (w & AppModel::Theme) { restyle(); repaint(); }
        if (w & AppModel::Capture) repaint(progress);
    }
    void visibilityChanged() override { buildOverlay(); model.notify(AppModel::Overlay); }

    void paint(Graphics& g) override {
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
        if (!summary.isEmpty()) {
            auto r = summary; g.setFont(FohLookAndFeel::font(T::small));
            if (summaryRows.empty()) { g.setColour(C::textSecondary()); g.drawText("Nothing measured yet in this guide.", r.removeFromTop(22), Justification::centredLeft); }
            for (auto& [k, v] : summaryRows) { auto row = r.removeFromTop(40); g.setColour(C::textSecondary()); g.drawText(k.toUpperCase(), row.removeFromTop(16), Justification::centredLeft);
                g.setColour(C::textPrimary()); g.setFont(FohLookAndFeel::numeric(T::small, false)); g.drawText(v, row, Justification::centredLeft); g.setFont(FohLookAndFeel::font(T::small));
                g.setColour(C::divider()); g.fillRect(row.getX(), row.getBottom() - 1, row.getWidth(), 1); }
        }
    }
    void resized() override { refresh(); }

private:
    int availableGuide(int requested) const {
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
        int selected = availableGuide(guide);
        if (selected != guide) { guide = selected; step = 0; result.setText({}, dontSendNotification); }
        guideBox.setSelectedId(guide + 1, dontSendNotification);
    }
    void refresh() {
        auto& steps = kGuides[(size_t) guide]; step = jlimit(0, (int) steps.size() - 1, step); auto& st = steps[(size_t) step];
        auto c = getLocalBounds().reduced(pad, 8);
        guideBox.setBounds(c.removeFromTop(26)); c.removeFromTop(10);
        rows.clear(); for (size_t i = 0; i < steps.size(); ++i) rows.push_back(c.removeFromTop(22)); c.removeFromTop(10);
        stepLabel.setVisible(true); stepLabel.setText(("STEP " + String(step + 1) + " OF " + String((int) steps.size())), dontSendNotification); stepLabel.setBounds(c.removeFromTop(16));
        titleLabel.setVisible(true); titleLabel.setText(u8(st.title), dontSendNotification); titleLabel.setBounds(c.removeFromTop(26)); c.removeFromTop(6);
        auto buttons = c.removeFromBottom(36); c.removeFromBottom(8);
        summary = {};
        for (auto& row : delayChoices) row.setVisible(st.chart == 3);
        if (st.chart == 5) { // Done
            body.setVisible(false); readout.setVisible(false); polarity.setVisible(false); result.setVisible(false); prev.setVisible(false);
            summary = c.removeFromTop(jmax(22, (int) summaryRows.size() * 40)); c.removeFromTop(10);
            doneActions.setVisible(true); doneActions.setBounds(c.removeFromTop(66));
            act.setVisible(true); act.setButtonText("Measure again"); act.setBounds(buttons);
            repaint(); return;
        }
        doneActions.setVisible(false);
        body.setVisible(true); body.setText(u8(st.body), dontSendNotification);
        int bodyH = jmin(c.getHeight() / 2, roundToInt(body.getFont().getHeight() * 1.25f * (2 + body.getFont().getStringWidthFloat(body.getText()) / jmax(1, c.getWidth() - 8))));
        if (st.chart == 3) bodyH = jmin(bodyH, 96);
        body.setBounds(c.removeFromTop(bodyH)); c.removeFromTop(6);
        bool ro = st.chart == 3; readout.setVisible(ro); polarity.setVisible(ro);
        if (ro) {
            say(updateSubChoices());
            readout.setBounds(c.removeFromTop(24));
            for (auto& row : delayChoices) row.setBounds(c.removeFromTop(28));
            polarity.setBounds(c.removeFromTop(76)); c.removeFromTop(4);
        }
        progress = c.removeFromBottom(26); c.removeFromBottom(4);
        result.setVisible(true); result.setBounds(c);
        prev.setVisible(step > 0); act.setVisible(true); act.setButtonText(u8(st.action));
        if (step > 0) { prev.setBounds(buttons.removeFromLeft(72)); buttons.removeFromLeft(8); }
        act.setBounds(buttons);
        buildOverlay(); model.notify(AppModel::Overlay); repaint();
    }

    void buildOverlay() {
        auto& ov = model.overlay; ov = {};
        if (!isShowing()) return;
        auto& st = kGuides[(size_t) guide][(size_t) step];
        auto* main = model.findTrace("Main"); auto* sub = model.findTrace("Sub"); auto& live = model.tf;
        auto add = [](std::vector<PlotComponent::Series>& out, const foh::TFResult& r, const std::vector<double>& v, Colour c, const float* dash, double maxF, float w, const String& label) {
            PlotComponent::Series s; s.label = label; s.c = c; s.dash = dash; s.maxF = maxF; s.width = w; logDecimate(r, v, s.f, s.v); out.push_back(s); };
        if (guide == 1 && xover.found) { ov.shade = {{xover.lo, xover.hi}}; ov.shadeLabel = "crossover ~" + String((int) std::sqrt(xover.lo * xover.hi)) + " Hz"; }
        switch (st.chart) {
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
        summaryRows = { {"Crossover", String(xover.lo, 0) + "-" + String(xover.hi, 0) + " Hz"},
                        {"Reference delay", String(consoleMs(model.lockedDelayMs), 2) + " ms"} };
        return out;
    }

    void action(int a) {
        String out; SafePointer<GuidePanel> safe(this);
        switch (a) {
            case 1: if (model.requestWorkspace) model.requestWorkspace("setup"); return;
            case 2: model.engine.gen = AudioEngine::Pink; model.notify(AppModel::Settings);
                    out = "Pink noise on at " + String(model.engine.genGainDb.load(), 1) + " dBFS. Watch the signal meters."; next(); break;
            case 3: say("Finding the delay...");
                    model.requestDelay(false, [safe] { if (!safe) return; auto& p = *safe; String o;
                        if (p.model.delayLocked()) { p.model.lockDelayFromLast(); o = "Locked reference delay " + String(consoleMs(p.model.lockedDelayMs), 2) + " ms. " + p.delayText(); p.next(); }
                        else o = "No lock. Is pink noise playing and are both inputs showing level?";
                        p.say(o); p.refresh(); });
                    return;
            case 4: if (model.requestWorkspace) model.requestWorkspace("alignment"); return;
            case 5: if (model.storeTrace("Position 1")) out = "Stored 'Position 1'."; break;
            case 12: say("Finding the sub/mains delay (LF-weighted, 3 s window)...");
                     model.requestDelay(true, [safe] { if (!safe) return; auto& p = *safe;
                         if (!p.model.delayLocked()) { p.say("No valid delay result. Is pink noise playing, and only the mains on?"); return; }
                         p.model.lockDelayFromLast();
                         p.say("Locked delay " + String(consoleMs(p.model.lockedDelayMs), 2) + " ms. Averaging until the trace settles...");
                         p.model.beginCapture("Main", [safe](bool settled) { if (!safe) return; auto& q = *safe;
                             q.say("Stored 'Main' (ref delay " + String(consoleMs(q.model.lockedDelayMs), 2) + " ms)." + String(settled ? "" : u8(" \xe2\x96\xb2 Unsettled: the average did not settle within 10 s."))); q.next(); q.refresh(); }); });
                     return;
            case 7: say("Averaging the sub until the trace settles...");
                    model.beginCapture("Sub", [safe](bool settled) { if (!safe) return; auto& q = *safe;
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
                summaryRows.push_back({"Selected", instruction});
                out = instruction + ". Enter this in your DSP, then capture both to verify.";
                next(); break;
            }
            case 9: { if (model.storeTrace("Both")) { auto* m = model.findTrace("Main"); auto* s = model.findTrace("Sub"); auto* b = model.findTrace("Both");
                        if (m && s && b && xover.found) { auto vs = foh::virtualSum(m->tf, s->tf); double gain = 0, ref = 0; int n = 0;
                            for (size_t k = 1; k < b->tf.freq.size(); ++k) if (b->tf.freq[k] >= xover.lo && b->tf.freq[k] <= xover.hi) { gain += b->tf.magDb[k] - std::max(m->tf.magDb[k], s->tf.magDb[k]); ref += vs.magDb[k] - std::max(m->tf.magDb[k], s->tf.magDb[k]); ++n; }
                            if (n) { gain /= n; ref /= n; summaryRows.push_back({"Sum in crossover", (gain >= 0 ? "+" : "") + String(gain, 1) + " dB (predicted " + (ref >= 0 ? "+" : "") + String(ref, 1) + " dB)"}); } }
                        out = "Stored 'Both'."; next(); } break; }
            case 10: say("Finding the fill arrival...");
                model.requestDelay(false, [safe] { if (!safe) return; auto& p = *safe;
                    if (!p.model.delayLocked()) { p.say("No valid delay result. Is only the fill on?"); return; }
                    // findDelay works on the raw (uncompensated) reference, so arrivals are absolute
                    double fillMs = p.model.delay.arrivals[0].delayMs, diff = p.model.lockedDelayMs - fillMs; String o;
                    o << "Fill arrives at " << String(consoleMs(fillMs), 2) << " ms; mains at " << String(consoleMs(p.model.lockedDelayMs), 2) << " ms. "
                      << (diff > 0 ? "Fill arrives " + String(consoleMs(diff), 2) + " ms BEFORE the mains here. Suggested: delay the fill by ~" + String(consoleMs(diff), 2) + " ms (optionally a few ms more)."
                                   : "Fill already arrives after the mains (" + String(consoleMs(-diff), 2) + " ms). Check the seam position and which speaker is on.");
                    p.next(); p.say(o); p.refresh(); });
                return;
            case 11: if (model.storeTrace("Main+Fill")) out = "Stored 'Main+Fill'. Compare with 'Main' in the Traces panel."; break;
            case 13: step = 0; selectedDelay = 0;
                delayChoices[0].setToggleState(true, dontSendNotification); break;
            default: break;
        }
        say(out); refresh();
    }

    ComboBox guideBox; Label stepLabel, titleLabel, body, result, readout, polarity, doneActions; TextButton prev {"Back"}, act {"Do it"};
    std::array<ToggleButton, 4> delayChoices;
    std::array<double, 4> choiceMs {};
    size_t selectedDelay = 0;
    bool choicesValid = false;
    std::vector<Rectangle<int>> rows; Rectangle<int> progress, summary;
    int guide = 1, step = 0; foh::Band2 xover; std::vector<std::pair<String, String>> summaryRows;
};

std::unique_ptr<Panel> makeGuidePanel(AppModel& m) {
    auto p = std::make_unique<GuidePanel>(m);
    if (std::getenv("FOH_GUIDE") || std::getenv("FOH_STEP")) p->setGuide(std::getenv("FOH_GUIDE") ? std::atoi(std::getenv("FOH_GUIDE")) : 1, std::getenv("FOH_STEP") ? std::atoi(std::getenv("FOH_STEP")) : 0); // dev/screenshot aid
    return p;
}
