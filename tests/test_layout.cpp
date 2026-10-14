// Workspace layout: JSON round-trip, collapse rule and floating-window display clamping (pure C++, no JUCE).
#include <catch2/catch_test_macros.hpp>
#include "LayoutModel.h"
#include "FileValidation.h"
using namespace fohws;

static Size mins(const std::string& id) { if (id == "TransferFunction" || id == "RTA") return {420, 220}; if (id == "Inspector") return {260, 140}; return {240, 140}; }
static int prio(const std::string& id) { return id == "Inspector" ? 0 : id == "TraceList" || id == "Events" ? 5 : 10; }

TEST_CASE("every built-in preset round-trips through JSON") {
    for (auto& w : builtInPresets()) {
        auto text = toJson(w).dump();
        Workspace back; std::string err;
        REQUIRE(fromJson(Json::parse(text), back, err));
        CHECK(err.empty());
        CHECK(back.id == w.id); CHECK(back.name == w.name); CHECK(back.builtIn == w.builtIn);
        CHECK(back.root == w.root);
        CHECK(toJson(back).dump() == text);
    }
}

TEST_CASE("presets follow the spec trees") {
    auto p = builtInPresets(); REQUIRE(p.size() == 4);
    CHECK(p[0].id == "setup"); CHECK(p[1].id == "alignment"); CHECK(p[2].id == "tuning"); CHECK(p[3].id == "rta-show");
    auto& a = p[1].root; REQUIRE(a.kind == LayoutNode::Split); CHECK(a.horizontal);
    CHECK(a.sizes[0] == 0.74); CHECK(a.children[0].sizes[0] == 0.58); CHECK(a.children[0].sizes[1] == 0.27); CHECK(a.children[0].sizes[2] == 0.15);
    CHECK(a.children[1].kind == LayoutNode::Tabs); CHECK(a.children[1].children[0].panel == "GuideInspector");
    for (auto& w : p) { double s = 0; for (double v : w.root.sizes) s += v; CHECK(s > 0.999); CHECK(s < 1.001); }
}

TEST_CASE("floating windows, panel state and tab state survive serialisation") {
    Workspace w = builtInPresets()[2];
    w.floating.push_back({ "DelayFinder", { 1700, 120, 420, 260 }, "display-2", true, "TransferFunction" });
    Json st = Json::object(); st.set("fraction", 6); w.panelState.set("RTA", st);
    w.root.children[0].children[1].active = 1;
    Workspace back; std::string err; REQUIRE(fromJson(Json::parse(toJson(w).dump()), back, err));
    REQUIRE(back.floating.size() == 1);
    CHECK(back.floating[0].panel == "DelayFinder"); CHECK(back.floating[0].bounds == Rect { 1700, 120, 420, 260 });
    CHECK(back.floating[0].displayId == "display-2"); CHECK(back.floating[0].alwaysOnTop); CHECK(back.floating[0].returnNeighbour == "TransferFunction");
    CHECK(back.panelState["RTA"]["fraction"].asInt() == 6);
    CHECK(back.root.children[0].children[1].active == 1);
}

TEST_CASE("bad JSON is rejected with an error, not a crash") {
    Workspace w; std::string err;
    CHECK_FALSE(fromJson(Json::parse("{ not json"), w, err)); CHECK_FALSE(err.empty());
    err.clear(); CHECK_FALSE(fromJson(Json::parse(R"({"format":"something-else","version":1})"), w, err));
    err.clear(); CHECK_FALSE(fromJson(Json::parse(R"({"format":"foh-gauge-workspace","version":99,"id":"x","name":"x","root":{"type":"leaf","panel":"RTA"}})"), w, err));
}

TEST_CASE("Empty dock roots round-trip after the last panel is floated or closed", "[layout][workspace]") {
    for (auto workspace : builtInPresets()) {
        std::vector<std::string> ids; workspace.root.collectPanels(ids);
        for (const auto& id : ids) {
            auto neighbour = removePanel(workspace.root, id);
            workspace.floating.push_back({id, {40, 65, 500, 300}, "main", false, neighbour});
        }
        REQUIRE(workspace.root == LayoutNode::leaf(""));
        Workspace restored; std::string error;
        REQUIRE(fromJson(toJson(workspace), restored, error));
        CHECK(toJson(restored) == toJson(workspace));
        std::vector<std::string> docked; restored.root.collectPanels(docked);
        CHECK(docked.empty());
        // Docking after restore can seed a new tree from the empty root.
        auto panel = restored.floating.back(); restored.floating.pop_back();
        addAsTab(restored.root, panel.panel, panel.returnNeighbour);
        CHECK(restored.root.contains(panel.panel));
        workspace.floating.clear();
        REQUIRE(fromJson(toJson(workspace), restored, error));
        CHECK(restored.root == LayoutNode::leaf(""));
    }
}

TEST_CASE("Empty root support still rejects absent IDs and empty child leaves", "[layout][workspace]") {
    auto node = LayoutNode::leaf("RTA"); const auto before = node;
    std::string error;
    for (const auto* text : {
        R"({"type":"leaf"})", R"({"type":"leaf","panel":null})",
        R"({"type":"leaf","panel":0})",
        R"({"type":"tabs","children":[{"type":"leaf","panel":""}]})",
        R"({"type":"split","sizes":[1],"children":[{"type":"leaf","panel":""}]})"}) {
        REQUIRE_FALSE(fromJson(Json::parse(text), node, error));
        CHECK(node == before);
        CHECK_FALSE(error.empty());
    }
}

TEST_CASE("Json escapes and numbers round-trip") {
    Json j = Json::object(); j.set("s", "quote \" backslash \\ tab \t \xc2\xb0"); j.set("n", 0.125); j.set("neg", -3); j.set("b", true);
    auto back = Json::parse(j.dump(0)); CHECK(back == j);
}

TEST_CASE("removePanel collapses single-child nodes and returns a neighbour") {
    auto r = builtInPresets()[1].root;
    auto nb = removePanel(r, "Coherence"); CHECK(!nb.empty()); CHECK_FALSE(r.contains("Coherence")); CHECK(r.contains("DelayFinder"));
    // the H[DelayFinder, Coherence] split became a plain leaf
    CHECK(r.children[0].children[2].kind == LayoutNode::Leaf);
    addAsTab(r, "Coherence", "DelayFinder"); CHECK(r.children[0].children[2].kind == LayoutNode::Tabs);
}

TEST_CASE("collapse rule folds the lowest-priority leaf into a tab") {
    auto r = H({ {0.7, L("TransferFunction")}, {0.3, L("Inspector")} });
    auto wide = r; enforceMinSizes(wide, 1400, 800, mins, prio); CHECK(wide == r);           // fits: unchanged
    auto narrow = r; enforceMinSizes(narrow, 600, 800, mins, prio);                           // 180 px for the inspector < 260
    REQUIRE(narrow.kind == LayoutNode::Tabs); CHECK(narrow.children.size() == 2); CHECK(narrow.contains("Inspector")); CHECK(narrow.contains("TransferFunction"));
}

TEST_CASE("clamp: a window on a connected display stays put") {
    std::vector<DisplayInfo> d { { "main", { 0, 0, 1440, 900 }, true }, { "display-2", { 1440, 0, 1920, 1080 }, false } };
    FloatingPanel fp { "RTA", { 1600, 100, 500, 300 }, "display-2" };
    CHECK(clampToDisplays(fp, d) == Rect { 1600, 100, 500, 300 }); CHECK(fp.displayId == "display-2");
}

TEST_CASE("clamp: a missing display moves the window onto the main display") {
    std::vector<DisplayInfo> d { { "main", { 0, 25, 1440, 875 }, true } };
    FloatingPanel fp { "SPLNumbers", { 2400, 300, 500, 300 }, "display-2" };
    auto r = clampToDisplays(fp, d);
    CHECK(fp.displayId == "main"); CHECK(r.x == 40); CHECK(r.y == 65); CHECK(r.w == 500); CHECK(r.h == 300);
}

TEST_CASE("clamp: off-edge and oversized windows are pulled inside the work area") {
    std::vector<DisplayInfo> d { { "main", { 0, 0, 1280, 800 }, true } };
    FloatingPanel a { "RTA", { 1200, 700, 400, 300 }, "main" };
    CHECK(clampToDisplays(a, d) == Rect { 880, 500, 400, 300 });
    FloatingPanel b { "RTA", { -50, -50, 3000, 2000 }, "main" };
    CHECK(clampToDisplays(b, d) == Rect { 0, 0, 1280, 800 });
    FloatingPanel c { "RTA", { 10, 10, 100, 50 }, "main" };       // below the 320x200 floating min
    auto rc = clampToDisplays(c, d); CHECK(rc.w == 320); CHECK(rc.h == 200);
}

TEST_CASE("clamp: no displays leaves bounds untouched") {
    FloatingPanel fp { "RTA", { 5, 6, 700, 400 }, "x" }; CHECK(clampToDisplays(fp, {}) == Rect { 5, 6, 700, 400 });
}

TEST_CASE("clamp: unplugging recovers every window onto the designated main display") {
    std::vector<DisplayInfo> connected {
        { "secondary", { -1920, 0, 1920, 1080 }, false },
        { "main", { 0, 25, 1440, 875 }, true },
        { "removed", { 1440, 0, 1920, 1080 }, false }
    };
    FloatingPanel a { "RTA", { 1600, 100, 500, 300 }, "removed" };
    FloatingPanel b { "DelayFinder", { 2000, 500, 600, 400 }, "removed" };
    FloatingPanel c { "Coherence", { -1500, 100, 500, 300 }, "secondary" };
    connected.pop_back();
    CHECK(clampToDisplays(a, connected) == Rect { 40, 65, 500, 300 });
    CHECK(clampToDisplays(b, connected) == Rect { 40, 65, 600, 400 });
    CHECK(a.displayId == "main"); CHECK(b.displayId == "main");
    CHECK(clampToDisplays(c, connected) == Rect { -1500, 100, 500, 300 });
    CHECK(c.displayId == "secondary");
}

TEST_CASE("clamp: changing a connected display work area keeps the title bar accessible") {
    FloatingPanel fp { "RTA", { 1000, 700, 500, 300 }, "main" };
    CHECK(clampToDisplays(fp, { { "main", { 0, 40, 1024, 700 }, true } }) == Rect { 524, 440, 500, 300 });
    CHECK(fp.displayId == "main");
}

TEST_CASE("workspace restore rejects multiple owners of the same panel") {
    auto w = builtInPresets()[2];
    SECTION("two docked slots") { w.root = LayoutNode::tabs({"RTA", "RTA"}); }
    SECTION("docked and floating") { w.floating.push_back({"RTA", {0, 0, 500, 300}, "main"}); }
    SECTION("two floating windows") {
        removePanel(w.root, "RTA");
        w.floating.push_back({"RTA", {0, 0, 500, 300}, "main"});
        w.floating.push_back(w.floating.front());
    }
    Workspace restored = builtInPresets()[0];
    const auto before = toJson(restored).dump();
    std::string error;
    CHECK_FALSE(fromJson(toJson(w), restored, error));
    CHECK(error == "Duplicate panel in workspace");
    CHECK(toJson(restored).dump() == before);
}

TEST_CASE("display refresh ignores empty work areas and preserves state until recovery") {
    FloatingPanel panel {"RTA", {-1800, 100, 500, 300}, "removed"};
    const auto original = panel.bounds;
    CHECK(clampToDisplays(panel, {{"main", {0, 0, 0, 0}, true}}) == original);
    CHECK(panel.displayId == "removed");
    CHECK(clampToDisplays(panel, {{"main", {0, 0, 0, 0}, true},
                                 {"usable", {-1440, 25, 1440, 875}, false}})
          == Rect {-1400, 65, 500, 300});
    CHECK(panel.displayId == "usable");
    for (int i = 0; i < 1000; ++i)
        CHECK(clampToDisplays(panel, {{"usable", {-1440, 25, 1440, 875}, true}})
              == Rect {-1400, 65, 500, 300});
}
TEST_CASE("Damaged layouts are rejected transactionally", "[layout][files]") {
    auto w = builtInPresets()[1]; const auto before = toJson(w); auto j = before; std::string err;
    SECTION("absurd active tab") { auto root = toJson(LayoutNode::tabs({"RTA", "Phase"})); root.set("active", 1e100); j.set("root", root); }
    SECTION("missing saved target") { Json s = Json::object(), p = Json::object(); p.set("targetChoice", 4); s.set("RTA", p); j.set("panelState", s); }
    SECTION("unsafe id") { j.set("id", "../outside"); }
    SECTION("nonintegral version") { j.set("version", 1.5); }
    SECTION("bad floating bounds") {
        Json f = Json::array(), p = Json::object(), b = Json::object();
        b.set("x", 1e100); b.set("y", 0); b.set("w", -1); b.set("h", 300); p.set("bounds", b); p.set("panel", "Test"); f.push(p); j.set("floating", f);
    }
    SECTION("negative split size") { auto root = j["root"]; Json sizes = Json::array(); sizes.push(-1); sizes.push(2); root.set("sizes", sizes); j.set("root", root); }
    SECTION("nonnumeric split size") { auto root = j["root"]; Json sizes = Json::array(); sizes.push("bad"); sizes.push(1); root.set("sizes", sizes); j.set("root", root); }
    SECTION("huge plot setting") { Json s = Json::object(), p = Json::object(); p.set("yMin", 1e100); s.set("TransferFunction", p); j.set("panelState", s); }
    SECTION("corrupt saved target") { Json s = Json::object(), p = Json::object(); p.set("targetChoice", 4); p.set("targetText", "20 0\n1000 nan"); s.set("RTA", p); j.set("panelState", s); }
    SECTION("duplicate panels") { j.set("root", toJson(LayoutNode::tabs({"RTA", "RTA"}))); }
    SECTION("deep layout") { auto root = toJson(L("RTA")); for (int i = 0; i < 40; ++i) { Json p = Json::object(), children = Json::array(), sizes = Json::array(); p.set("type", "split"); children.push(root); sizes.push(1); p.set("children", children); p.set("sizes", sizes); root = p; } j.set("root", root); }
    REQUIRE_FALSE(fromJson(j, w, err)); REQUIRE_FALSE(err.empty()); REQUIRE(toJson(w) == before);
}
TEST_CASE("Failed node parsing preserves its previous tree", "[layout][files]") {
    auto n = builtInPresets()[1].root; const auto before = n; std::string error;
    REQUIRE_FALSE(fromJson(Json::parse(R"({"type":"split","children":[{"type":"leaf","panel":"RTA"}],"sizes":["bad"]})"), n, error));
    REQUIRE(n == before);
}

TEST_CASE("Preference XML preflight bounds recursion bytes and encodings", "[prefs][files]") {
    REQUIRE(fohapp::boundedXml("\xef\xbb\xbf" "<?xml version=\"1.0\"?>\r\n<PROPERTIES><VALUE name=\"x\" val=\"&lt;\" /></PROPERTIES>"));
    for (const auto& bad : {std::string(), std::string("<PROPERTIES>"), std::string("<PROPERTIES val=\"unterminated>"),
                            std::string("<!DOCTYPE a [<!ENTITY x SYSTEM \"file:///synthetic\">]><a/>"),
                            std::string("\xff\xfe<a/>"), std::string(1024 * 1024 + 1, ' ')}) REQUIRE_FALSE(fohapp::boundedXml(bad));
    std::string deep; for (int i = 0; i < 40; ++i) deep += "<a>"; for (int i = 0; i < 40; ++i) deep += "</a>";
    REQUIRE_FALSE(fohapp::boundedXml(deep));
    std::string wide = "<a>"; for (int i = 0; i < 4097; ++i) wide += "<b/>"; wide += "</a>";
    REQUIRE_FALSE(fohapp::boundedXml(wide));
}

TEST_CASE("Workspace preserves disabled routing channels", "[layout][files]") {
    auto w = builtInPresets()[0]; Json routing = Json::object();
    for (const auto* key : {"mic", "ref", "outA", "outB"}) routing.set(key, -1);
    w.panelState.set("InputsAndDevices", routing); Workspace loaded; std::string error;
    REQUIRE(fromJson(toJson(w), loaded, error)); REQUIRE(loaded.panelState == w.panelState);
}

TEST_CASE("Saved targets reject invalid choices and flags atomically", "[layout][target][regression]") {
    const auto original = builtInPresets()[1];
    for (const auto& entry : std::vector<std::pair<std::string, Json>>{{"targetChoice", 0}, {"targetChoice", 5},
            {"targetShown", "true"}, {"targetBand", Json::array()}, {"targetName", 3},
            {"targetText", "20 0\n1000"}, {"targetText", ""}, {"targetText", Json::object()}}) {
        INFO(entry.first);
        auto candidate = original; auto state = Json::object(); state.set(entry.first, entry.second);
        candidate.panelState.set("TransferFunction", state);
        auto out = original; std::string error;
        REQUIRE_FALSE(fromJson(toJson(candidate), out, error));
        CHECK_FALSE(error.empty()); CHECK(toJson(out) == toJson(original));
    }
}
