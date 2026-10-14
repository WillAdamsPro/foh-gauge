#include "LayoutModel.h"
#include <fohcore/fohcore.h>
#include <limits>
#include <set>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace fohws {

// ===================== layout tree =====================
constexpr auto Leaf = LayoutNode::Leaf; constexpr auto Tabs = LayoutNode::Tabs; constexpr auto Split = LayoutNode::Split;
LayoutNode LayoutNode::tabs(std::vector<std::string> ids, int act) { LayoutNode n; n.kind = Tabs; for (auto& i : ids) n.children.push_back(leaf(i)); n.active = act; return n; }
LayoutNode LayoutNode::split(bool h, std::vector<std::pair<double, LayoutNode>> parts) {
    LayoutNode n; n.kind = Split; n.horizontal = h;
    for (auto& p : parts) { n.sizes.push_back(p.first); n.children.push_back(std::move(p.second)); }
    n.presetSizes = n.sizes; return n;
}
bool LayoutNode::operator==(const LayoutNode& o) const {
    if (kind != o.kind || panel != o.panel || horizontal != o.horizontal || active != o.active || children != o.children || sizes.size() != o.sizes.size()) return false;
    for (size_t i = 0; i < sizes.size(); ++i) if (std::fabs(sizes[i] - o.sizes[i]) > 1e-9) return false;
    return true;
}
void LayoutNode::collectPanels(std::vector<std::string>& out) const { if (kind == Leaf) out.push_back(panel); for (auto& c : children) c.collectPanels(out); }
bool LayoutNode::contains(const std::string& id) const { if (kind == Leaf) return panel == id; for (auto& c : children) if (c.contains(id)) return true; return false; }

static void renorm(std::vector<double>& s) { double t = 0; for (double v : s) t += std::max(0.0, v); if (t <= 0) { for (auto& v : s) v = 1.0 / (double) s.size(); return; } for (auto& v : s) v = std::max(0.0, v) / t; }
void normalise(LayoutNode& n) {
    for (auto& c : n.children) normalise(c);
    if (n.kind == Leaf) return;
    for (size_t i = n.children.size(); i-- > 0;) { auto& c = n.children[i];
        bool empty = (c.kind == Leaf && c.panel.empty()) || (c.kind != Leaf && c.children.empty());
        if (empty) { n.children.erase(n.children.begin() + (long) i); if (n.kind == Split && i < n.sizes.size()) n.sizes.erase(n.sizes.begin() + (long) i); if (n.presetSizes.size() > i) n.presetSizes.erase(n.presetSizes.begin() + (long) i); } }
    if (n.kind == Split) { n.sizes.resize(n.children.size(), 1.0 / std::max<size_t>(1, n.children.size())); renorm(n.sizes); n.presetSizes.resize(n.children.size(), 0); renorm(n.presetSizes); }
    if (n.kind == Tabs) n.active = n.children.empty() ? 0 : std::clamp(n.active, 0, (int) n.children.size() - 1);
    if (n.children.size() == 1 && (n.kind == Split || n.children[0].kind == Leaf)) { LayoutNode only = n.children[0]; n = only; }
}
static std::string firstLeaf(const LayoutNode& n) { if (n.kind == Leaf) return n.panel; for (auto& c : n.children) { auto s = firstLeaf(c); if (!s.empty()) return s; } return {}; }
static bool removeRec(LayoutNode& n, const std::string& id, std::string& neighbour) {
    for (size_t i = 0; i < n.children.size(); ++i) {
        auto& c = n.children[i];
        if (c.kind == Leaf && c.panel == id) {
            if (n.children.size() > 1) neighbour = firstLeaf(n.children[i == 0 ? 1 : i - 1]);
            n.children.erase(n.children.begin() + (long) i);
            if (n.kind == Split) { n.sizes.erase(n.sizes.begin() + (long) i); if (n.presetSizes.size() > i) n.presetSizes.erase(n.presetSizes.begin() + (long) i); }
            return true;
        }
        if (removeRec(c, id, neighbour)) return true;
    }
    return false;
}
std::string removePanel(LayoutNode& root, const std::string& id) {
    std::string nb;
    if (root.kind == Leaf) { if (root.panel == id) root.panel.clear(); return nb; }
    if (!removeRec(root, id, nb)) return {};
    normalise(root);
    return nb.empty() ? std::string(" ") : nb;
}
static LayoutNode* findLeafParent(LayoutNode& n, const std::string& id, LayoutNode** leafOut) {
    for (auto& c : n.children) { if (c.kind == LayoutNode::Leaf && c.panel == id) { *leafOut = &c; return &n; } if (auto* p = findLeafParent(c, id, leafOut)) return p; }
    return nullptr;
}
void addAsTab(LayoutNode& root, const std::string& id, const std::string& neighbour) {
    if (root.contains(id)) return;
    std::string nb = (!neighbour.empty() && root.contains(neighbour)) ? neighbour : firstLeaf(root);
    if (nb.empty()) { root = LayoutNode::leaf(id); return; }
    if (root.kind == LayoutNode::Leaf) { root = LayoutNode::tabs({ root.panel, id }, 1); return; }
    LayoutNode* leaf = nullptr; LayoutNode* parent = findLeafParent(root, nb, &leaf);
    if (!parent || !leaf) return;
    if (parent->kind == LayoutNode::Tabs) { parent->children.push_back(LayoutNode::leaf(id)); parent->active = (int) parent->children.size() - 1; }
    else *leaf = LayoutNode::tabs({ nb, id }, 1);
}

Size minSizeOf(const LayoutNode& n, const std::function<Size(const std::string&)>& minSize, int headerH) {
    if (n.kind == LayoutNode::Leaf) { auto s = minSize(n.panel); s.h += headerH; return s; }
    if (n.kind == LayoutNode::Tabs) { Size m; for (auto& c : n.children) { auto s = minSize(c.panel); m.w = std::max(m.w, s.w); m.h = std::max(m.h, s.h + headerH); } return m; }
    Size m; for (auto& c : n.children) { auto s = minSizeOf(c, minSize, headerH); if (n.horizontal) { m.w += s.w; m.h = std::max(m.h, s.h); } else { m.h += s.h; m.w = std::max(m.w, s.w); } }
    return m;
}
static int nodePriority(const LayoutNode& n, const std::function<int(const std::string&)>& pr) {
    if (n.kind == LayoutNode::Leaf) return pr(n.panel);
    int p = 1 << 30; for (auto& c : n.children) p = std::min(p, nodePriority(c, pr)); return p;
}
void enforceMinSizes(LayoutNode& n, int width, int height, const std::function<Size(const std::string&)>& minSize,
                     const std::function<int(const std::string&)>& priority) {
    if (n.kind != LayoutNode::Split) return;
    for (int guard = 0; guard < 16 && n.kind == LayoutNode::Split && n.children.size() > 1; ++guard) {
        bool ok = true; double total = n.horizontal ? width : height;
        for (size_t i = 0; i < n.children.size(); ++i) { auto m = minSizeOf(n.children[i], minSize); double avail = total * n.sizes[i]; if ((n.horizontal ? m.w : m.h) > avail + 0.5) ok = false; }
        if (ok) break;
        // fold the lowest-priority leaf child (prefer leaves; else the lowest node's first leaf) into its sibling as a tab
        size_t victim = 0; int vp = 1 << 30;
        for (size_t i = 0; i < n.children.size(); ++i) { int p = nodePriority(n.children[i], priority); if (n.children[i].kind == LayoutNode::Leaf) p -= 1; if (p < vp) { vp = p; victim = i; } }
        LayoutNode v = n.children[victim]; std::vector<std::string> ids; v.collectPanels(ids);
        size_t sib = victim == 0 ? 1 : victim - 1; double add = n.sizes[victim];
        n.children.erase(n.children.begin() + (long) victim); n.sizes.erase(n.sizes.begin() + (long) victim);
        if (n.presetSizes.size() > victim) n.presetSizes.erase(n.presetSizes.begin() + (long) victim);
        if (sib > victim) --sib;
        n.sizes[sib] += add;
        std::string nb; { std::vector<std::string> s; n.children[sib].collectPanels(s); nb = s.empty() ? "" : s.front(); }
        for (auto& id : ids) addAsTab(n.children[sib], id, nb);
        normalise(n);
    }
    if (n.kind != LayoutNode::Split) return;
    for (size_t i = 0; i < n.children.size(); ++i) {
        int w = n.horizontal ? (int) (width * n.sizes[i]) : width, h = n.horizontal ? height : (int) (height * n.sizes[i]);
        enforceMinSizes(n.children[i], w, h, minSize, priority);
    }
}

// ===================== displays =====================
Rect clampToDisplays(FloatingPanel& fp, const std::vector<DisplayInfo>& displays, int minW, int minH) {
    Rect r = fp.bounds; if (displays.empty()) return r;
    const auto usable = [](const DisplayInfo& x) { return x.area.w > 0 && x.area.h > 0; };
    const DisplayInfo* d = nullptr; for (auto& x : displays) if (usable(x) && x.id == fp.displayId) d = &x;
    if (!d) { // display gone: move onto the main display with an inset so the title bar is accessible
        const DisplayInfo* m = nullptr;
        for (auto& x : displays) if (usable(x) && (!m || x.isMain)) m = &x;
        if (!m) return r; // transient display refresh: preserve the last valid position
        r.x = m->area.x + 40; r.y = m->area.y + 40; d = m; fp.displayId = m->id;
    }
    const Rect& a = d->area;
    r.w = std::clamp(r.w, std::min(minW, a.w), a.w); r.h = std::clamp(r.h, std::min(minH, a.h), a.h);
    r.x = std::clamp(r.x, a.x, a.x + a.w - r.w); r.y = std::clamp(r.y, a.y, a.y + a.h - r.h);
    fp.bounds = r; return r;
}

// ===================== JSON mapping =====================
Json toJson(const LayoutNode& n) {
    Json j = Json::object();
    if (n.kind == LayoutNode::Leaf) { j.set("type", "leaf"); j.set("panel", n.panel); return j; }
    if (n.kind == LayoutNode::Tabs) { j.set("type", "tabs"); j.set("active", n.active); }
    else { j.set("type", "split"); j.set("dir", n.horizontal ? "h" : "v"); Json s = Json::array(); for (double v : n.sizes) s.push(v); j.set("sizes", s);
           Json p = Json::array(); for (double v : n.presetSizes) p.push(v); j.set("presetSizes", p); }
    Json c = Json::array(); for (auto& ch : n.children) c.push(toJson(ch)); j.set("children", c);
    return j;
}
static bool parseNode(const Json& j, LayoutNode& n, std::string& err, int depth, size_t& count) {
    if (depth > 32 || ++count > 256) { err = "Layout is too complex"; return false; }
    if (!j.isObject()) { err = "node is not an object"; return false; }
    auto t = j["type"].asString();
    n = {};
    if (t == "leaf") { n.kind = LayoutNode::Leaf; n.panel = j["panel"].asString(); if (n.panel.empty()) { err = "leaf without panel"; return false; } return true; }
    if (t != "tabs" && t != "split") { err = "unknown node type '" + t + "'"; return false; }
    n.kind = t == "tabs" ? LayoutNode::Tabs : LayoutNode::Split;
    if (!j["children"].isArray() || j["children"].items().empty()) { err = t + " without children"; return false; }
    for (auto& c : j["children"].items()) { LayoutNode ch; if (!parseNode(c, ch, err, depth + 1, count)) return false; if (n.kind == LayoutNode::Tabs && ch.kind != LayoutNode::Leaf) { err = "tab child must be a leaf"; return false; } n.children.push_back(std::move(ch)); }
    if (n.kind == LayoutNode::Tabs) {
        double active = j["active"].asNumber(0);
        if (!std::isfinite(active) || active < 0 || active > 1023 || std::floor(active) != active || (j.has("active") && j["active"].type() != Json::Number)) { err = "Invalid active tab"; return false; }
        n.active = std::clamp((int) active, 0, (int) n.children.size() - 1); return true;
    }
    if (j.has("dir") && j["dir"].asString() != "h" && j["dir"].asString() != "v") { err = "Invalid split direction"; return false; }
    n.horizontal = j["dir"].asString("h") != "v";
    for (const auto* key : {"sizes", "presetSizes"}) {
        if (std::string(key) == "presetSizes" && !j.has(key)) continue;
        if (!j[key].isArray()) { err = "Invalid split sizes"; return false; }
        double total = 0;
        for (const auto& v : j[key].items()) {
            double d = v.asNumber(-1);
            if (!std::isfinite(d) || d < 0 || d > 1) { err = "Invalid split proportion"; return false; }
            total += d;
        }
        if (total <= 0) { err = "Empty split proportions"; return false; }
    }
    for (auto& v : j["sizes"].items()) n.sizes.push_back(v.asNumber());
    for (auto& v : j["presetSizes"].items()) n.presetSizes.push_back(v.asNumber());
    if (n.sizes.size() != n.children.size()) { err = "sizes/children count mismatch"; return false; }
    renorm(n.sizes); if (n.presetSizes.size() != n.children.size()) n.presetSizes = n.sizes; else renorm(n.presetSizes);
    return true;
}
bool fromJson(const Json& j, LayoutNode& n, std::string& err) {
    LayoutNode candidate; size_t count = 0;
    if (!parseNode(j, candidate, err, 0, count)) return false;
    n = std::move(candidate); err.clear(); return true;
}
Json toJson(const Workspace& w) {
    Json j = Json::object();
    j.set("format", "foh-gauge-workspace"); j.set("version", w.version); j.set("id", w.id); j.set("name", w.name); j.set("builtIn", w.builtIn);
    j.set("root", toJson(w.root));
    Json f = Json::array();
    for (auto& fp : w.floating) { Json o = Json::object(); o.set("panel", fp.panel); Json b = Json::object(); b.set("x", fp.bounds.x); b.set("y", fp.bounds.y); b.set("w", fp.bounds.w); b.set("h", fp.bounds.h);
        o.set("bounds", b); o.set("display", fp.displayId); o.set("alwaysOnTop", fp.alwaysOnTop); o.set("returnNeighbour", fp.returnNeighbour); f.push(o); }
    j.set("floating", f); j.set("panelState", w.panelState);
    return j;
}
bool fromJson(const Json& j, Workspace& w, std::string& err) {
    if (!j.isObject() || j["format"].asString() != "foh-gauge-workspace") { err = "not a FOH Gauge workspace"; return false; }
    if (j["version"].asNumber() != 1) { err = "workspace version " + std::to_string(j["version"].asInt()) + " is newer than this app"; return false; }
    Workspace x; x.version = 1; x.id = j["id"].asString(); x.name = j["name"].asString(x.id); x.builtIn = j["builtIn"].asBool();
    if (x.id.empty() || x.id.size() > 128 || x.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) { err = "workspace without id"; return false; }
    if (!fromJson(j["root"], x.root, err)) return false;
    if (j.has("floating") && (!j["floating"].isArray() || j["floating"].items().size() > 64)) { err = "Invalid floating panels"; return false; }
    for (auto& o : j["floating"].items()) { FloatingPanel fp; fp.panel = o["panel"].asString(); auto& b = o["bounds"];
        if (!o.isObject() || fp.panel.empty() || !b.isObject()) { err = "Invalid floating panel"; return false; }
        for (const auto* key : {"x", "y", "w", "h"}) {
            double v = b[key].asNumber(std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(v) || std::floor(v) != v || std::abs(v) > 100000 || ((std::string(key) == "w" || std::string(key) == "h") && v <= 0)) {
                err = "Invalid floating bounds"; return false;
            }
        }
        fp.bounds = { b["x"].asInt(), b["y"].asInt(), b["w"].asInt(400), b["h"].asInt(300) }; fp.displayId = o["display"].asString(); fp.alwaysOnTop = o["alwaysOnTop"].asBool(); fp.returnNeighbour = o["returnNeighbour"].asString();
        if (!fp.panel.empty()) x.floating.push_back(fp); }
    if (j.has("panelState") && !j["panelState"].isObject()) { err = "Invalid panel state"; return false; }
    for (const auto& entry : j["panelState"].members()) {
        const auto& state = entry.second;
        if (!state.isObject()) { err = "Invalid panel state"; return false; }
        for (const auto* key : {"mic", "ref", "outA", "outB", "fraction", "targetChoice", "postMs", "targetOffset", "yMin", "yMax"}) {
            if (!state.has(key)) continue;
            double v = state[key].asNumber(std::numeric_limits<double>::quiet_NaN());
            bool valid = std::isfinite(v);
            std::string k(key);
            if (k == "yMin" || k == "yMax") valid &= std::abs(v) <= 300;
            else if (k == "targetOffset") valid &= std::abs(v) <= 100;
            else if (k == "postMs") valid &= v > 0 && v <= 10000;
            else valid &= v >= ((k == "mic" || k == "ref" || k == "outA" || k == "outB") ? -1 : 0) && v <= 1023 && std::floor(v) == v;
            if (!valid) { err = "Invalid panel setting: " + k; return false; }
        }
        if (state.has("yMin") && state["yMax"].asNumber() <= state["yMin"].asNumber()) { err = "Invalid plot range"; return false; }
        if (state["targetChoice"].asInt() == 4 && !state.has("targetText")) { err = "Missing saved target"; return false; }
        if (state.has("targetText") && !foh::parseTarget(state["targetText"].asString()).ok()) { err = "Invalid saved target"; return false; }
    }
    if (j["panelState"].isObject()) x.panelState = j["panelState"];
    std::vector<std::string> panels; x.root.collectPanels(panels);
    for (const auto& fp : x.floating) panels.push_back(fp.panel);
    std::set<std::string> unique;
    for (const auto& panel : panels) if (!unique.insert(panel).second) { err = "Duplicate panel in workspace"; return false; }
    w = std::move(x); err.clear(); return true;
}

// ===================== presets (ws-component-spec §7) =====================
std::vector<Workspace> builtInPresets() {
    auto T = [](std::vector<std::string> ids) { return LayoutNode::tabs(std::move(ids), 0); };
    std::vector<Workspace> v;
    { Workspace w; w.id = "setup"; w.name = "Setup"; w.builtIn = true;
      // spec: H[0.62 V[0.55 InputsAndDevices, 0.45 H[0.5 SignalMeters, 0.5 Calibration]], 0.38 V[0.60 LimitPreset, 0.40 Inspector]]
      // Generator is stacked as a tab with SignalMeters (the spec tree has no generator leaf).
      w.root = H({ {0.62, V({ {0.55, L("InputsAndDevices")}, {0.45, H({ {0.5, T({"SignalMeters", "Generator"})}, {0.5, L("Calibration")} })} })},
                   {0.38, V({ {0.60, L("LimitPreset")}, {0.40, L("Inspector")} })} });
      v.push_back(w); }
    { Workspace w; w.id = "alignment"; w.name = "Alignment"; w.builtIn = true;
      w.root = H({ {0.74, V({ {0.58, T({"TransferFunction", "Impulse"})}, {0.27, L("Phase")}, {0.15, H({ {0.6, L("DelayFinder")}, {0.4, L("Coherence")} })} })},
                   {0.26, T({"GuideInspector", "Inspector"})} });
      v.push_back(w); }
    { Workspace w; w.id = "tuning"; w.name = "Tuning"; w.builtIn = true;
      w.root = H({ {0.74, V({ {0.62, L("TransferFunction")}, {0.38, H({ {0.55, T({"RTA", "Phase"})}, {0.45, L("EQSuggestions")} })} })},
                   {0.26, V({ {0.55, L("TraceList")}, {0.45, L("Inspector")} })} });
      v.push_back(w); }
    { Workspace w; w.id = "rta-show"; w.name = "RTA/Show"; w.builtIn = true;
      w.root = H({ {0.70, V({ {0.65, L("RTA")}, {0.35, L("SPLHistory")} })},
                   {0.30, V({ {0.45, L("SPLNumbers")}, {0.25, L("Dose")}, {0.30, T({"Inspector", "Events"})} })} });
      v.push_back(w); }
    return v;
}

} // namespace fohws
