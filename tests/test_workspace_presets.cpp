#include <catch2/catch_test_macros.hpp>
#include "WorkspaceManager.h"
#include "WorkspacePresetCommands.h"
#include <algorithm>

namespace {
struct TemporaryWorkspaces {
    juce::File folder = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("foh-workspace-tests", "", false);
    ~TemporaryWorkspaces() { folder.deleteRecursively(); }
};

std::vector<std::string> panels(const fohws::Workspace& workspace) {
    std::vector<std::string> ids;
    workspace.root.collectPanels(ids);
    for (const auto& floating : workspace.floating) ids.push_back(floating.panel);
    std::sort(ids.begin(), ids.end());
    return ids;
}
}

TEST_CASE("Cmd+1-4 restore the four workspace panel sets in order", "[app][workspace]") {
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    TemporaryWorkspaces temporary;
    WorkspaceManager workspaces(temporary.folder);
    int current = -1;
    WorkspacePresetCommands commands([&](int index) {
        if (current >= 0) workspaces.saveCurrent(current);
        current = index;
    });
    const std::vector<std::vector<std::string>> expected {
        { "Calibration", "Generator", "InputsAndDevices", "Inspector", "LimitPreset", "SignalMeters" },
        { "Coherence", "DelayFinder", "GuideInspector", "Impulse", "Inspector", "Phase", "TransferFunction" },
        { "EQSuggestions", "Inspector", "Phase", "RTA", "TraceList", "TransferFunction" },
        { "Dose", "Events", "Inspector", "RTA", "SPLHistory", "SPLNumbers" }
    };
    const std::vector<std::string> names { "Setup", "Alignment", "Tuning", "RTA/Show" };

    // Visit in both directions, repeatedly: selecting a preset must not inherit
    // the outgoing workspace's panel set.
    for (int index : { 0, 1, 2, 3, 2, 1, 0, 3 }) {
        INFO("preset " << index);
        const juce::KeyPress key('1' + index, juce::ModifierKeys::commandModifier, 0);
        const auto id = commands.keyMappings()->findCommandForKeyPress(key);
        REQUIRE(id == WorkspacePresetCommands::firstPreset + index);
        REQUIRE(commands.commandManager().invokeDirectly(id, false));
        REQUIRE(current == index);
        CHECK(workspaces.at(current).name == names[(size_t) index]);
        CHECK(panels(workspaces.at(current)) == expected[(size_t) index]);
    }
    workspaces.saveAll();
    WorkspaceManager relaunched(temporary.folder);
    for (int i = 0; i < 4; ++i) CHECK(panels(relaunched.at(i)) == expected[(size_t) i]);
    CHECK(commands.keyMappings()->findCommandForKeyPress(juce::KeyPress('1')) == 0);
    CHECK(commands.keyMappings()->findCommandForKeyPress(
        juce::KeyPress('1', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) == 0);
}

TEST_CASE("each preset restores its own edited panel set and can reset independently", "[app][workspace]") {
    TemporaryWorkspaces temporary;
    const auto defaults = fohws::builtInPresets();
    {
        WorkspaceManager workspaces(temporary.folder);
        fohws::removePanel(workspaces.at(0).root, "Calibration");
        fohws::removePanel(workspaces.at(1).root, "Coherence");
        fohws::removePanel(workspaces.at(2).root, "Phase");
        fohws::removePanel(workspaces.at(3).root, "Events");
        // Exercise both an explicit save and live layouts saved on departure.
        workspaces.save(0);
        workspaces.saveAll();
    }
    WorkspaceManager relaunched(temporary.folder);
    const char* removed[] { "Calibration", "Coherence", "Phase", "Events" };
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(relaunched.at(i).root.contains(removed[i]));
        relaunched.resetToDefault(i);
        CHECK(panels(relaunched.at(i)) == panels(defaults[(size_t) i]));
        for (int j = i + 1; j < 4; ++j) CHECK_FALSE(relaunched.at(j).root.contains(removed[j]));
    }
    WorkspaceManager reset(temporary.folder);
    for (int i = 0; i < 4; ++i) CHECK(panels(reset.at(i)) == panels(defaults[(size_t) i]));
}
