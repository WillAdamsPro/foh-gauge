#include <catch2/catch_test_macros.hpp>
#include "WorkspaceManager.h"

namespace {
struct TemporaryWorkspaces {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("foh-workspace-restore", "", false);
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
        CHECK(fohws::clampToDisplays(panel, displays) == fohws::Rect { -1800, 100, 500, 300 });
        CHECK(panel.displayId == "secondary");
        // The same recovery path handles unplugging while open and launching without that display.
        CHECK(fohws::clampToDisplays(panel, { displays.front() }) == fohws::Rect { 40, 65, 500, 300 });
        CHECK(panel.displayId == "main");
        relaunched.saveAll();
    }
    WorkspaceManager recovered(temp.folder);
    REQUIRE(recovered.at(2).floating.size() == 1);
    const auto& panel = recovered.at(2).floating.front();
    CHECK(panel.displayId == "main");
    CHECK(panel.bounds == fohws::Rect { 40, 65, 500, 300 });
}
