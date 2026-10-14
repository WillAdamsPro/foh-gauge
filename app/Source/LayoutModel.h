// LayoutModel: pure C++17 (no JUCE) workspace data model, so it is unit-testable with the core tests.
// A workspace = a tree of split / tab / leaf nodes (+ proportions) and a list of floating panels
// (bounds + display id), plus opaque per-panel state. Serialised as JSON.
// Kept ready for drag-to-dock and user-saved layouts: nodes are addressable, removable and insertable.
#pragma once
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace fohws {

// ---- minimal JSON value (objects keep insertion order) ----
class Json {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    Json() = default;
    Json(bool v) : t(Bool), b(v) {}
    Json(int v) : t(Number), n(v) {}
    Json(double v) : t(Number), n(v) {}
    Json(const char* v) : t(String), s(v) {}
    Json(std::string v) : t(String), s(std::move(v)) {}
    static Json array() { Json j; j.t = Array; return j; }
    static Json object() { Json j; j.t = Object; return j; }
    Type type() const { return t; }
    bool isNull() const { return t == Null; } bool isObject() const { return t == Object; } bool isArray() const { return t == Array; }
    bool asBool(bool d = false) const { return t == Bool ? b : d; }
    double asNumber(double d = 0) const { return t == Number ? n : d; }
    int asInt(int d = 0) const { return t == Number ? (int) n : d; }
    std::string asString(const std::string& d = {}) const { return t == String ? s : d; }
    const std::vector<Json>& items() const { return a; }
    std::vector<Json>& items() { return a; }
    const std::vector<std::pair<std::string, Json>>& members() const { return o; }
    void push(Json v) { if (t != Array) { *this = array(); } a.push_back(std::move(v)); }
    Json& set(const std::string& k, Json v);
    const Json& operator[](const std::string& k) const;
    bool has(const std::string& k) const;
    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text, std::string* error = nullptr);
    bool operator==(const Json& other) const;
private:
    void dumpTo(std::string& out, int indent, int depth) const;
    Type t = Null; bool b = false; double n = 0; std::string s; std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
};

// ---- layout tree ----
struct LayoutNode {
    enum Kind { Split, Tabs, Leaf };
    Kind kind = Leaf;
    bool horizontal = true;          // Split: true = side by side (H), false = stacked (V)
    std::vector<double> sizes;       // Split: proportions of the parent, sum 1
    std::vector<LayoutNode> children;// Split: any nodes; Tabs: leaves
    std::string panel;               // Leaf: panel id
    int active = 0;                  // Tabs: active index
    std::vector<double> presetSizes; // Split: proportions to restore on splitter double-click

    static LayoutNode leaf(std::string id) { LayoutNode n; n.kind = Leaf; n.panel = std::move(id); return n; }
    static LayoutNode tabs(std::vector<std::string> ids, int active = 0);
    static LayoutNode split(bool horizontal, std::vector<std::pair<double, LayoutNode>> parts);
    bool operator==(const LayoutNode& o) const;
    void collectPanels(std::vector<std::string>& out) const;
    bool contains(const std::string& id) const;
};
inline LayoutNode H(std::vector<std::pair<double, LayoutNode>> p) { return LayoutNode::split(true, std::move(p)); }
inline LayoutNode V(std::vector<std::pair<double, LayoutNode>> p) { return LayoutNode::split(false, std::move(p)); }
inline LayoutNode L(std::string id) { return LayoutNode::leaf(std::move(id)); }

// remove a panel's leaf from the tree (renormalises sizes, collapses single-child splits / tab groups).
// Returns the id of a panel that was next to it (for "return to dock"), or "" if not found.
std::string removePanel(LayoutNode& root, const std::string& id);
// add `id` as a tab in the slot that holds `neighbour` (or the first leaf when not found)
void addAsTab(LayoutNode& root, const std::string& id, const std::string& neighbour);
// normalise sizes, drop empty nodes, collapse singletons
void normalise(LayoutNode& n);

// Collapse rule (spec §7): when a split can't give each child its min size, the lowest-priority
// leaf child becomes a tab in its sibling. minSize(panel) -> {w,h}; priority(panel): lower = drops first.
struct Size { int w = 0, h = 0; };
void enforceMinSizes(LayoutNode& n, int width, int height, const std::function<Size(const std::string&)>& minSize,
                     const std::function<int(const std::string&)>& priority);
Size minSizeOf(const LayoutNode& n, const std::function<Size(const std::string&)>& minSize, int headerH = 28);

// ---- floating windows + displays ----
struct Rect { int x = 0, y = 0, w = 0, h = 0; bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; } };
struct DisplayInfo { std::string id; Rect area; bool isMain = false; };
struct FloatingPanel { std::string panel; Rect bounds; std::string displayId; bool alwaysOnTop = false; std::string returnNeighbour; };
// Restore a floating window onto its display. If that display is missing, move it onto the main
// display (keeping its offset relative to the old display's origin where possible). Then clamp so the
// whole window (at least min size) lies inside the display's work area. Updates fp.displayId.
Rect clampToDisplays(FloatingPanel& fp, const std::vector<DisplayInfo>& displays, int minW = 320, int minH = 200);

// ---- workspace ----
struct Workspace {
    std::string id, name; bool builtIn = false; int version = 1;
    LayoutNode root; std::vector<FloatingPanel> floating;
    Json panelState = Json::object();  // panel id -> opaque state from Panel::saveState()
};
Json toJson(const LayoutNode& n);
bool fromJson(const Json& j, LayoutNode& n, std::string& err);
Json toJson(const Workspace& w);
bool fromJson(const Json& j, Workspace& w, std::string& err);

// built-in presets (spec §7): "setup", "alignment", "tuning", "rta-show"
std::vector<Workspace> builtInPresets();

} // namespace fohws
