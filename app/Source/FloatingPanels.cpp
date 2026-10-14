#include "FloatingPanels.h"
#if JUCE_MAC
#import <AppKit/AppKit.h>
#endif
using namespace juce;
namespace WS = foh::theme::ws;
static const foh::theme::Theme& TH() { return foh::theme::ThemeManager::get().current(); }

std::string displayIdFor(Rectangle<int> r) {
    auto* d = Desktop::getInstance().getDisplays().getDisplayForRect(r); if (!d) return {};
    auto a = d->totalArea; return (String(a.getX()) + "," + String(a.getY()) + "," + String(a.getWidth()) + "x" + String(a.getHeight())).toStdString();
}
std::vector<fohws::DisplayInfo> currentDisplays() {
    std::vector<fohws::DisplayInfo> out;
    for (auto& d : Desktop::getInstance().getDisplays().displays) { auto a = d.totalArea, u = d.userArea;
        out.push_back({ (String(a.getX()) + "," + String(a.getY()) + "," + String(a.getWidth()) + "x" + String(a.getHeight())).toStdString(), { u.getX(), u.getY(), u.getWidth(), u.getHeight() }, d.isMain }); }
    return out;
}

// 20x20 title-bar button: Pin (toggle, accent + filled when on) and Dock (arrow to tray)
struct FloatingPanelWindow::TitleButton : Button {
    bool isPin; TitleButton(bool p) : Button(p ? "Pin" : "Dock panel"), isPin(p) { setTooltip(p ? "Keep on top" : "Dock panel"); setClickingTogglesState(p); }
    void paintButton(Graphics& g, bool over, bool down) override {
        auto& t = TH(); auto r = getLocalBounds().toFloat();
        if (down) { g.setColour(WS::pressedOverlay(t)); g.fillRoundedRectangle(r, 4.f); } else if (over) { g.setColour(WS::hoverOverlay(t)); g.fillRoundedRectangle(r, 4.f); }
        bool on = isPin && getToggleState(); Colour c = on ? t.accent : over ? t.text.primary : t.text.axis; g.setColour(c);
        auto b = r.withSizeKeepingCentre(12, 12);
        if (isPin) { Path p; p.addRoundedRectangle(b.getX() + 3, b.getY(), 6, 6, 1.5f); p.addTriangle(b.getX() + 1, b.getY() + 6, b.getRight() - 1, b.getY() + 6, b.getCentreX(), b.getY() + 9);
                     if (on) g.fillPath(p); else g.strokePath(p, PathStrokeType(1.f)); g.fillRect(b.getCentreX() - 0.5f, b.getY() + 8, 1.f, 4.f); }
        else { g.drawLine(b.getCentreX(), b.getY(), b.getCentreX(), b.getY() + 7, 1.3f); Path a; a.addTriangle(b.getCentreX() - 3.5f, b.getY() + 5, b.getCentreX() + 3.5f, b.getY() + 5, b.getCentreX(), b.getY() + 9); g.fillPath(a);
               g.fillRect(b.getX() + 1, b.getBottom() - 1.5f, b.getWidth() - 2, 1.3f); }
    }
};

FloatingPanelWindow::FloatingPanelWindow(Panel& p, fohws::FloatingPanel& state)
    : DocumentWindow(p.title() + juce::String::fromUTF8(" \xe2\x80\x94 FOH Gauge"), TH().surfaces.background, DocumentWindow::closeButton), panel(p), st(state) {
    setUsingNativeTitleBar(false); setTitleBarHeight(WS::floatTitleBarHeight); setTitleBarTextCentred(false);
    setContentNonOwned(&panel, false);
    setResizable(true, false); setResizeLimits(WS::floatMinWidth, WS::floatMinHeight, 4000, 3000);
    pin = std::make_unique<TitleButton>(true); dock = std::make_unique<TitleButton>(false);
    Component::addAndMakeVisible(*pin); Component::addAndMakeVisible(*dock);
    pin->onClick = [this] { setPinned(pin->getToggleState()); if (onChanged) onChanged(); };
    dock->onClick = [this] { if (onDock) onDock(); };
    restoreToDisplays(currentDisplays());
    setPinned(st.alwaysOnTop); restyle();
#if JUCE_MAC
    // These custom-chrome windows can be destroyed immediately by docking or
    // workspace replacement. AppKit's automatic close animation can outlive
    // the JUCE peer on macOS 26; keep native visibility changes synchronous.
    if (auto* peer = getPeer())
        [(NSView*) peer->getNativeHandle() window].animationBehavior = NSWindowAnimationBehaviorNone;
#endif
    setVisible(true); ready = true;
}
FloatingPanelWindow::~FloatingPanelWindow() { ready = false; clearContentComponent(); }
void FloatingPanelWindow::restoreToDisplays(const std::vector<fohws::DisplayInfo>& connected) {
    // Suppress move/resize callbacks until the saved display has been resolved. Otherwise a
    // callback can replace its ID midway through recovery with a different display's ID.
    const bool wasReady = ready;
    ready = false;
    auto r = fohws::clampToDisplays(st, connected, WS::floatMinWidth, WS::floatMinHeight);
    // The constrainer must agree with recovery even when the work area is
    // smaller than the normal minimum; otherwise the next resize jumps off-screen.
    for (const auto& display : connected)
        if (display.id == st.displayId && display.area.w > 0 && display.area.h > 0)
            setResizeLimits(jmin(WS::floatMinWidth, display.area.w), jmin(WS::floatMinHeight, display.area.h),
                            display.area.w, display.area.h);
    setBounds(r.x, r.y, r.w, r.h);
    ready = wasReady;
}
void FloatingPanelWindow::parentSizeChanged() {
    // JUCE's Displays::refresh calls this on desktop peers when the monitor arrangement or
    // work area changes. Recover on the message thread before move callbacks replace the ID.
    const bool wasReady = ready;
    ready = false;
    DocumentWindow::parentSizeChanged();
    if (wasReady) restoreToDisplays(currentDisplays());
    ready = wasReady;
    if (!ready) return;
    if (onChanged) onChanged();
}
void FloatingPanelWindow::setPinned(bool on) { st.alwaysOnTop = on; pin->setToggleState(on, dontSendNotification); setAlwaysOnTop(on); }
void FloatingPanelWindow::restyle() { setBackgroundColour(TH().surfaces.background); repaint(); }
void FloatingPanelWindow::moved() { DocumentWindow::moved(); if (!ready) return; auto b = getBounds(); st.bounds = { b.getX(), b.getY(), b.getWidth(), b.getHeight() }; st.displayId = displayIdFor(b); if (onChanged) onChanged(); }
void FloatingPanelWindow::resized() {
    DocumentWindow::resized();
    if (pin && dock) { auto tb = getTitleBarArea(); int s = WS::headerButtonSize, y = tb.getCentreY() - s / 2;
        int closeW = tb.getHeight(); int x = tb.getRight() - closeW - WS::headerButtonGap - s;
        dock->setBounds(x, y, s, s); pin->setBounds(x - WS::headerButtonGap - s, y, s, s); }
    if (ready) { auto b = getBounds(); st.bounds = { b.getX(), b.getY(), b.getWidth(), b.getHeight() }; st.displayId = displayIdFor(b); if (onChanged) onChanged(); }
}
void FloatingPanelWindow::mouseDoubleClick(const MouseEvent& e) { if (e.y < getTitleBarHeight() && onDock) onDock(); }
