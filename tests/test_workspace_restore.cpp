#include <catch2/catch_test_macros.hpp>
#include "WorkspaceManager.h"
#include "FloatingPanels.h"

namespace {
struct TemporaryWorkspaces {
    // CTest processes may start before either fixture creates its directory.
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("foh-workspace-restore-" + juce::Uuid().toString());
    ~TemporaryWorkspaces() { folder.deleteRecursively(); }
};
}

TEST_CASE("Floating panel positions survive workspace manager relaunch", "[app][workspace]") {
    TemporaryWorkspaces temp;
    const std::vector<fohws::DisplayInfo> displays {
        { "main", { 0, 25, 1440, 875 }, true },
        { "secondary", { -1920, 0, 1920, 1080 }, false }
    };
    {
        WorkspaceManager manager(temp.folder);
        auto& workspace = manager.at(2);
        REQUIRE(workspace.root.contains("RTA"));
        auto neighbour = fohws::removePanel(workspace.root, "RTA");
        workspace.floating.push_back({ "RTA", { -1800, 100, 500, 300 }, "secondary", true, neighbour });
        manager.saveCurrent(2);
    }
    {
        WorkspaceManager relaunched(temp.folder);
        auto& workspace = relaunched.at(2);
        REQUIRE(workspace.floating.size() == 1);
        CHECK_FALSE(workspace.root.contains("RTA"));
        auto& panel = workspace.floating.front();
        CHECK(panel.alwaysOnTop);
        CHECK_FALSE(panel.returnNeighbour.empty());
        const auto returnNeighbour = panel.returnNeighbour;
        CHECK(panel.bounds == fohws::Rect { -1800, 100, 500, 300 });
        SECTION("Unplug after restoring on the saved display") {
            CHECK(fohws::clampToDisplays(panel, displays) == fohws::Rect { -1800, 100, 500, 300 });
            CHECK(panel.displayId == "secondary");
            CHECK(fohws::clampToDisplays(panel, { displays.front() }) == fohws::Rect { 40, 65, 500, 300 });
        }
        SECTION("Launch with the saved display already missing") {
            CHECK(panel.displayId == "secondary");
            CHECK(fohws::clampToDisplays(panel, { displays.front() }) == fohws::Rect { 40, 65, 500, 300 });
        }
        CHECK(panel.displayId == "main");
        CHECK(panel.alwaysOnTop);
        CHECK(panel.returnNeighbour == returnNeighbour);
        // Repeated display notifications must leave the recovered position unchanged.
        CHECK(fohws::clampToDisplays(panel, { displays.front() }) == fohws::Rect { 40, 65, 500, 300 });
        relaunched.saveAll();
    }
    WorkspaceManager recovered(temp.folder);
    REQUIRE(recovered.at(2).floating.size() == 1);
    const auto& panel = recovered.at(2).floating.front();
    CHECK(panel.displayId == "main");
    CHECK(panel.bounds == fohws::Rect { 40, 65, 500, 300 });
    CHECK(panel.alwaysOnTop);
    CHECK_FALSE(panel.returnNeighbour.empty());
}

TEST_CASE("Missing-display recovery persists every floating panel on a smaller main display", "[app][workspace]") {
    TemporaryWorkspaces temp;
    const std::vector<fohws::DisplayInfo> remaining {
        { "secondary", { -1920, 0, 1920, 1080 }, false },
        // Main is deliberately not first, and its work area has a nonzero origin.
        { "main", { 100, 40, 1024, 700 }, true }
    };
    const std::vector<fohws::DisplayInfo> connected {
        remaining[0], remaining[1], { "removed", { 1124, 0, 2560, 1440 }, false }
    };
    const std::vector<fohws::Rect> expected {
        { 100, 40, 1024, 700 }, { 140, 80, 500, 300 }, { -1800, 100, 500, 300 }
    };
    {
        WorkspaceManager manager(temp.folder);
        auto& workspace = manager.at(2);
        for (const auto* id : { "RTA", "TransferFunction", "Inspector" }) {
            auto neighbour = fohws::removePanel(workspace.root, id);
            workspace.floating.push_back({ id, {}, {}, false, neighbour });
        }
        workspace.floating[0].bounds = { 1200, 100, 2200, 1200 };
        workspace.floating[1].bounds = { 1800, 500, 500, 300 };
        workspace.floating[2].bounds = expected[2];
        workspace.floating[0].displayId = workspace.floating[1].displayId = "removed";
        workspace.floating[2].displayId = "secondary";
        for (auto& panel : workspace.floating) {
            const auto savedBounds = panel.bounds;
            CHECK(fohws::clampToDisplays(panel, connected) == savedBounds);
        }
        manager.saveCurrent(2);
    }
    {
        WorkspaceManager relaunched(temp.folder);
        auto& panels = relaunched.at(2).floating;
        REQUIRE(panels.size() == expected.size());
        for (size_t i = 0; i < panels.size(); ++i) {
            CHECK(fohws::clampToDisplays(panels[i], remaining) == expected[i]);
            CHECK(panels[i].displayId == (i == 2 ? "secondary" : "main"));
        }
        relaunched.saveAll();
    }
    WorkspaceManager recovered(temp.folder);
    const auto& panels = recovered.at(2).floating;
    REQUIRE(panels.size() == expected.size());
    for (size_t i = 0; i < panels.size(); ++i) {
        CHECK(panels[i].bounds == expected[i]);
        CHECK(panels[i].displayId == (i == 2 ? "secondary" : "main"));
        CHECK_FALSE(recovered.at(2).root.contains(panels[i].panel));
    }
}

TEST_CASE("floating panel peers release ownership on repeated open and close", "[app][workspace][stress]") {
    juce::ScopedJuceInitialiser_GUI initialiser;
    AppModel model;
    Panel panel(model, "Synthetic", "Synthetic");
    fohws::FloatingPanel state {"Synthetic", {40, 65, 500, 300}, "missing-display"};
    const int peers = juce::Desktop::getInstance().getNumComponents();
    for (int i = 0; i < 100; ++i) {
        juce::Component::SafePointer<FloatingPanelWindow> pointer;
        {
            FloatingPanelWindow window(panel, state);
            pointer = &window;
            CHECK(window.getContentComponent() == &panel);
            window.setPinned(i % 2 == 0);
            window.setBounds(40, 65, 550 + i % 10, 350 + i % 10);
            window.parentSizeChanged();
            CHECK(state.bounds.w > 0);
        }
        CHECK(pointer == nullptr);
        CHECK(panel.getParentComponent() == nullptr);
        CHECK(juce::Desktop::getInstance().getNumComponents() == peers);
    }
}
TEST_CASE("Corrupt current workspace falls back to the previous saved layout", "[app][workspace][files]") {
    TemporaryWorkspaces temp;
    fohws::Json saved;
    {
        WorkspaceManager manager(temp.folder); manager.at(1).name = "Synthetic saved layout";
        saved = fohws::toJson(manager.at(1)); manager.save(1);
    }
    auto current = temp.folder.getChildFile("alignment.current.json");
    SECTION("truncated") { REQUIRE(current.replaceWithText("{\"root\":")); }
    SECTION("empty") { REQUIRE(current.replaceWithText("")); }
    SECTION("garbled") { REQUIRE(current.replaceWithText("garbled")); }
    SECTION("wrong encoding") { const char bytes[] = "\xff\xfe{}"; REQUIRE(current.replaceWithData(bytes, sizeof(bytes) - 1)); }
    SECTION("absurd bounds") {
        auto json = saved; auto root = json["root"]; auto sizes = fohws::Json::array(); sizes.push(1e100); sizes.push(-1); root.set("sizes", sizes); json.set("root", root);
        REQUIRE(current.replaceWithText(juce::String(json.dump())));
    }
    WorkspaceManager loaded(temp.folder);
    REQUIRE(fohws::toJson(loaded.at(1)) == saved); REQUIRE_FALSE(loaded.loadErrors().isEmpty());
}
