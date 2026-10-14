#include "SqConsoleWindow.h"
#include <cstdlib>
#include <cmath>
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif
namespace foh::sq {
using namespace juce;
SqConsolePanel::SqConsolePanel() : controller([this](const SqController::Batch& batch) { return connection && connection->enqueue(batch); }) {
    for (auto* c : std::initializer_list<Component*>{&enabled,&dryRun,&delayIn,&host,&frequency,&gain,&delay,&proposed,&target,&band,&mode,&connect,&preview,&apply,&undo,&status,&help}) addAndMakeVisible(c);
    dryRun.setToggleState(true,dontSendNotification);
    host.setTextToShowWhenEmpty("Console address",Colours::grey);
    frequency.setText("1000"); gain.setText("-3"); delay.setText("30");
    frequency.setTextToShowWhenEmpty("Frequency (Hz)",Colours::grey); gain.setTextToShowWhenEmpty("Gain (dB)",Colours::grey); delay.setTextToShowWhenEmpty("Delay (ms)",Colours::grey);
    for (int i = 1; i <= 48; ++i) target.addItem("Input " + String(i), i);
    target.addItem("Aux 1",49); target.addItem("Matrix 1 (stereo)",50); target.setSelectedId(49);
    band.addItem("LF",1); band.addItem("LM",2); band.addItem("HM",3); band.addItem("HF",4); band.setSelectedId(3);
    mode.addItem("PEQ frequency / gain",1); mode.addItem("Output delay time / in",2); mode.setSelectedId(1);
    proposed.setMultiLine(true); proposed.setReadOnly(true);
    help.setText("Fields: frequency (Hz), gain (dB), output delay (ms). Enter measured corrections, review, then confirm. Q, shape and PEQ in are disabled; firmware writes await capture 3.",dontSendNotification);
    help.setJustificationType(Justification::topLeft);
    enabled.onClick = [this] { if (!enabled.getToggleState()) disconnect(); refresh(); };
    connect.onClick = [this] {
        disconnect();
        if (!enabled.getToggleState() || host.getText().trim().isEmpty()) { status.setText("Enable connection and enter an address",dontSendNotification); return; }
        controller.startConnection(); connection = std::make_unique<SqConnection>(host.getText().trim()); refresh();
    };
    host.onTextChange = [this] { disconnect(); refresh(); };
    auto edited = [this] { confirmation.close(); controller.cancel(); proposed.clear(); refresh(); };
    for (auto* box : {&target,&band,&mode}) box->onChange = edited;
    for (auto* editor : {&frequency,&gain,&delay}) editor->onTextChange = edited;
    delayIn.onClick = edited; dryRun.onClick = edited;
    preview.onClick = [this] { review(false); };
    undo.onClick = [this] { review(true); if (!controller.changes().empty()) confirm(); };
    apply.onClick = [this] { confirm(); };
    status.setText("Disconnected; firmware unknown",dontSendNotification);
    setSize(720,470); refresh(); startTimerHz(20);
}
SqConsolePanel::~SqConsolePanel() { stopTimer(); confirmation.close(); disconnect(); }
void SqConsolePanel::disconnect() {
    confirmation.close(); connection.reset(); controller.disconnect(); wasConnected = false;
    status.setText("Disconnected; firmware unknown",dontSendNotification); proposed.clear(); refresh();
}
void SqConsolePanel::restoreSession(const foh::Json& j) {
    disconnect(); controller.restoreSession(j);
    enabled.setToggleState(false,dontSendNotification); dryRun.setToggleState(true,dontSendNotification);
    refresh();
}
Strip SqConsolePanel::strip() const {
    const auto n = target.getSelectedId();
    if (n <= 48) return {StripKind::Input,n};
    return {n == 49 ? StripKind::Aux : StripKind::Matrix,1};
}
void SqConsolePanel::review(bool restore) {
    confirmation.close();
    bool ok = false;
    if (restore) ok = controller.proposeUndo();
    else {
        auto parseNumber = [](const String& text) -> std::optional<double> {
            const auto s = text.trim().toStdString(); char* end{};
            const double n = std::strtod(s.c_str(),&end);
            if (s.empty() || end != s.c_str() + s.size() || !std::isfinite(n)) return {};
            return n;
        };
        const auto f = parseNumber(frequency.getText()), g = parseNumber(gain.getText()), d = parseNumber(delay.getText());
        if ((mode.getSelectedId() == 1 && (!f || !g)) || (mode.getSelectedId() == 2 && !d)) {
            controller.cancel(); proposed.setText("Enter valid numeric correction values"); refresh(); return;
        }
        const auto b = static_cast<Band>(band.getSelectedId() - 1);
        if (mode.getSelectedId() == 1) ok = controller.propose({{{strip(),Parameter::Frequency,b},*f},{{strip(),Parameter::Gain,b},*g}});
        else ok = controller.propose({{{strip(),Parameter::DelayTime,b},*d},{{strip(),Parameter::DelayIn,b},delayIn.getToggleState() ? 1.0 : 0.0}});
    }
    String text;
    if (ok) for (const auto& c : controller.changes()) text += String(c.description) + "\n";
    else text = String(controller.message());
    proposed.setText(text); refresh();
}
void SqConsolePanel::confirm() {
    if (controller.changes().empty()) return;
    const bool dry = dryRun.getToggleState();
    const auto reviewed = controller.changes();
    String text = dry ? "Dry-run: log these frames with no parameter bytes sent.\n\n" : "Send these changes to the connected console?\n\n";
    for (const auto& c : reviewed) text += String(c.description) + "\n";
    confirmation = AlertWindow::showScopedAsync(MessageBoxOptions().withIconType(MessageBoxIconType::QuestionIcon)
        .withTitle(dry ? "Confirm dry-run" : "Confirm console changes").withMessage(text)
        .withButton("Confirm").withButton("Cancel").withAssociatedComponent(this),
        [sp = SafePointer<SqConsolePanel>(this),reviewed,dry](int result) {
            if (!sp || result != 1) return;
            const auto& now = sp->controller.changes();
            bool same = now.size() == reviewed.size();
            for (std::size_t i = 0; same && i < now.size(); ++i) same = now[i].before == reviewed[i].before && now[i].after == reviewed[i].after;
            if (!same) { sp->status.setText("Evidence changed: review again",dontSendNotification); return; }
            sp->controller.apply(true,dry);
            String log(sp->controller.message());
            for (const auto& bytes : sp->controller.dryRunFrames()) log += "\n" + String(SqCodec::hex(bytes));
            sp->proposed.setText(log); sp->refresh();
        });
}
void SqConsolePanel::timerCallback() {
    if (!connection) return;
    const auto update = connection->poll();
    if (update.connected && !wasConnected) controller.startConnection();
    if (update.connected && controller.versionBytes() != update.version) controller.firmware(update.version);
    for (const auto& bytes : update.echoes) controller.echo(bytes);
    if (!update.connected) controller.disconnect();
    wasConnected = update.connected;
    String label(update.status);
    if (!update.version.empty()) label += "\nFirmware bytes: " + String(SqCodec::hex(update.version));
    status.setText(label,dontSendNotification); refresh();
}
void SqConsolePanel::refresh() {
    connect.setEnabled(enabled.getToggleState());
    apply.setEnabled(!controller.changes().empty()); undo.setEnabled(controller.hasUndo());
    const bool peq = mode.getSelectedId() == 1;
    frequency.setEnabled(peq); gain.setEnabled(peq); band.setEnabled(peq);
    delay.setEnabled(!peq && strip().kind != StripKind::Input); delayIn.setEnabled(delay.isEnabled());
}
void SqConsolePanel::resized() {
    auto r = getLocalBounds().reduced(16);
    enabled.setBounds(r.removeFromTop(30));
    auto row = r.removeFromTop(30); connect.setBounds(row.removeFromRight(110)); host.setBounds(row.reduced(0,2));
    status.setBounds(r.removeFromTop(48));
    row = r.removeFromTop(34); target.setBounds(row.removeFromLeft(210)); mode.setBounds(row.removeFromLeft(265).reduced(4,0)); band.setBounds(row.reduced(4,0));
    row = r.removeFromTop(34); frequency.setBounds(row.removeFromLeft(160).reduced(2)); gain.setBounds(row.removeFromLeft(130).reduced(2)); delay.setBounds(row.removeFromLeft(130).reduced(2)); delayIn.setBounds(row);
    dryRun.setBounds(r.removeFromTop(30)); help.setBounds(r.removeFromTop(62));
    auto buttons = r.removeFromBottom(34); preview.setBounds(buttons.removeFromLeft(230)); apply.setBounds(buttons.removeFromLeft(230).reduced(4,0)); undo.setBounds(buttons.reduced(4,0));
    proposed.setBounds(r.reduced(0,4));
}
SqConsoleWindow::SqConsoleWindow() : DocumentWindow("Console — SQ (experimental)",Colours::darkgrey,DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true); setContentOwned(new SqConsolePanel(),true); setResizable(true,false);
    setResizeLimits(720,470,1200,1000); centreWithSize(720,470); setVisible(true);
#if JUCE_MAC
    if (auto* p = getPeer()) { NSWindow* native = [(NSView*)p->getNativeHandle() window]; native.animationBehavior = NSWindowAnimationBehaviorNone; }
#endif
}
void SqConsoleWindow::closeButtonPressed() { panel().disconnect(); setVisible(false); }
}
