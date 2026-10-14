#include "MainComponent.h"
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;

MainComponent::MainComponent(juce::File workspaceFolder) : mgr(std::move(workspaceFolder)) {
    if (auto* pf = model.settings.getUserSettings()) foh::theme::ThemeManager::get().setTheme(pf->getValue("appearance") == "sunlight" ? "apple-pro-sunlight" : "apple-pro-dark");
    if (auto* t = std::getenv("FOH_THEME")) foh::theme::ThemeManager::get().setTheme(t); // dev aid
    setLookAndFeel(&lnf); LookAndFeel::setDefaultLookAndFeel(&lnf); lnf.applyTheme(); // default too, so floating panel windows get the ws chrome
    foh::theme::ThemeManager::get().addChangeListener(this); model.addListener(this);

    addAndMakeVisible(contentViewport);
    contentViewport.setViewedComponent(&contentCanvas, false);
    contentViewport.setScrollOnDragMode(Viewport::ScrollOnDragMode::never);
    contentCanvas.addAndMakeVisible(content);
    addChildComponent(microphoneNotice);
    microphoneNotice.setText("Microphone access is blocked. Enable it in System Settings.", dontSendNotification);
    microphoneNotice.setFont(FohLookAndFeel::font(T::small));
    addChildComponent(microphoneSettingsBtn);
    microphoneSettingsBtn.onClick = [] {
        URL("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone").launchInDefaultBrowser();
    };
    content.addAndMakeVisible(toolbarTitle); toolbarTitle.setFont(FohLookAndFeel::font(T::small + 1, true));
    content.addAndMakeVisible(switcher); switcher.onChange = [this](int i) { selectWorkspace(i); };
    content.addAndMakeVisible(status);
    content.addChildComponent(audioNotice); audioNotice.setFont(FohLookAndFeel::font(T::small));
    content.addAndMakeVisible(expertBtn); expertBtn.setButtonText("Expert"); expertBtn.setClickingTogglesState(true);
    expertBtn.setToggleState(model.expertMode(), dontSendNotification);
    expertBtn.setTooltip("Reveal all charts and settings in every workspace; guides remain available");
    expertBtn.onClick = [this] {
        if (expertBtn.getToggleState() && showMode) setShowMode(false);
        model.setExpertMode(expertBtn.getToggleState());
    };
    content.addAndMakeVisible(showModeBtn); showModeBtn.setButtonText("Show mode"); showModeBtn.setClickingTogglesState(true);
    showModeBtn.setTooltip("Live TF/RTA monitoring against a stored reference; toggle off to restore the workspace layout");
    showModeBtn.onClick = [this] { setShowMode(showModeBtn.getToggleState()); };
    content.addAndMakeVisible(pauseBtn); pauseBtn.setClickingTogglesState(true); makePrimary(pauseBtn); pauseBtn.setButtonText(u8("\xe2\x96\xa0 Stop"));
    pauseBtn.onClick = [this] { model.setPaused(pauseBtn.getToggleState()); pauseBtn.setButtonText(model.paused ? u8("\xe2\x96\xb6 Start") : u8("\xe2\x96\xa0 Stop")); };

    appearance.setSelected(foh::theme::ThemeManager::get().current().isLight ? 1 : 0);
    appearance.onChange = [this](int i) { setSunlightTheme(i == 1); };
    content.addAndMakeVisible(appearance);
    for (int pc : {100, 125, 150, 200}) scaleBox.addItem("UI scale " + String(pc) + "%", pc);
    uiScale = model.settings.getUserSettings() ? model.settings.getUserSettings()->getIntValue("uiScale", 100) : 100;
    if (scaleBox.indexOfItemId(uiScale) < 0) uiScale = 100;
    scaleBox.setSelectedId(uiScale, dontSendNotification);
    scaleBox.onChange = [this] { setUiScale(scaleBox.getSelectedId()); };
    content.addAndMakeVisible(scaleBox);

    content.addAndMakeVisible(view);
    content.addChildComponent(setup);
    setup.onDone = [this] { setup.setVisible(false); view.setVisible(true); openFloatingWindows(); };
    content.addAndMakeVisible(splBtn); splBtn.onClick = [this] { openSetup(); setup.focusSplCalibration(); };
    content.addAndMakeVisible(setupBtn); setupBtn.onClick = [this] { openSetup(); };
    model.requestWorkspace = [this](const std::string& id) { selectWorkspace(id); };
    content.addAndMakeVisible(wsMenuBtn); wsMenuBtn.setButtonText(u8("Workspace \xe2\x96\xbe")); wsMenuBtn.onClick = [this] { showWorkspaceMenu(); };
    view.onLayoutEdited = [this] { updateSwitcher(); };
    view.onFloatRequested = [this](const std::string& id) { floatPanel(id); };
    view.isFloating = [this](const std::string& id) { if (auto* w = view.workspace()) for (auto& f : w->floating) if (f.panel == id) return true; return false; };
    setWantsKeyboardFocus(true);
    addKeyListener(presetCommands.keyMappings());
    setSize(std::getenv("FOH_WINDOW_W") ? std::atoi(std::getenv("FOH_WINDOW_W")) : foh::theme::frame::baseWidth, foh::theme::frame::baseHeight); /* FOH_WINDOW_W: dev aid */
    int start = std::getenv("FOH_WORKSPACE") ? jlimit(0, 3, std::atoi(std::getenv("FOH_WORKSPACE"))) : jmax(0, mgr.indexOf(model.settings.getUserSettings() ? model.settings.getUserSettings()->getValue("workspace", "alignment").toStdString() : "alignment")); // dev/screenshot aid, default Alignment
    selectWorkspace(start);
    if (auto* pf = model.settings.getUserSettings()) if (pf->getBoolValue("showMode", false)) setShowMode(true);
    if (auto* f = std::getenv("FOH_FLOAT")) { std::string id = f; Timer::callAfterDelay(800, [sp = SafePointer<MainComponent>(this), id] { if (sp) sp->floatPanel(id); }); } // dev/screenshot aid
    if (!model.settings.getUserSettings() || !model.settings.getUserSettings()->getBoolValue("setupComplete", false)) openSetup();
    if (!mgr.loadErrors().isEmpty()) AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Workspace load failed", mgr.loadErrors().joinIntoString("\n"));
    startTimerHz(15);
}
MainComponent::~MainComponent() { contentViewport.setViewedComponent(nullptr, false); sessionNotice.close(); removeKeyListener(presetCommands.keyMappings()); stopTimer(); leaveWorkspace(); view.setWorkspace(nullptr); model.removeListener(this); foh::theme::ThemeManager::get().removeChangeListener(this); floats.clear(); LookAndFeel::setDefaultLookAndFeel(nullptr); setLookAndFeel(nullptr); }

void MainComponent::openSetup() {
    if (showMode) setShowMode(false);
    closeFloatingWindows(); view.setVisible(false); setup.setVisible(true); setup.toFront(true); resized();
}
void MainComponent::selectWorkspace(const std::string& id) { int i = mgr.indexOf(id); if (i >= 0) selectWorkspace(i); }
void MainComponent::leaveWorkspace() {
    if (current < 0) return; view.storePanelStates(); closeFloatingWindows(); mgr.saveCurrent(current);
}
void MainComponent::selectWorkspace(int i) {
    if (i < 0 || i >= mgr.count()) return;
    if (i == current) return;
    if (showMode) setShowMode(false);
    leaveWorkspace();
    current = i; view.setWorkspace(&mgr.at(i)); openFloatingWindows();
    { std::vector<std::string> ids; mgr.at(i).root.collectPanels(ids); for (auto& id : ids) if (id != "Inspector") { view.focus(id); break; } } // Inspector explains the first panel
    switcher.setSelected(i < 4 ? i : -1);
    toolbarTitle.setText(String(mgr.at(i).name), dontSendNotification);
    if (auto* pf = model.settings.getUserSettings()) pf->setValue("workspace", String(mgr.at(i).id));
    updateSwitcher(); workspaceSelected(); repaint();
}
// Message-thread presentation only: the saved workspace tree is never replaced.
void MainComponent::setShowMode(bool enabled) {
    if (enabled == showMode) return;
    if (enabled) {
        selectWorkspace("rta-show");
        closeFloatingWindows();
    }
    showMode = enabled;
    view.setShowMode(enabled);
    if (!enabled) openFloatingWindows();
    showModeBtn.setToggleState(enabled, dontSendNotification);
    if (auto* pf = model.settings.getUserSettings()) {
        pf->setValue("showMode", enabled);
        pf->saveIfNeeded();
    }
    view.focus(enabled ? "ShowMonitoring" : "RTA");
    resized(); repaint();
}

void MainComponent::updateSwitcher() { // "•" after a modified layout: a shape, not colour alone (spec §4)
    StringArray l; for (int k = 0; k < 4 && k < mgr.count(); ++k) l.add(String(mgr.at(k).name) + (mgr.isModified(k) ? u8(" \xe2\x80\xa2") : ""));
    switcher.setLabels(l);
    if (current >= 0) toolbarTitle.setText(String(mgr.at(current).name) + (mgr.isModified(current) ? u8(" \xe2\x80\xa2") : ""), dontSendNotification);
}

// ---- floating panels (spec §5)
void MainComponent::openFloatingWindows() {
    auto* w = view.workspace(); if (!w || setup.isVisible() || showMode) return;
    for (auto& f : w->floating) {
        if (std::any_of(floats.begin(), floats.end(), [&](auto* win) { return win->panel.panelId() == f.panel; })) continue;
        auto* p = view.panel(f.panel); if (!p) continue; view.releasePanel(f.panel, p);
        auto* win = floats.add(new FloatingPanelWindow(*p, f)); std::string id = f.panel;
        win->addKeyListener(presetCommands.keyMappings());
        // A desktop window has no component ancestry back to the shell.
        // Forward cycling/scale keys as well as the shared preset mappings.
        win->addKeyListener(this);
        auto window = SafePointer<FloatingPanelWindow>(win);
        // Ignore queued actions if a workspace switch or reset has already destroyed the window.
        win->onDock = [this, id, window] { MessageManager::callAsync([sp = SafePointer<MainComponent>(this), id, window] { if (sp && window) sp->dockPanel(id); }); };
        win->onClose = [this, id, window] { MessageManager::callAsync([sp = SafePointer<MainComponent>(this), id, window] { if (sp && window) { // close = hide; Workspace > Show Panels brings it back
            auto* w2 = sp->view.workspace(); if (!w2) return;
            // Erasing from the state vector invalidates references held by every later window.
            sp->closeFloatingWindows();
            w2->floating.erase(std::remove_if(w2->floating.begin(), w2->floating.end(), [&](auto& x) { return x.panel == id; }), w2->floating.end());
            sp->openFloatingWindows(); sp->updateSwitcher(); } }); };
        win->onChanged = [this] { updateSwitcher(); }; }
}
void MainComponent::closeFloatingWindows() {
    for (auto* window : floats) {
        window->removeKeyListener(this);
        window->removeKeyListener(presetCommands.keyMappings());
    }
    floats.clear();
}
void MainComponent::saveWorkspaceAs(const String& name) {
    view.storePanelStates();
    closeFloatingWindows();
    // saveAs grows the manager's vector, invalidating the view's workspace pointer.
    // Detach while that pointer is still valid, before allocating the new entry.
    view.setWorkspace(nullptr);
    const int index = mgr.saveAs(current, name);
    current = -1;
    selectWorkspace(index);
}
void MainComponent::floatPanel(const std::string& id) {
    if (showMode) return;
    auto* w = view.workspace(); auto* p = view.panel(id, false); if (!w || !p) return;
    if (std::any_of(w->floating.begin(), w->floating.end(), [&](const auto& f) { return f.panel == id; })) return;
    auto screen = p->getScreenBounds().withHeight(jmax(p->getHeight() + foh::theme::ws::floatTitleBarHeight, foh::theme::ws::floatMinHeight)).translated(24, 24);
    fohws::FloatingPanel f; f.panel = id; f.bounds = { screen.getX(), screen.getY(), jmax(screen.getWidth(), foh::theme::ws::floatMinWidth), screen.getHeight() };
    f.displayId = displayIdFor(screen); f.returnNeighbour = fohws::removePanel(w->root, id);
    closeFloatingWindows(); w->floating.push_back(f); view.rebuild(); openFloatingWindows(); updateSwitcher();
}
void MainComponent::dockPanel(const std::string& id) {
    auto* w = view.workspace(); if (!w) return;
    auto it = std::find_if(w->floating.begin(), w->floating.end(), [&](auto& x) { return x.panel == id; }); if (it == w->floating.end()) return;
    std::string nb = it->returnNeighbour; closeFloatingWindows(); w->floating.erase(it);
    fohws::addAsTab(w->root, id, nb); view.rebuild(); openFloatingWindows(); view.focus(id); updateSwitcher();
}

// ---- Workspace menu + shortcuts (spec §4). commandModifier = Cmd on macOS, Ctrl on Windows/Linux.
void MainComponent::showWorkspaceMenu() {
    auto item = [](String t, int id, bool en, bool tick, String sc) { PopupMenu::Item i(t); i.itemID = id; i.isEnabled = en; i.isTicked = tick; i.shortcutKeyDescription = sc; return i; };
    PopupMenu m; auto mod = String(SystemStats::getOperatingSystemType() & SystemStats::MacOSX ? u8("\xe2\x8c\x98") : "Ctrl+");
    for (int k = 0; k < mgr.count(); ++k) m.addItem(item(String(mgr.at(k).name) + (mgr.isModified(k) ? u8(" \xe2\x80\xa2") : ""), 100 + k, true, k == current, k < 4 ? mod + String(k + 1) : String()));
    m.addSeparator();
    m.addItem(8, "Show mode", true, showMode);
    std::vector<std::string> hidden;
    if (!showMode) {
        m.addItem(1, "Save Workspace", current >= 0 && mgr.isModified(current));
        m.addItem(2, u8("Save Workspace As\xe2\x80\xa6"));
        m.addItem(item("Revert to Saved Layout", 3, current >= 0 && mgr.isModified(current), false, u8("\xe2\x8c\xa5") + mod + "0"));
        m.addItem(4, "Reset to Default Layout", current >= 0 && mgr.at(current).builtIn);
        bool custom = current >= 0 && !mgr.at(current).builtIn;
        m.addItem(5, u8("Rename Custom\xe2\x80\xa6"), custom); m.addItem(6, "Delete Custom", custom);
        m.addSeparator();
        m.addItem(item("Show Inspector", 7, true, false, u8("\xe2\x8c\xa5") + mod + "I"));
        PopupMenu show; int n = 0;
        if (auto* w = view.workspace()) for (auto& e : PanelRegistry::standard().all()) { bool fl = false; for (auto& f : w->floating) fl |= f.panel == e.id;
            if (!w->root.contains(e.id) && !fl) { hidden.push_back(e.id); show.addItem(1000 + n++, e.title); } }
        m.addSubMenu("Show Panels", show, !hidden.empty());
    }
    SafePointer<MainComponent> sp(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&wsMenuBtn).withStandardItemHeight(foh::theme::ws::menuItemHeight), [sp, hidden](int r) {
        if (!sp || r == 0) return; auto& s = *sp;
        if (r >= 1000) { auto* w = s.view.workspace(); auto id = hidden[(size_t) (r - 1000)]; fohws::addAsTab(w->root, id, s.view.focusedPanel()); s.view.rebuild(); s.view.focus(id); s.updateSwitcher(); return; }
        if (r >= 100) { s.selectWorkspace(r - 100); return; }
        auto reload = [&s] { int c = s.current; s.closeFloatingWindows(); s.view.setWorkspace(&s.mgr.at(c)); s.openFloatingWindows(); s.updateSwitcher(); };
        switch (r) {
            case 8: s.setShowMode(!s.showMode); break;
            case 1: s.view.storePanelStates(); s.mgr.save(s.current); s.updateSwitcher(); break;
            case 3: s.closeFloatingWindows(); s.mgr.revert(s.current); reload(); break;
            case 4: s.closeFloatingWindows(); s.mgr.resetToDefault(s.current); reload(); break;
            case 2: case 5: {
                auto* aw = new AlertWindow(r == 2 ? "Save Workspace As" : "Rename Workspace", "Name:", MessageBoxIconType::NoIcon);
                aw->addTextEditor("name", r == 2 ? String(s.mgr.at(s.current).name) + " copy" : String(s.mgr.at(s.current).name)); aw->addButton("OK", 1, KeyPress(KeyPress::returnKey)); aw->addButton("Cancel", 0, KeyPress(KeyPress::escapeKey));
                aw->enterModalState(true, ModalCallbackFunction::create([sp, aw, r](int res) { if (!sp || res != 1) return; auto name = aw->getTextEditorContents("name").trim(); if (name.isEmpty()) return;
                    auto& s2 = *sp; if (r == 2) s2.saveWorkspaceAs(name);
                    else { s2.mgr.rename(s2.current, name); s2.updateSwitcher(); } }), true);
                break; }
            case 6: { int c = s.current; s.closeFloatingWindows(); s.view.setWorkspace(nullptr); s.current = -1; s.mgr.remove(c); s.selectWorkspace(0); break; }
            case 7: { auto* w = s.view.workspace(); if (!w->root.contains("Inspector")) { fohws::addAsTab(w->root, "Inspector", s.view.focusedPanel()); s.view.rebuild(); s.updateSwitcher(); } break; }
            default: break;
        }
    });
}
bool MainComponent::keyPressed(const KeyPress& k, Component*) {
    const auto modifiers = k.getModifiers();
    const int code = k.getKeyCode();
    const bool cycle = code == KeyPress::tabKey
        && modifiers.withoutFlags(ModifierKeys::shiftModifier).getRawFlags() == ModifierKeys::ctrlModifier;
    const bool scale = modifiers.isCommandDown() && !modifiers.isAltDown() && !modifiers.isCtrlDown()
        && (code == '0' || code == '=' || code == '+' || code == '-');
    // Panel-local keys must keep their normal floating-window behavior.
    return (cycle || scale) && keyPressed(k);
}
bool MainComponent::keyPressed(const KeyPress& k) {
    auto m = k.getModifiers(); int code = k.getKeyCode();
    if (m.isCommandDown() && !m.isAltDown() && !m.isCtrlDown()) {
        if (code == '0') { setUiScale(100); return true; }
        if (code == '=' || code == '+') { stepUiScale(1); return true; }
        if (code == '-') { stepUiScale(-1); return true; }
    }
    if (showMode) {
        if (code == KeyPress::escapeKey) { setShowMode(false); return true; }
        return false; // Layout shortcuts cannot alter the hidden workspace in Show mode.
    }
    if (code == KeyPress::tabKey
        && m.withoutFlags(ModifierKeys::shiftModifier).getRawFlags() == ModifierKeys::ctrlModifier) {
        const int count = mgr.count();
        if (count > 0) selectWorkspace((current + (m.isShiftDown() ? count - 1 : 1)) % count);
        return true;
    }
    if (m.isCommandDown() && m.isAltDown() && code == '0') { if (current >= 0) { closeFloatingWindows(); mgr.revert(current); view.setWorkspace(&mgr.at(current)); openFloatingWindows(); updateSwitcher(); } return true; }
    if (m.isCommandDown() && m.isAltDown() && (code == 'I' || code == 'i')) { if (auto* w = view.workspace()) if (!w->root.contains("Inspector")) { fohws::addAsTab(w->root, "Inspector", view.focusedPanel()); view.rebuild(); updateSwitcher(); } return true; }
    if (m.isCommandDown() && !m.isAltDown() && (code == 'W' || code == 'w')) { if (!view.focusedPanel().empty()) view.closePanel(view.focusedPanel()); return true; }
    return false;
}

// These settings and transforms run exclusively on the message thread.
void MainComponent::setSunlightTheme(bool enabled) {
    foh::theme::ThemeManager::get().setTheme(enabled ? "apple-pro-sunlight" : "apple-pro-dark");
    if (auto* pf = model.settings.getUserSettings()) { pf->setValue("appearance", enabled ? "sunlight" : "dark"); pf->saveIfNeeded(); }
}
void MainComponent::setUiScale(int percent) {
    if (scaleBox.indexOfItemId(percent) < 0) return;
    uiScale = percent;
    scaleBox.setSelectedId(percent, dontSendNotification);
    resized();
    contentViewport.setViewPosition(0, 0);
    if (auto* pf = model.settings.getUserSettings()) { pf->setValue("uiScale", percent); pf->saveIfNeeded(); }
}
void MainComponent::stepUiScale(int direction) {
    const int index = scaleBox.indexOfItemId(uiScale);
    setUiScale(scaleBox.getItemId(jlimit(0, scaleBox.getNumItems() - 1, index + direction)));
}
void MainComponent::fitSavedScale() {
    // At launch retain the logical size required by the toolbar and workspace.
    // Explicit later choices may zoom further; View and Cmd+0 remain available.
    int fit = 100;
    for (int percent : {100, 125, 150, 200})
        if (percent <= uiScale && getWidth() * 100 >= minimumContentWidth * percent
            && (getHeight() - (microphoneNotice.isVisible() ? 36 : 0)) * 100 >= minimumContentHeight * percent)
            fit = percent;
    setUiScale(fit);
}
void MainComponent::setMicrophoneAccessBlocked(bool blocked) {
    // Called only on the message thread, including before the window becomes visible.
    if (microphoneNotice.isVisible() == blocked) return;
    microphoneNotice.setVisible(blocked);
    microphoneSettingsBtn.setVisible(blocked);
    resized();
}
void MainComponent::resized() {
    const int consentHeight = microphoneNotice.isVisible() ? 36 : 0;
    auto consent = getLocalBounds().removeFromTop(consentHeight).reduced(12, 0);
    microphoneSettingsBtn.setBounds(consent.removeFromRight(156).withSizeKeepingCentre(156, 28));
    consent.removeFromRight(8);
    microphoneNotice.setBounds(consent);
    microphoneNotice.setColour(Label::textColourId, C::textPrimary());
    microphoneNotice.setColour(Label::backgroundColourId, C::background());
    const float scale = (float) uiScale / 100.0f;
    contentViewport.setBounds(getLocalBounds().withTrimmedTop(consentHeight));
    // Reserve scrollbar space before choosing the logical extent. Starting without
    // bars allows a formerly cramped window to return to a full-size workspace.
    int width = contentViewport.getWidth(), height = contentViewport.getHeight();
    const int bar = contentViewport.getScrollBarThickness();
    bool horizontal = false, vertical = false;
    for (int i = 0; i < 2; ++i) {
        horizontal = minimumContentWidth * scale > width;
        vertical = minimumContentHeight * scale > height;
        width = jmax(0, contentViewport.getWidth() - (vertical ? bar : 0));
        height = jmax(0, contentViewport.getHeight() - (horizontal ? bar : 0));
    }
    content.setTransform(AffineTransform::scale(scale));
    content.setBounds(0, 0, jmax(minimumContentWidth, (int) std::floor(width / scale)),
                           jmax(minimumContentHeight, (int) std::floor(height / scale)));
    contentCanvas.setSize((int) std::ceil(content.getWidth() * scale),
                          (int) std::ceil(content.getHeight() * scale));
    layoutContent();
    content.repaint();
}

void MainComponent::paintContent(Graphics& g) {
    const int top = D::statusBarHeight(), nw = sidebarVisible() ? D::navWidth() : 0;
    g.fillAll(C::background());
    g.setColour(C::statusBarBackground()); g.fillRect(0, 0, content.getWidth(), top);
    g.setColour(C::border()); g.fillRect(0.f, top - D::hairline(), (float) content.getWidth(), D::hairline());
    if (nw) { g.setColour(C::navBackground()); g.fillRect(0, top, nw, content.getHeight() - top); g.setColour(C::border()); g.fillRect((float) nw - D::hairline(), (float) top, D::hairline(), (float) content.getHeight()); }
    g.setFont(FohLookAndFeel::font(T::logo, true)); g.setColour(C::textPrimary()); g.drawText("FOH", 16, 0, 40, top, Justification::centredLeft);
    g.setColour(C::accent()); g.drawText("Gauge", 52, 0, 80, top, Justification::centredLeft);
}
void MainComponent::layoutContent() {
    const int top = D::statusBarHeight(), nw = sidebarVisible() ? D::navWidth() : 0;
    auto bar = content.getLocalBounds().removeFromTop(top).reduced(12, 0); bar.removeFromLeft(130);
    splBtn.setBounds(bar.removeFromRight(80).withSizeKeepingCentre(80, 30)); bar.removeFromRight(8);
    setupBtn.setBounds(bar.removeFromRight(106).withSizeKeepingCentre(106, 30)); bar.removeFromRight(8);
    pauseBtn.setBounds(bar.removeFromRight(84).withSizeKeepingCentre(84, 30)); bar.removeFromRight(12);
    showModeBtn.setBounds(bar.removeFromRight(96).withSizeKeepingCentre(96, 30)); bar.removeFromRight(8);
    expertBtn.setBounds(bar.removeFromRight(70).withSizeKeepingCentre(70, 30)); bar.removeFromRight(8);
    const bool wide = content.getWidth() >= 1200;
    toolbarTitle.setVisible(wide); status.setVisible(wide);
    toolbarTitle.setBounds(wide ? bar.removeFromLeft(110) : Rectangle<int>());
    int sw = jlimit(100, 380, bar.getWidth() - 128); switcher.setBounds(bar.removeFromLeft(sw).withSizeKeepingCentre(sw, foh::theme::ws::switcherHeight - 6)); bar.removeFromLeft(8);
    wsMenuBtn.setBounds(bar.removeFromLeft(112).withSizeKeepingCentre(112, foh::theme::ws::switcherHeight - 6)); bar.removeFromLeft(16);
    status.setBounds(bar);
    appearance.setVisible(nw > 0); scaleBox.setVisible(nw > 0);
    if (nw) { auto s = Rectangle<int>(0, top, nw, content.getHeight() - top).reduced(10, 0);
        auto bot = s.removeFromBottom(80); appearance.setBounds(bot.removeFromTop(30).reduced(0, 2)); bot.removeFromTop(8); scaleBox.setBounds(bot.removeFromTop(28)); }
    const int noticeHeight = audioNotice.isVisible() ? 24 : 0;
    audioNotice.setBounds(nw + 12, top, content.getWidth() - nw - 24, noticeHeight);
    view.setBounds(content.getLocalBounds().withTrimmedTop(top + noticeHeight).withTrimmedLeft(nw));
    setup.setBounds(content.getLocalBounds().withTrimmedTop(top + noticeHeight).withTrimmedLeft(nw));
}

void MainComponent::changeListenerCallback(ChangeBroadcaster*) {
    appearance.setSelected(foh::theme::ThemeManager::get().current().isLight ? 1 : 0); lnf.applyTheme(); makePrimary(pauseBtn);
    toolbarTitle.setColour(Label::textColourId, C::textPrimary());
    sendLookAndFeelChange(); model.notify(AppModel::Theme | AppModel::Snapshot | AppModel::Delay | AppModel::Traces);
    resized(); content.repaint(); view.repaint(); for (auto* f : floats) f->restyle();
    if (auto* w = getTopLevelComponent()) if (auto* dw = dynamic_cast<DocumentWindow*>(w)) dw->setBackgroundColour(C::background());
}
void MainComponent::modelChanged(int what) {
    if (what & AppModel::Expert) {
        expertBtn.setToggleState(model.expertMode(), dontSendNotification);
        view.refreshPresentation();
    }
}

void MainComponent::timerCallback() {
    toolbarTitle.setColour(Label::textColourId, C::textPrimary());
    const bool needsNotice = model.engine.deviceFailed.load() || model.engine.state.load() != AudioEngine::Ready
        || model.engine.captureEpoch.load() != model.engine.analysisEpoch.load();
    audioNotice.setText(model.engine.statusText(), dontSendNotification);
    audioNotice.setColour(Label::textColourId, C::textPrimary());
    if (audioNotice.isVisible() != needsNotice) { audioNotice.setVisible(needsNotice); layoutContent(); }
    float mp = model.micPeak, rp = model.refPeak; double fs = model.sampleRate;
    auto dbs = [](float p) { return p > 1e-6f ? String(20 * std::log10(p), 1) + " dBFS" : String("-inf"); };
    auto* dev = model.dm.getCurrentAudioDevice();
    using P = StatusPills; bool clip = mp > 0.99f || rp > 0.99f;
    auto lvl = [&](const char* n, float p) { return P::Pill{ String(n) + " " + dbs(p), p > 0.99f ? P::Poor : p > 0.01f ? P::Good : P::Caution }; };
    std::vector<P::Pill> v { { model.paused ? "Paused" : model.engine.statusText(), model.paused || model.engine.deviceFailed.load() || model.engine.state.load() != AudioEngine::Ready || model.engine.captureEpoch.load() != model.engine.analysisEpoch.load() ? P::Caution : P::Good },
                             { dev ? dev->getName() + u8(" \xc2\xb7 ") + String((int) fs / 1000) + " kHz" : String("No device"), dev ? P::None : P::Poor },
                             P::Pill{ "Mic " + (model.splCal().calibrated ? (model.micLevelReady() ? String(model.micLevel(), 1) + " dB SPL" : String("-- dB SPL")) : dbs(mp)), mp > 0.99f ? P::Poor : P::None }, lvl("Ref", rp), { model.calApplied ? "Cal on" : "Cal off", model.calApplied ? P::Good : P::None }, { "Ref delay " + String(model.refDelayMs, 2) + " ms", P::None } };
    if (clip) v.push_back({ "CLIPPING", P::Poor });
    status.setPills(std::move(v));
}
