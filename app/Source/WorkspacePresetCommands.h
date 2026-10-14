// Message-thread workspace shortcuts shared by the main and floating windows.
#pragma once
#include "LayoutModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class WorkspacePresetCommands : public juce::ApplicationCommandTarget {
public:
    static constexpr juce::CommandID firstPreset = 0x2000;

    explicit WorkspacePresetCommands(std::function<void(int)> select)
        : selectPreset(std::move(select)), presets(fohws::builtInPresets()) {
        manager.registerAllCommandsForTarget(this);
        manager.setFirstCommandTarget(this);
        manager.getKeyMappings()->resetToDefaultMappings();
    }

    juce::KeyPressMappingSet* keyMappings() { return manager.getKeyMappings(); }
    juce::ApplicationCommandManager& commandManager() { return manager; }

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override {
        for (int i = 0; i < (int) presets.size(); ++i) commands.add(firstPreset + i);
    }
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override {
        const int index = id - firstPreset;
        if (index < 0 || index >= (int) presets.size()) return;
        info.setInfo(presets[(size_t) index].name, "Restore this workspace's panels", "Workspace", 0);
        info.addDefaultKeypress('1' + index, juce::ModifierKeys::commandModifier);
    }
    bool perform(const InvocationInfo& info) override {
        const int index = info.commandID - firstPreset;
        if (index < 0 || index >= (int) presets.size()) return false;
        selectPreset(index);
        return true;
    }

private:
    std::function<void(int)> selectPreset;
    std::vector<fohws::Workspace> presets;
    juce::ApplicationCommandManager manager;
};
