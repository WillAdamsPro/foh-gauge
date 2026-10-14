#include <catch2/catch_test_macros.hpp>
#include "WorkspaceManager.h"
#include "WorkspacePresetCommands.h"
#include "WorkspaceView.h"
#include <set>
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
        { "Calibration", "Generator", "InputsAndDevices", "Inspector", "SignalMeters" },
        { "Coherence", "DelayFinder", "GuideInspector", "Impulse", "Inspector", "Phase", "TransferFunction" },
        { "Inspector", "Phase", "RTA", "TraceList", "TransferFunction" },
        { "Inspector", "RTA", "SPLNumbers", "ShowMonitoring" }
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
        fohws::removePanel(workspaces.at(3).root, "SPLNumbers");
        // Exercise both an explicit save and live layouts saved on departure.
        workspaces.save(0);
        workspaces.saveAll();
    }
    WorkspaceManager relaunched(temporary.folder);
    const char* removed[] { "Calibration", "Coherence", "Phase", "SPLNumbers" };
    for (int i = 0; i < 4; ++i) {
        CHECK_FALSE(relaunched.at(i).root.contains(removed[i]));
        relaunched.resetToDefault(i);
        CHECK(panels(relaunched.at(i)) == panels(defaults[(size_t) i]));
        for (int j = i + 1; j < 4; ++j) CHECK_FALSE(relaunched.at(j).root.contains(removed[j]));
    }
    WorkspaceManager reset(temporary.folder);
    for (int i = 0; i < 4; ++i) CHECK(panels(reset.at(i)) == panels(defaults[(size_t) i]));
}

TEST_CASE("Save As keeps live workspace and floating state references valid", "[app][workspace][stress]") {
    TemporaryWorkspaces temporary;
    WorkspaceManager manager(temporary.folder);
    auto* workspace = &manager.at(2);
    fohws::removePanel(workspace->root, "RTA");
    workspace->floating.push_back({"RTA", {40, 65, 500, 300}, "main"});
    auto* floating = &workspace->floating.front();
    for (int i = 0; i < 64; ++i) {
        const int copy = manager.saveAs(2, "Synthetic copy");
        REQUIRE(&manager.at(2) == workspace);
        REQUIRE(&manager.at(2).floating.front() == floating);
        CHECK(manager.at(copy).floating.front().bounds == floating->bounds);
    }
    floating->bounds.x = 80;
    manager.saveCurrent(2);
    WorkspaceManager restored(temporary.folder);
    REQUIRE(restored.at(2).floating.size() == 1);
    CHECK(restored.at(2).floating.front().bounds.x == 80);
}

TEST_CASE("workspace view survives repeated shortcuts rebuilds and Show toggles", "[app][workspace][stress]") {
    juce::ScopedJuceInitialiser_GUI initialiser;
    AppModel model;
    PanelRegistry registry;
    auto workspaces = fohws::builtInPresets();
    std::set<std::string> ids;
    for (auto& w : workspaces) { std::vector<std::string> p; w.root.collectPanels(p); ids.insert(p.begin(), p.end()); }
    int live = 0, created = 0;
    struct CountedPanel : Panel {
        int& live;
        CountedPanel(AppModel& m, const std::string& id, int& count) : Panel(m, id, id), live(count) { ++live; }
        ~CountedPanel() override { --live; }
    };
    for (const auto& id : ids) registry.add({id, id, [&, id](AppModel& m) {
        ++created; return std::make_unique<CountedPanel>(m, id, live);
    }});
    {
        WorkspaceView view(model, registry);
        view.setSize(1440, 900);
        WorkspacePresetCommands commands([&](int index) { view.setWorkspace(&workspaces[(size_t) index]); });
        for (int i = 0; i < 1000; ++i) {
            const int index = (i / 4) % 2 ? 3 - i % 4 : i % 4;
            REQUIRE(commands.commandManager().invokeDirectly(WorkspacePresetCommands::firstPreset + index, false));
            CHECK(view.workspace() == &workspaces[(size_t) index]);
            const auto saved = fohws::toJson(view.workspace()->root).dump();
            view.setShowMode(true);
            view.activateTab({}, -1);
            view.closePanel("RTA");
            view.setSize(600, 360);
            view.setShowMode(false);
            view.setSize(1440, 900);
            view.rebuild();
            CHECK(fohws::toJson(view.workspace()->root).dump() == saved);
            CHECK(created <= (int) ids.size());
            CHECK(live == created);
        }
        auto tabs = fohws::builtInPresets()[2]; tabs.root = fohws::LayoutNode::tabs({"RTA", "Phase"});
        view.setWorkspace(&tabs);
        view.activateTab({}, -1); view.activateTab({}, 2); view.activateTab({99}, 0);
        CHECK(tabs.root.active == 0);
        view.activateTab({}, 1); CHECK(tabs.root.active == 1);
        view.setWorkspace(nullptr);
        view.activateTab({}, 0);
    }
    CHECK(live == 0);
}

TEST_CASE("bundled typefaces are released with the UI before JUCE shutdown", "[app][workspace][stress]") {
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::Typeface::Ptr regular, semibold, bold;
    {
        FohLookAndFeel look;
        REQUIRE(look.regular != nullptr);
        REQUIRE(look.semibold != nullptr);
        REQUIRE(look.bold != nullptr);
        regular = look.regular; semibold = look.semibold; bold = look.bold;
    }
    // Only these test references may remain. Static ownership would keep an
    // extra reference alive until after JUCE's font machinery was destroyed.
    CHECK(regular->getReferenceCount() == 1);
    CHECK(semibold->getReferenceCount() == 1);
    CHECK(bold->getReferenceCount() == 1);
}

TEST_CASE("presets contain only finished registered panels", "[app][workspace]") {
    juce::ScopedJuceInitialiser_GUI initialiser;
    AppModel model;
    const auto& registry = PanelRegistry::standard();
    for (const auto& workspace : fohws::builtInPresets()) {
        for (const auto& id : panels(workspace)) {
            INFO(id);
            auto panel = registry.create(id, model);
            REQUIRE(panel != nullptr);
            CHECK_FALSE(panel->isPlaceholder());
        }
    }
    for (const auto* id : {"LimitPreset", "EQSuggestions", "SPLHistory", "Dose", "Events"}) {
        INFO(id);
       #if FOH_SHOW_UNFINISHED
        auto panel = registry.create(id, model);
        REQUIRE(panel != nullptr);
        CHECK(panel->isPlaceholder());
       #else
        CHECK(registry.find(id) == nullptr);
       #endif
    }
}

TEST_CASE("old workspaces silently skip unregistered docked and floating panels", "[app][workspace][files]") {
    TemporaryWorkspaces temporary;
    REQUIRE(temporary.folder.createDirectory().wasOk());
    auto legacy = fohws::builtInPresets()[0];
    legacy.root = fohws::H({
        {0.25, fohws::L("LimitPreset")},
        {0.25, fohws::LayoutNode::tabs({"EQSuggestions", "RTA", "SPLHistory"}, 1)},
        {0.25, fohws::V({{0.5, fohws::L("Dose")}, {0.5, fohws::L("FuturePanel")}})},
        {0.25, fohws::L("Inspector")}
    });
    legacy.floating = {{"Events", {40, 65, 500, 300}, "main"},
                       {"FutureFloatingPanel", {40, 65, 500, 300}, "main"},
                       {"SignalMeters", {40, 65, 500, 300}, "main"}};
    auto file = temporary.folder.getChildFile("setup.json");
    REQUIRE(file.replaceWithText(juce::String(fohws::toJson(legacy).dump())));
    WorkspaceManager loaded(temporary.folder);
    REQUIRE(loaded.loadErrors().isEmpty());
    const auto& workspace = loaded.at(0);
   #if FOH_SHOW_UNFINISHED
    CHECK(panels(workspace) == std::vector<std::string>{"Dose", "EQSuggestions", "Events", "Inspector", "LimitPreset", "RTA", "SPLHistory", "SignalMeters"});
   #else
    CHECK(panels(workspace) == std::vector<std::string>{"Inspector", "RTA", "SignalMeters"});
    REQUIRE(workspace.root.kind == fohws::LayoutNode::Split);
    REQUIRE(workspace.root.children.size() == 2);
    CHECK(workspace.root.children[0] == fohws::L("RTA"));
    CHECK(workspace.root.sizes == std::vector<double>{0.5, 0.5});
    REQUIRE(workspace.floating.size() == 1);
    CHECK(workspace.floating.front().panel == "SignalMeters");
   #endif
    CHECK_FALSE(workspace.root.contains("FuturePanel"));
    loaded.saveAll();
    WorkspaceManager relaunched(temporary.folder);
    CHECK(relaunched.loadErrors().isEmpty());
    CHECK(panels(relaunched.at(0)) == panels(workspace));

    // An unavailable root is a valid empty workspace, ready to dock a panel.
    legacy.root = fohws::L("FuturePanel"); legacy.floating.clear();
    REQUIRE(temporary.folder.getChildFile("setup.current.json").replaceWithText(
        juce::String(fohws::toJson(legacy).dump())));
    WorkspaceManager empty(temporary.folder);
    CHECK(empty.loadErrors().isEmpty());
    CHECK(empty.at(0).root == fohws::L(""));
}

TEST_CASE("pruning unknown tabs preserves the selected surviving panel", "[app][workspace][files]") {
    TemporaryWorkspaces temporary;
    REQUIRE(temporary.folder.createDirectory().wasOk());
    auto legacy = fohws::builtInPresets()[0];
    legacy.root = fohws::LayoutNode::tabs({"FuturePanel", "Inspector", "RTA"}, 2);
    REQUIRE(temporary.folder.getChildFile("setup.json").replaceWithText(
        juce::String(fohws::toJson(legacy).dump())));
    WorkspaceManager loaded(temporary.folder);
    CHECK(loaded.loadErrors().isEmpty());
    CHECK(loaded.at(0).root == fohws::LayoutNode::tabs({"Inspector", "RTA"}, 1));
}

TEST_CASE("finished presets render in both themes and UI scales", "[app][workspace][layout]") {
    juce::ScopedJuceInitialiser_GUI initialiser;
    const auto originalTheme = foh::theme::ThemeManager::get().current().id;
    AppModel model; model.analysis.stop(); model.dm.removeAudioCallback(&model.engine);
    auto presets = fohws::builtInPresets();
    for (bool sunlight : {false, true}) {
        foh::theme::ThemeManager::get().setTheme(sunlight ? "apple-pro-sunlight" : "apple-pro-dark");
        FohLookAndFeel look;
        for (int scale : {100, 200}) for (auto& workspace : presets) {
            INFO(workspace.id << " scale " << scale << " sunlight " << sunlight);
            WorkspaceView view(model, PanelRegistry::standard());
            view.setLookAndFeel(&look); view.setSize(1200, 800); view.setWorkspace(&workspace);
            for (const auto& id : panels(workspace)) {
                auto* panel = view.panel(id);
                REQUIRE(panel != nullptr);
                CHECK_FALSE(panel->isPlaceholder());
            }
            auto image = view.createComponentSnapshot(view.getLocalBounds(), true, scale / 100.f);
            CHECK(image.getWidth() == 1200 * scale / 100);
            CHECK(image.getHeight() == 800 * scale / 100);
            if (const auto* folder = std::getenv("FOH_WORKSPACE_QA_DIR")) {
                juce::File dir(folder); REQUIRE(dir.createDirectory().wasOk());
                auto file = dir.getChildFile(juce::String(workspace.id) + (sunlight ? "-sunlight-" : "-dark-") + juce::String(scale) + ".png");
                auto stream = file.createOutputStream(); REQUIRE(stream != nullptr);
                stream->setPosition(0);
                juce::PNGImageFormat png; REQUIRE(png.writeImageToStream(image, *stream)); stream->truncate();
            }
            view.setWorkspace(nullptr); view.setLookAndFeel(nullptr);
        }
    }
    foh::theme::ThemeManager::get().setTheme(originalTheme);
}
