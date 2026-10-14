// Workspace layout: JSON round-trip, collapse rule and floating-window display clamping (pure C++, no JUCE).
#include <catch2/catch_test_macros.hpp>
#include "LayoutModel.h"
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
    w.root.children[0].children[1].children[0].active = 1;
    Workspace back; std::string err; REQUIRE(fromJson(Json::parse(toJson(w).dump()), back, err));
    REQUIRE(back.floating.size() == 1);
    CHECK(back.floating[0].panel == "DelayFinder"); CHECK(back.floating[0].bounds == Rect { 1700, 120, 420, 260 });
    CHECK(back.floating[0].displayId == "display-2"); CHECK(back.floating[0].alwaysOnTop); CHECK(back.floating[0].returnNeighbour == "TransferFunction");
    CHECK(back.panelState["RTA"]["fraction"].asInt() == 6);
    CHECK(back.root.children[0].children[1].children[0].active == 1);
}

TEST_CASE("bad JSON is rejected with an error, not a crash") {
    Workspace w; std::string err;
    CHECK_FALSE(fromJson(Json::parse("{ not json"), w, err)); CHECK_FALSE(err.empty());
    err.clear(); CHECK_FALSE(fromJson(Json::parse(R"({"format":"something-else","version":1})"), w, err));
    err.clear(); CHECK_FALSE(fromJson(Json::parse(R"({"format":"foh-gauge-workspace","version":99,"id":"x","name":"x","root":{"type":"leaf","panel":"RTA"}})"), w, err));
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
