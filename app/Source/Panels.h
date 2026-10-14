// Panels: every view is a self-contained Panel (id, title, saveState/loadState) that reads from the
// shared AppModel and is created through the PanelRegistry. Chrome (header, tabs, splitters, floating
// windows) belongs to the WorkspaceView, not to the panels.
#pragma once
#include "AppModel.h"
#include "LayoutModel.h"

class Panel : public juce::Component, public AppModel::Listener {
public:
    Panel(AppModel& m, std::string id, juce::String title) : model(m), pid(std::move(id)), ptitle(std::move(title)) { model.addListener(this); }
    ~Panel() override { model.removeListener(this); }
    const std::string& panelId() const { return pid; }
    const juce::String& title() const { return ptitle; }
    virtual fohws::Json saveState() const { return {}; }
    virtual void loadState(const fohws::Json&) {}
    virtual fohws::Size minSize() const { return { foh::theme::ws::panelMinWidth, foh::theme::ws::panelMinHeight }; }
    virtual int priority() const { return 10; }            // collapse order: lower folds into a tab first
    virtual bool isPlaceholder() const { return false; }
    void modelChanged(int) override {}
    juce::String helpText;                                   // shown by the Inspector when this panel has focus
    std::function<void(Panel&)> onFocus;                     // set by the host
    void mouseDown(const juce::MouseEvent&) override { if (onFocus) onFocus(*this); }
protected:
    AppModel& model;
    static constexpr int pad = 10;
private:
    std::string pid; juce::String ptitle;
};

class PanelRegistry {
public:
    struct Entry { std::string id; juce::String title; std::function<std::unique_ptr<Panel>(AppModel&)> make; };
    void add(Entry e) { entries.push_back(std::move(e)); }
    const Entry* find(const std::string& id) const { for (auto& e : entries) if (e.id == id) return &e; return nullptr; }
    std::unique_ptr<Panel> create(const std::string& id, AppModel& m) const { if (auto* e = find(id)) return e->make(m); return nullptr; }
    const std::vector<Entry>& all() const { return entries; }
    static PanelRegistry& standard(); // all build #1 panels
private:
    std::vector<Entry> entries;
};
