#include "WorkspaceView.h"
using namespace juce;
namespace WS = foh::theme::ws;
using fohws::LayoutNode;
static const foh::theme::Theme& TH() { return foh::theme::ThemeManager::get().current(); }

// ---- glyphs (drawn, no SF Symbols): float = two squares, close = x, overflow = >>
static void drawFloatGlyph(Graphics& g, Rectangle<float> r, Colour c) { g.setColour(c); auto a = r.withSizeKeepingCentre(9, 9).translated(1.5f, -1.5f), b = a.translated(-3, 3);
    g.drawRect(a, 1.f); g.setColour(c); g.fillRect(b.withWidth(1)); g.fillRect(b.withHeight(1)); g.fillRect(b.withTrimmedTop(b.getHeight() - 1)); g.fillRect(b.withTrimmedLeft(b.getWidth() - 1)); }
static void drawCloseGlyph(Graphics& g, Rectangle<float> r, Colour c, float s = 8) { auto b = r.withSizeKeepingCentre(s, s); g.setColour(c);
    g.drawLine(b.getX(), b.getY(), b.getRight(), b.getBottom(), 1.3f); g.drawLine(b.getRight(), b.getY(), b.getX(), b.getBottom(), 1.3f); }
static void drawButtonState(Graphics& g, Rectangle<float> r, bool hover, bool down) {
    if (down) { g.setColour(WS::pressedOverlay(TH())); g.fillRoundedRectangle(r, 4.f); } else if (hover) { g.setColour(WS::hoverOverlay(TH())); g.fillRoundedRectangle(r, 4.f); } }

struct WorkspaceView::Node : Component { std::vector<int> path; virtual ~Node() = default; };

// ===================== Slot: a leaf or a tab group, with its header / tab strip (§1, §2) =====================
struct WorkspaceView::Slot : WorkspaceView::Node {
    WorkspaceView& v; std::vector<std::string> ids; int active = 0; Panel* shown = nullptr;
    enum Hit { None, Float, Close, Overflow, Tab, TabClose, Title };
    struct HitInfo { Hit what = None; int tab = -1; };
    HitInfo hover, down; std::vector<Rectangle<int>> tabRects; int firstHidden = -1;
    Slot(WorkspaceView& view, const LayoutNode& n) : v(view) {
        if (n.kind == LayoutNode::Leaf) ids = { n.panel }; else { for (auto& c : n.children) if (c.kind == LayoutNode::Leaf) ids.push_back(c.panel); active = jlimit(0, jmax(0, (int) ids.size() - 1), n.active); }
        if (!ids.empty()) if (auto* p = v.panel(ids[(size_t) active])) { shown = p; addAndMakeVisible(p); }
    }
    bool tabbed() const { return ids.size() > 1; }
    int headerH() const { if (v.showMode) return 0; return tabbed() ? WS::tabHeight : WS::panelHeaderHeight; }
    Rectangle<int> header() const { return getLocalBounds().removeFromTop(headerH()); }
    Rectangle<int> btn(int i) const { auto h = header(); int s = WS::headerButtonSize; return { h.getRight() - WS::headerPaddingX - (i + 1) * s - i * WS::headerButtonGap, h.getCentreY() - s / 2, s, s }; } // 0 = close, 1 = float
    Rectangle<int> overflowBtn() const { auto b = btn(1); return { b.getX() - WS::headerButtonGap - WS::overflowButtonWidth, b.getY(), WS::overflowButtonWidth, b.getHeight() }; }
    void resized() override {
        if (shown) shown->setBounds(getLocalBounds().withTrimmedTop(headerH()));
        tabRects.clear(); firstHidden = -1; if (!tabbed()) return;
        int avail = btn(1).getX() - 4, x = 0; auto f = FohLookAndFeel::font(WS::panelTitleSize, false, true);
        std::vector<int> w; for (auto& id : ids) { auto* p = v.panel(id); w.push_back(jlimit(WS::tabMinWidth, WS::tabMaxWidth, roundToInt(f.getStringWidthFloat(p ? p->title() : String(id))) + 2 * WS::tabPaddingX + WS::tabCloseSize)); }
        int total = 0; for (int x2 : w) total += x2;
        if (total > avail) { for (auto& x2 : w) x2 = jmax(WS::tabMinWidth, x2 * avail / jmax(1, total)); total = 0; for (int x2 : w) total += x2; }
        if (total > avail) avail -= WS::overflowButtonWidth + WS::headerButtonGap;
        for (size_t i = 0; i < ids.size(); ++i) { if (x + w[i] > avail && firstHidden < 0) firstHidden = (int) i; tabRects.push_back(firstHidden >= 0 ? Rectangle<int>() : Rectangle<int>(x, 0, w[i], WS::tabHeight)); if (firstHidden < 0) x += w[i]; }
    }
    HitInfo hit(Point<int> p) const {
        if (!header().contains(p)) return {};
        if (btn(0).contains(p)) return { Close }; if (btn(1).contains(p)) return { Float };
        if (firstHidden >= 0 && overflowBtn().contains(p)) return { Overflow };
        for (int i = 0; i < (int) tabRects.size(); ++i) if (tabRects[(size_t) i].contains(p)) {
            auto cr = tabRects[(size_t) i].withTrimmedLeft(tabRects[(size_t) i].getWidth() - WS::tabPaddingX + 4 - WS::tabCloseSize).withWidth(WS::tabCloseSize).withSizeKeepingCentre(WS::tabCloseSize, WS::tabCloseSize);
            return { cr.contains(p) ? TabClose : Tab, i }; }
        return { Title };
    }
    std::string activeId() const { return ids.empty() ? std::string() : ids[(size_t) active]; }
    void paint(Graphics& g) override {
        if (v.showMode) return;
        auto& t = TH(); auto h = header(); bool focusedSlot = std::find(ids.begin(), ids.end(), v.focused) != ids.end();
        g.setColour(WS::panelHeaderBg(t)); g.fillRect(h);
        g.setColour(t.surfaces.border); g.fillRect((float) h.getX(), h.getBottom() - t.borderWidth, (float) h.getWidth(), t.borderWidth);
        if (!tabbed()) {
            auto* p = v.panel(activeId(), false); auto tr = h.withTrimmedLeft(WS::headerPaddingX).withRight(btn(1).getX() - 4);
            g.setColour(focusedSlot ? t.text.primary : t.text.secondary); g.setFont(FohLookAndFeel::font(WS::panelTitleSize, false, true));
            g.drawText(p ? p->title() : String(activeId()), tr, Justification::centredLeft, true);
        } else {
            g.setFont(FohLookAndFeel::font(WS::panelTitleSize, false, true));
            for (int i = 0; i < (int) tabRects.size(); ++i) { auto r = tabRects[(size_t) i]; if (r.isEmpty()) continue; bool act = i == active, hov = hover.tab == i;
                g.setColour(act ? WS::tabActiveBg(t) : WS::tabInactiveBg(t)); g.fillRect(r);
                if (hov && !act) { g.setColour(down.tab == i ? WS::pressedOverlay(t) : WS::hoverOverlay(t)); g.fillRect(r); }
                if (act) { g.setColour(t.accent); g.fillRect(r.getX(), r.getBottom() - WS::tabIndicatorHeight, r.getWidth(), WS::tabIndicatorHeight); }
                else { g.setColour(t.surfaces.border); g.fillRect(r.getRight() - 1, r.getY() + 5, 1, r.getHeight() - 10); }
                auto* p = v.panel(ids[(size_t) i], false);
                g.setColour(act || hov ? t.text.primary : t.text.secondary);
                g.drawText(p ? p->title() : String(ids[(size_t) i]), r.withTrimmedLeft(WS::tabPaddingX).withTrimmedRight(WS::tabPaddingX / 2 + WS::tabCloseSize), Justification::centredLeft, true);
                if (act || hov) { auto cr = Rectangle<float>((float) r.getRight() - WS::tabPaddingX + 4 - WS::tabCloseSize, (float) r.getCentreY() - WS::tabCloseSize / 2.f, (float) WS::tabCloseSize, (float) WS::tabCloseSize);
                    bool ch = hover.what == TabClose && hover.tab == i; drawButtonState(g, cr, ch, ch && down.what == TabClose);
                    drawCloseGlyph(g, cr, ch ? WS::closeHover(t) : t.text.axis, 6); } }
            if (firstHidden >= 0) { auto o = overflowBtn().toFloat(); drawButtonState(g, o, hover.what == Overflow, down.what == Overflow);
                g.setColour(hover.what == Overflow ? t.text.primary : t.text.axis); g.setFont(FohLookAndFeel::font(foh::theme::type::body)); g.drawText(u8("\xc2\xbb"), o, Justification::centred); }
        }
        if (focusedSlot) { g.setColour(t.accent); g.fillRect(h.getX(), h.getY(), 2, h.getHeight()); } // focus indicator (title also turns primary)
        bool canFloat = (bool) v.onFloatRequested;
        auto fb = btn(1).toFloat(), cb = btn(0).toFloat();
        if (canFloat) { drawButtonState(g, fb, hover.what == Float, down.what == Float); drawFloatGlyph(g, fb, (hover.what == Float ? t.text.primary : t.text.axis)); }
        drawButtonState(g, cb, hover.what == Close, down.what == Close); drawCloseGlyph(g, cb, hover.what == Close ? WS::closeHover(t) : t.text.axis);
    }
    void mouseMove(const MouseEvent& e) override { auto h = hit(e.getPosition()); if (h.what != hover.what || h.tab != hover.tab) { hover = h; repaint(header()); }
        setTooltipFor(h); }
    void setTooltipFor(const HitInfo&) {}
    void mouseExit(const MouseEvent&) override { hover = {}; repaint(header()); }
    void mouseDown(const MouseEvent& e) override { down = hit(e.getPosition()); v.focus(activeId()); repaint(header()); }
    void mouseUp(const MouseEvent& e) override {
        auto up = hit(e.getPosition()); auto d = down; down = {}; repaint(header());
        if (up.what != d.what || up.tab != d.tab) return;
        switch (up.what) {
            case Close: v.closePanel(activeId()); return;
            case Float: if (v.onFloatRequested) v.onFloatRequested(activeId()); return;
            case Tab: v.activateTab(path, up.tab); return;
            case TabClose: v.closePanel(ids[(size_t) up.tab]); return;
            case Overflow: { PopupMenu m; for (int i = firstHidden; i < (int) ids.size(); ++i) { auto* p = v.panel(ids[(size_t) i], false); m.addItem(i + 1, p ? p->title() : String(ids[(size_t) i]), true, i == active); }
                SafePointer<Slot> s(this); m.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(overflowBtn())), [s](int r) { if (s && r > 0) s->v.activateTab(s->path, r - 1); }); return; }
            default: return;
        }
    }
    void mouseDoubleClick(const MouseEvent& e) override { auto h = hit(e.getPosition()); if (h.what == Title || h.what == Tab) v.toggleMaximise(activeId()); }
    void mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& w) override { if (tabbed() && header().contains(e.getPosition()) && w.deltaY != 0) v.activateTab(path, (active + (w.deltaY < 0 ? 1 : -1) + (int) ids.size()) % (int) ids.size()); }
};

// ===================== Splitter (§3) =====================
struct WorkspaceView::Splitter : Component {
    WorkspaceView& v; std::vector<int> path; size_t index; bool horizontal; bool hot = false, dragging = false; double startA = 0, startB = 0; int extent = 1;
    Splitter(WorkspaceView& view, std::vector<int> p, size_t i, bool h) : v(view), path(std::move(p)), index(i), horizontal(h) { setMouseCursor(h ? MouseCursor::LeftRightResizeCursor : MouseCursor::UpDownResizeCursor); setRepaintsOnMouseActivity(true); }
    void paint(Graphics& g) override {
        auto& t = TH(); bool act = dragging || isMouseOver(); float w = act ? WS::splitterLineActive : WS::splitterLine;
        g.setColour(act ? WS::splitterActive(t) : WS::splitterIdle(t));
        if (horizontal) g.fillRect((getWidth() - w) / 2.f, 0.f, w, (float) getHeight()); else g.fillRect(0.f, (getHeight() - w) / 2.f, (float) getWidth(), w);
        if (dragging) if (auto* n = v.nodeAt(v.display, path)) { // live size readout pill
            int px = roundToInt(n->sizes[index] * extent); String s = String(roundToInt(n->sizes[index] * 100)) + u8("% \xc2\xb7 ") + String(px) + " px";
            g.setFont(FohLookAndFeel::font(foh::theme::type::caption, false, true)); int tw = roundToInt(g.getCurrentFont().getStringWidthFloat(s)) + 12;
            Rectangle<float> pill = horizontal ? Rectangle<float>((float) getWidth() / 2 - tw / 2.f, 8, (float) tw, 18) : Rectangle<float>(8, (float) getHeight() / 2 - 9, (float) tw, 18);
            g.setColour(t.accent); g.fillRoundedRectangle(pill, 4.f); g.setColour(t.text.onAccent); g.drawText(s, pill, Justification::centred); }
    }
    void mouseDown(const MouseEvent&) override { auto* n = v.nodeAt(v.display, path); if (!n) return; startA = n->sizes[index]; startB = n->sizes[index + 1]; dragging = true;
        extent = jmax(1, horizontal ? getParentComponent()->getWidth() : getParentComponent()->getHeight());
        setSize(horizontal ? 120 : getWidth(), horizontal ? getHeight() : 40); setTopLeftPosition(horizontal ? getX() - 56 : getX(), horizontal ? getY() : getY() - 16); repaint(); } // widen to fit the pill
    void mouseDrag(const MouseEvent& e) override {
        auto* n = v.nodeAt(v.display, path); if (!n) return;
        double d = (horizontal ? e.getDistanceFromDragStartX() : e.getDistanceFromDragStartY()) / (double) extent, sum = startA + startB, minF = 0.04;
        double a = jlimit(minF, sum - minF, startA + d); n->sizes[index] = a; n->sizes[index + 1] = sum - a;
        if (!v.collapsed) if (auto* r = v.nodeAt(v.ws->root, path)) { r->sizes = n->sizes; }
        if (auto* p = dynamic_cast<Node*>(getParentComponent())) p->resized();
        if (v.onLayoutEdited) v.onLayoutEdited();
    }
    void mouseUp(const MouseEvent&) override { dragging = false; if (auto* p = getParentComponent()) p->resized(); }
    void mouseDoubleClick(const MouseEvent&) override { // reset to the preset proportion
        auto* n = v.nodeAt(v.display, path); if (!n || n->presetSizes.size() != n->sizes.size()) return; n->sizes = n->presetSizes;
        if (!v.collapsed) if (auto* r = v.nodeAt(v.ws->root, path)) r->sizes = n->sizes;
        if (auto* p = getParentComponent()) p->resized(); if (v.onLayoutEdited) v.onLayoutEdited(); }
};

// ===================== Split box =====================
struct SplitBox : WorkspaceView::Node {
    WorkspaceView& v; const LayoutNode* node; std::vector<std::unique_ptr<WorkspaceView::Node>> kids; std::vector<std::unique_ptr<WorkspaceView::Splitter>> bars;
    SplitBox(WorkspaceView& view, const LayoutNode& n) : v(view), node(&n) {}
    void resized() override {
        int n = (int) kids.size(); if (!n) return; bool h = node->horizontal; int total = (h ? getWidth() : getHeight()) - (n - 1) * (int) WS::splitterLine; double pos = 0;
        for (int i = 0; i < n; ++i) { int a = roundToInt(pos), b = i == n - 1 ? total : roundToInt(pos + node->sizes[(size_t) i] * total); pos += node->sizes[(size_t) i] * total;
            int off = i * (int) WS::splitterLine; auto r = h ? Rectangle<int>(a + off, 0, b - a, getHeight()) : Rectangle<int>(0, a + off, getWidth(), b - a); kids[(size_t) i]->setBounds(r);
            if (i < n - 1) { auto* s = bars[(size_t) i].get(); if (s->dragging) { s->setBounds(h ? Rectangle<int>(r.getRight() - 60 + 1, 0, 120, getHeight()) : Rectangle<int>(0, r.getBottom() - 20 + 1, getWidth(), 40)); }
                else s->setBounds(h ? Rectangle<int>(r.getRight() - WS::splitterHitArea / 2 + 1, 0, WS::splitterHitArea, getHeight()) : Rectangle<int>(0, r.getBottom() - WS::splitterHitArea / 2 + 1, getWidth(), WS::splitterHitArea)); s->toFront(false); } }
    }
};

static std::unique_ptr<WorkspaceView::Node> buildNode(WorkspaceView& v, const LayoutNode& n, std::vector<int> path) {
    if (n.kind == LayoutNode::Split) { auto b = std::make_unique<SplitBox>(v, n); b->path = path;
        for (size_t i = 0; i < n.children.size(); ++i) { auto p = path; p.push_back((int) i); b->kids.push_back(buildNode(v, n.children[i], p)); b->addAndMakeVisible(*b->kids.back()); }
        for (size_t i = 0; i + 1 < n.children.size(); ++i) { b->bars.push_back(std::make_unique<WorkspaceView::Splitter>(v, path, i, n.horizontal)); b->addAndMakeVisible(*b->bars.back()); }
        return b; }
    auto s = std::make_unique<WorkspaceView::Slot>(v, n); s->path = path; return s;
}

static bool sameShape(const LayoutNode& a, const LayoutNode& b) { // structure + panels, ignoring sizes / active tab
    if (a.kind != b.kind || a.panel != b.panel || a.horizontal != b.horizontal || a.children.size() != b.children.size()) return false;
    for (size_t i = 0; i < a.children.size(); ++i) if (!sameShape(a.children[i], b.children[i])) return false;
    return true;
}

// ===================== WorkspaceView =====================
WorkspaceView::WorkspaceView(AppModel& m, const PanelRegistry& r) : model(m), registry(r) {}
WorkspaceView::~WorkspaceView() { rootComp.reset(); panels.clear(); }

Panel* WorkspaceView::panel(const std::string& id, bool create) {
    auto it = panels.find(id); if (it != panels.end()) return it->second.get();
    if (!create) return nullptr;
    auto p = registry.create(id, model); if (!p) return nullptr;
    p->onFocus = [this](Panel& x) { focus(x.panelId()); };
    if (ws && ws->panelState.has(id)) p->loadState(ws->panelState[id]);
    auto* raw = p.get(); panels[id] = std::move(p); return raw;
}
void WorkspaceView::releasePanel(const std::string&, Panel* p) { if (p && p->getParentComponent()) p->getParentComponent()->removeChildComponent(p); }

void WorkspaceView::setWorkspace(fohws::Workspace* w) {
    if (ws) storePanelStates();
    ws = w; maximised.clear();
    if (ws) for (auto& [id, p] : panels) if (ws->panelState.has(id)) p->loadState(ws->panelState[id]);
    rebuild();
}
void WorkspaceView::setShowMode(bool enabled) { showMode = enabled; maximised.clear(); rebuild(); }
void WorkspaceView::storePanelStates() { if (!ws) return; for (auto& [id, p] : panels) { auto s = p->saveState(); if (!s.isNull()) ws->panelState.set(id, s); } }

LayoutNode* WorkspaceView::nodeAt(LayoutNode& root, const std::vector<int>& path) {
    LayoutNode* n = &root; for (int i : path) { if (i < 0 || i >= (int) n->children.size()) return nullptr; n = &n->children[(size_t) i]; } return n;
}

void WorkspaceView::rebuild() {
    for (auto& [id, p] : panels) if (!(isFloating && isFloating(id))) releasePanel(id, p.get());
    rootComp.reset(); if (!ws) return;
    layoutDisplay();
    resized(); repaint();
}
// Extend only the display copy: Expert never changes a saved preset or guide state.
LayoutNode WorkspaceView::presentationLayout() {
    if (showMode) return LayoutNode::leaf("ShowMonitoring");
    if (!maximised.empty() && ws->root.contains(maximised)) return LayoutNode::leaf(maximised);
    auto layout = ws->root;
    if (model.expertMode()) {
        for (const auto& entry : registry.all()) {
            if (!layout.contains(entry.id) && !(isFloating && isFloating(entry.id)))
                fohws::addAsTab(layout, entry.id, "TransferFunction");
        }
        // Adding a tab normally selects it. Keep the workspace's current charts
        // selected when Expert adds the extra tabs.
        std::function<void(LayoutNode&, const LayoutNode&)> retainActive = [&](LayoutNode& node, const LayoutNode& saved) {
            if (node.kind == LayoutNode::Tabs) node.active = saved.kind == LayoutNode::Tabs ? saved.active : 0;
            else if (node.kind == LayoutNode::Split)
                for (size_t i = 0; i < node.children.size(); ++i) retainActive(node.children[i], saved.children[i]);
        };
        retainActive(layout, ws->root);
    }
    return layout;
}
void WorkspaceView::layoutDisplay() {
    for (auto& [id, p] : panels) if (!(isFloating && isFloating(id))) releasePanel(id, p.get());
    display = presentationLayout();
    auto ms = [this](const std::string& id) { auto* p = panel(id); return p ? p->minSize() : fohws::Size { WS::panelMinWidth, WS::panelMinHeight }; };
    auto pr = [this](const std::string& id) { auto* p = panel(id); return p ? p->priority() : 10; };
    LayoutNode before = display; if (getWidth() > 0 && getHeight() > 0) fohws::enforceMinSizes(display, getWidth(), getHeight(), ms, pr); collapsed = model.expertMode() || !(before == display); // collapse rule on a display copy
    rootComp = buildNode(*this, display, {}); addAndMakeVisible(*rootComp);
}
void WorkspaceView::resized() {
    if (!ws) return;
    // re-run the collapse rule when the size changes, so a wide window restores the full preset tree
    LayoutNode check = presentationLayout();
    auto ms = [this](const std::string& id) { auto* p = panel(id); return p ? p->minSize() : fohws::Size { WS::panelMinWidth, WS::panelMinHeight }; };
    auto pr = [this](const std::string& id) { auto* p = panel(id); return p ? p->priority() : 10; };
    fohws::enforceMinSizes(check, getWidth(), getHeight(), ms, pr);
    bool structural = !rootComp || !sameShape(check, display);
    if (structural) { rootComp.reset(); for (auto& [id, p] : panels) if (!(isFloating && isFloating(id))) releasePanel(id, p.get());
        display = check; collapsed = model.expertMode() || !(check == ws->root); rootComp = buildNode(*this, display, {}); addAndMakeVisible(*rootComp); }
    rootComp->setBounds(getLocalBounds());
}
void WorkspaceView::paint(Graphics& g) { g.fillAll(TH().surfaces.background); }

void WorkspaceView::focus(const std::string& id) {
    if (focused == id) return; focused = id;
    if (auto* p = panel(id, false)) if (id != "Inspector") model.setInspector(p->title(), p->helpText);
    repaint(); if (rootComp) { std::function<void(Component*)> rp = [&](Component* c) { c->repaint(0, 0, c->getWidth(), WS::panelHeaderHeight); for (auto* k : c->getChildren()) if (dynamic_cast<Node*>(k)) rp(k); }; rp(rootComp.get()); }
}
void WorkspaceView::closePanel(const std::string& id) {
    if (!ws || showMode) return; if (maximised == id) maximised.clear();
    fohws::removePanel(ws->root, id); if (focused == id) focused.clear();
    rebuild(); if (onLayoutEdited) onLayoutEdited();
}
void WorkspaceView::activateTab(const std::vector<int>& path, int index) {
    if (!ws || showMode) return;
    auto* tab = nodeAt(display, path);
    if (!tab || tab->kind != LayoutNode::Tabs || index < 0 || index >= (int) tab->children.size()) return;
    tab->active = index;
    if (!collapsed) if (auto* r = nodeAt(ws->root, path)) r->active = index;
    if (auto* n = nodeAt(display, path)) { if (n->kind == LayoutNode::Tabs && index < (int) n->children.size()) focused = n->children[(size_t) index].panel; }
    rootComp.reset(); for (auto& [id, p] : panels) if (!(isFloating && isFloating(id))) releasePanel(id, p.get());
    rootComp = buildNode(*this, display, {}); addAndMakeVisible(*rootComp); rootComp->setBounds(getLocalBounds());
    if (auto* p = panel(focused, false)) model.setInspector(p->title(), p->helpText);
    if (onLayoutEdited) onLayoutEdited();
}
