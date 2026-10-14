// WorkspaceView: hosts a fohws::LayoutNode tree (split / tabs / leaf) of Panels with the workspace
// chrome from Iris's ws-component-spec (§1 header 28 px, §2 tabs 26 px, §3 splitters 8 px hit / 1 px line).
// The tree in the Workspace is the source of truth; the view builds components from it, applies the
// collapse rule (§7) to a display copy, and writes splitter drags / tab changes back to the tree.
#pragma once
#include "Panels.h"

class WorkspaceView : public juce::Component {
public:
    WorkspaceView(AppModel& m, const PanelRegistry& r);
    ~WorkspaceView() override;

    void setWorkspace(fohws::Workspace* w);           // not owned. Loads panel state, rebuilds.
    fohws::Workspace* workspace() const { return ws; }
    void setShowMode(bool enabled);
    void rebuild();                                    // after any change to ws->root
    void storePanelStates();                           // Panel::saveState() -> ws->panelState
    Panel* panel(const std::string& id, bool create = true);
    void releasePanel(const std::string& id, Panel* p);// re-parent target for floating windows

    void focus(const std::string& id);
    const std::string& focusedPanel() const { return focused; }
    void closePanel(const std::string& id);
    void activateTab(const std::vector<int>& path, int index);
    void toggleMaximise(const std::string& id) { maximised = maximised == id ? std::string() : id; rebuild(); }

    std::function<void()> onLayoutEdited;                       // splitter / tab / close changes (modified dot)
    std::function<void(const std::string&)> onFloatRequested;   // header tear-off button
    std::function<bool(const std::string&)> isFloating;         // panel currently lives in a floating window

    void resized() override;
    void paint(juce::Graphics&) override;

    struct Node; struct Slot; struct Splitter; // implementation components
private:
    friend struct Node; friend struct Slot; friend struct Splitter;
    fohws::LayoutNode* nodeAt(fohws::LayoutNode& root, const std::vector<int>& path);
    void layoutDisplay();
    AppModel& model; const PanelRegistry& registry; fohws::Workspace* ws = nullptr;
    std::map<std::string, std::unique_ptr<Panel>> panels;
    fohws::LayoutNode display; bool showMode = false; bool collapsed = false; std::string focused, maximised;
    std::unique_ptr<Node> rootComp;
};
