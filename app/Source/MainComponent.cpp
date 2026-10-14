#include "MainComponent.h"
using namespace juce;
namespace C = foh::ui::col;
namespace D = foh::ui::dim;
namespace T = foh::theme::type;

MainComponent::MainComponent() {
    if (auto* pf = model.settings.getUserSettings()) foh::theme::ThemeManager::get().setTheme(pf->getValue("appearance") == "sunlight" ? "apple-pro-sunlight" : "apple-pro-dark");
    if (auto* t = std::getenv("FOH_THEME")) foh::theme::ThemeManager::get().setTheme(t); // dev aid
    setLookAndFeel(&lnf); lnf.applyTheme();
    foh::theme::ThemeManager::get().addChangeListener(this); model.addListener(this);

    addAndMakeVisible(toolbarTitle); toolbarTitle.setFont(FohLookAndFeel::font(T::small + 1, true));
    addAndMakeVisible(switcher); switcher.onChange = [this](int i) { selectWorkspace(i); };
    addAndMakeVisible(status);
    addAndMakeVisible(pauseBtn); pauseBtn.setClickingTogglesState(true); makePrimary(pauseBtn); pauseBtn.setButtonText(u8("\xe2\x96\xa0 Stop"));
    pauseBtn.onClick = [this] { model.setPaused(pauseBtn.getToggleState()); pauseBtn.setButtonText(model.paused ? u8("\xe2\x96\xb6 Start") : u8("\xe2\x96\xa0 Stop")); };

    const char* names[] = {"Setup", "Alignment", "Tuning", "RTA / Show"}; NavButton::Glyph glyphs[] = {NavButton::Home, NavButton::Guides, NavButton::Transfer, NavButton::Rta};
    for (int i = 0; i < 4; ++i) { auto* b = nav.add(new NavButton(names[i], glyphs[i])); b->onClick = [this, i] { selectWorkspace(i); }; addAndMakeVisible(b); }
    appearance.setSelected(foh::theme::ThemeManager::get().current().isLight ? 1 : 0);
    appearance.onChange = [this](int i) { foh::theme::ThemeManager::get().setTheme(i == 1 ? "apple-pro-sunlight" : "apple-pro-dark");
        if (auto* pf = model.settings.getUserSettings()) { pf->setValue("appearance", i == 1 ? "sunlight" : "dark"); pf->saveIfNeeded(); } };
    addAndMakeVisible(appearance);
    for (int pc : {100, 125, 150, 200}) scaleBox.addItem("UI scale " + String(pc) + "%", pc);
    { int saved = model.settings.getUserSettings() ? model.settings.getUserSettings()->getIntValue("uiScale", 100) : 100; if (scaleBox.indexOfItemId(saved) < 0) saved = 100;
      scaleBox.setSelectedId(saved, dontSendNotification); if (saved != 100) Desktop::getInstance().setGlobalScaleFactor((float) saved / 100.0f); }
    scaleBox.onChange = [this] { int pc = scaleBox.getSelectedId(); Desktop::getInstance().setGlobalScaleFactor((float) pc / 100.0f);
        if (auto* pf = model.settings.getUserSettings()) { pf->setValue("uiScale", pc); pf->saveIfNeeded(); } };
    addAndMakeVisible(scaleBox);

    addAndMakeVisible(view);
    model.requestWorkspace = [this](const std::string& id) { selectWorkspace(id); };
    workspaces = fohws::builtInPresets();
    setSize(std::getenv("FOH_WINDOW_W") ? std::atoi(std::getenv("FOH_WINDOW_W")) : foh::theme::frame::baseWidth, foh::theme::frame::baseHeight); /* FOH_WINDOW_W: dev aid */
    int start = std::getenv("FOH_WORKSPACE") ? jlimit(0, 3, std::atoi(std::getenv("FOH_WORKSPACE"))) : 1; // dev/screenshot aid, default Alignment
    selectWorkspace(start);
    startTimerHz(15);
}
MainComponent::~MainComponent() { stopTimer(); view.setWorkspace(nullptr); model.removeListener(this); foh::theme::ThemeManager::get().removeChangeListener(this); setLookAndFeel(nullptr); }

void MainComponent::selectWorkspace(const std::string& id) { for (size_t i = 0; i < workspaces.size(); ++i) if (workspaces[i].id == id) { selectWorkspace((int) i); return; } }
void MainComponent::selectWorkspace(int i) {
    if (i < 0 || i >= (int) workspaces.size()) return;
    current = i; view.setWorkspace(&workspaces[(size_t) i]);
    { std::vector<std::string> ids; workspaces[(size_t) i].root.collectPanels(ids); for (auto& id : ids) if (id != "Inspector") { view.focus(id); break; } } // Inspector explains the first panel
    switcher.setSelected(jmin(i, 3)); for (int k = 0; k < nav.size(); ++k) nav[k]->setToggleState(k == i, dontSendNotification);
    toolbarTitle.setText(String(workspaces[(size_t) i].name), dontSendNotification);
    workspaceSelected(); repaint();
}

void MainComponent::paint(Graphics& g) {
    const int top = D::statusBarHeight(), nw = sidebarVisible() ? D::navWidth() : 0;
    g.fillAll(C::background());
    g.setColour(C::statusBarBackground()); g.fillRect(0, 0, getWidth(), top);
    g.setColour(C::border()); g.fillRect(0.f, top - D::hairline(), (float) getWidth(), D::hairline());
    if (nw) { g.setColour(C::navBackground()); g.fillRect(0, top, nw, getHeight() - top); g.setColour(C::border()); g.fillRect((float) nw - D::hairline(), (float) top, D::hairline(), (float) getHeight()); }
    g.setFont(FohLookAndFeel::font(T::logo, true)); g.setColour(C::textPrimary()); g.drawText("FOH", 16, 0, 40, top, Justification::centredLeft);
    g.setColour(C::accent()); g.drawText("Gauge", 52, 0, 80, top, Justification::centredLeft);
    if (nw) { g.setColour(C::textAxis()); g.setFont(FohLookAndFeel::font(T::axisTick, true)); g.drawText("WORKSPACES", 16, top + 14, nw - 32, 16, Justification::centredLeft); }
}
void MainComponent::resized() {
    const int top = D::statusBarHeight(), nw = sidebarVisible() ? D::navWidth() : 0;
    auto bar = getLocalBounds().removeFromTop(top).reduced(12, 0); bar.removeFromLeft(130);
    pauseBtn.setBounds(bar.removeFromRight(84).withSizeKeepingCentre(84, 30)); bar.removeFromRight(12);
    toolbarTitle.setBounds(bar.removeFromLeft(110));
    int sw = 360; switcher.setBounds(bar.removeFromLeft(sw).withSizeKeepingCentre(sw, foh::theme::ws::switcherHeight - 6)); bar.removeFromLeft(16);
    status.setBounds(bar);
    for (auto* b : nav) b->setVisible(nw > 0); appearance.setVisible(nw > 0); scaleBox.setVisible(nw > 0);
    if (nw) { auto s = Rectangle<int>(0, top, nw, getHeight() - top).reduced(10, 0); s.removeFromTop(36);
        for (auto* b : nav) { b->setBounds(s.removeFromTop(40)); s.removeFromTop(4); }
        auto bot = s.removeFromBottom(80); appearance.setBounds(bot.removeFromTop(30).reduced(0, 2)); bot.removeFromTop(8); scaleBox.setBounds(bot.removeFromTop(28)); }
    view.setBounds(getLocalBounds().withTrimmedTop(top).withTrimmedLeft(nw));
}

void MainComponent::changeListenerCallback(ChangeBroadcaster*) {
    appearance.setSelected(foh::theme::ThemeManager::get().current().isLight ? 1 : 0); lnf.applyTheme(); makePrimary(pauseBtn);
    toolbarTitle.setColour(Label::textColourId, C::textPrimary());
    sendLookAndFeelChange(); model.notify(AppModel::Theme | AppModel::Snapshot | AppModel::Delay | AppModel::Traces);
    resized(); repaint(); view.repaint();
    if (auto* w = getTopLevelComponent()) if (auto* dw = dynamic_cast<DocumentWindow*>(w)) dw->setBackgroundColour(C::background());
}
void MainComponent::modelChanged(int) {}

void MainComponent::timerCallback() {
    toolbarTitle.setColour(Label::textColourId, C::textPrimary());
    float mp = model.micPeak, rp = model.refPeak; double fs = model.sampleRate;
    auto dbs = [](float p) { return p > 1e-6f ? String(20 * std::log10(p), 1) + " dBFS" : String("-inf"); };
    auto* dev = model.dm.getCurrentAudioDevice();
    using P = StatusPills; bool clip = mp > 0.99f || rp > 0.99f;
    auto lvl = [&](const char* n, float p) { return P::Pill{ String(n) + " " + dbs(p), p > 0.99f ? P::Poor : p > 0.01f ? P::Good : P::Caution }; };
    std::vector<P::Pill> v { { model.paused ? "Paused" : "Measuring", model.paused ? P::Caution : P::Good },
                             { dev ? dev->getName() + u8(" \xc2\xb7 ") + String((int) fs / 1000) + " kHz" : String("No device"), dev ? P::None : P::Poor },
                             lvl("Mic", mp), lvl("Ref", rp), { model.calApplied ? "Cal on" : "Cal off", model.calApplied ? P::Good : P::None }, { "Ref delay " + String(model.refDelayMs, 2) + " ms", P::None } };
    if (clip) v.push_back({ "CLIPPING", P::Poor });
    status.setPills(std::move(v));
}
