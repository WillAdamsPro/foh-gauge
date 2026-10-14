// WorkspaceManager: the four presets plus custom workspaces, persisted as JSON in
//   macOS:  ~/Library/Application Support/FOH Gauge/workspaces/
//   Linux:  ~/.config/FOH Gauge/workspaces/     Windows: %APPDATA%\FOH Gauge\workspaces\
// <id>.json        = the saved layout (Save Workspace / Save As). Missing for a preset = its spec default.
// <id>.current.json = the live layout, written on switch and quit so a restart comes back as you left it.
// "Modified" (the switcher dot) = live tree + floating windows differ from the saved layout.
#pragma once
#include "LayoutModel.h"
#include <juce_core/juce_core.h>

class WorkspaceManager {
public:
    explicit WorkspaceManager(juce::File folder = defaultFolder());
    static juce::File defaultFolder();
    juce::File folder() const { return dir; }

    const juce::StringArray& loadErrors() const { return errors; }
    int count() const { return (int) list.size(); }
    fohws::Workspace& at(int i) { return list[(size_t) i]; }
    int indexOf(const std::string& id) const;
    bool isModified(int i) const;

    void saveCurrent(int i) const;                // live layout -> <id>.current.json
    void saveAll() const { for (int i = 0; i < count(); ++i) saveCurrent(i); }
    void save(int i);                             // live layout becomes the saved layout
    void revert(int i);                           // back to the saved layout (preset default if never saved)
    void resetToDefault(int i);                   // presets only: drop the saved layout too
    int saveAs(int i, const juce::String& name);  // new custom workspace; returns its index
    bool rename(int i, const juce::String& name); // custom only
    bool remove(int i);                           // custom only
    const fohws::Workspace* defaultFor(const std::string& id) const;

private:
    static std::string layoutKey(const fohws::Workspace& w); // root + floating, excluding panel state
    bool readFile(const juce::File& f, fohws::Workspace& w) const;
    void writeFile(const juce::File& f, const fohws::Workspace& w) const;
    mutable juce::StringArray errors;
    juce::File dir; std::vector<fohws::Workspace> list, saved, presets;
};
