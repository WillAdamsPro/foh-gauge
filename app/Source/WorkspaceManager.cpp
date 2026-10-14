#include "WorkspaceManager.h"
#include <fohcore/input.h>
using namespace juce;
using namespace fohws;

File WorkspaceManager::defaultFolder() {
   #if JUCE_MAC
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Application Support/FOH Gauge/workspaces");
   #else
    return File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("FOH Gauge/workspaces");
   #endif
}

bool WorkspaceManager::readFile(const File& f, Workspace& w) const {
    if (!f.existsAsFile()) return false;
    try {
        std::string err;
        auto json = Json::parse(foh::input::read(f.getFullPathName().toStdString()), &err);
        Workspace tmp;
        if (!err.empty() || !fromJson(json, tmp, err)) throw std::runtime_error(err);
        w = std::move(tmp); return true;
    } catch (const std::exception& e) {
        errors.add(f.getFileName() + ": " + String(e.what()));
        return false;
    }
}
void WorkspaceManager::writeFile(const File& f, const Workspace& w) const {
    dir.createDirectory(); auto tmp = f.getSiblingFile(f.getFileName() + ".tmp");
    if (tmp.replaceWithText(String(toJson(w).dump())) ) tmp.moveFileTo(f); // write-then-rename: never leave half a file
}

WorkspaceManager::WorkspaceManager(File folder) : dir(std::move(folder)) {
    presets = builtInPresets();
    for (auto& p : presets) { Workspace s = p; readFile(dir.getChildFile(p.id + ".json"), s); s.builtIn = true; s.id = p.id; saved.push_back(s); }
    for (auto& f : dir.findChildFiles(File::findFiles, false, "custom-*.json")) {
        if (f.getFileName().contains(".current.")) continue; Workspace w; if (readFile(f, w)) { w.builtIn = false; saved.push_back(w); } }
    for (auto& s : saved) { Workspace live = s; Workspace cur; if (readFile(dir.getChildFile(s.id + ".current.json"), cur)) { cur.id = s.id; cur.name = s.name; cur.builtIn = s.builtIn; live = cur; } list.push_back(live); }
}
int WorkspaceManager::indexOf(const std::string& id) const { for (size_t i = 0; i < list.size(); ++i) if (list[i].id == id) return (int) i; return -1; }
const Workspace* WorkspaceManager::defaultFor(const std::string& id) const { for (auto& p : presets) if (p.id == id) return &p; return nullptr; }

std::string WorkspaceManager::layoutKey(const Workspace& w) {
    Json j = toJson(w); Json k = Json::object(); k.set("root", j["root"]); k.set("floating", j["floating"]); return k.dump(0);
}
bool WorkspaceManager::isModified(int i) const { return layoutKey(list[(size_t) i]) != layoutKey(saved[(size_t) i]); }

void WorkspaceManager::saveCurrent(int i) const { writeFile(dir.getChildFile(list[(size_t) i].id + ".current.json"), list[(size_t) i]); }
void WorkspaceManager::save(int i) { saved[(size_t) i] = list[(size_t) i]; writeFile(dir.getChildFile(list[(size_t) i].id + ".json"), list[(size_t) i]); saveCurrent(i); }
void WorkspaceManager::revert(int i) { auto state = list[(size_t) i].panelState; list[(size_t) i] = saved[(size_t) i]; list[(size_t) i].panelState = state; saveCurrent(i); }
void WorkspaceManager::resetToDefault(int i) {
    auto* p = defaultFor(list[(size_t) i].id); if (!p) { revert(i); return; }
    auto state = list[(size_t) i].panelState; saved[(size_t) i] = *p; list[(size_t) i] = *p; list[(size_t) i].panelState = state;
    dir.getChildFile(p->id + ".json").deleteFile(); saveCurrent(i);
}
int WorkspaceManager::saveAs(int i, const String& name) {
    Workspace w = list[(size_t) i]; w.builtIn = false; w.name = name.toStdString();
    String slug = name.toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789- ").trim().replaceCharacter(' ', '-'); if (slug.isEmpty()) slug = "workspace";
    std::string id = ("custom-" + slug).toStdString(); for (int n = 2; indexOf(id) >= 0; ++n) id = ("custom-" + slug + "-" + String(n)).toStdString();
    w.id = id; list.push_back(w); saved.push_back(w); int idx = count() - 1; save(idx); return idx;
}
bool WorkspaceManager::rename(int i, const String& name) {
    if (list[(size_t) i].builtIn || name.trim().isEmpty()) return false;
    list[(size_t) i].name = saved[(size_t) i].name = name.trim().toStdString();
    writeFile(dir.getChildFile(list[(size_t) i].id + ".json"), saved[(size_t) i]); saveCurrent(i); return true;
}
bool WorkspaceManager::remove(int i) {
    if (list[(size_t) i].builtIn) return false; auto id = list[(size_t) i].id;
    dir.getChildFile(id + ".json").deleteFile(); dir.getChildFile(id + ".current.json").deleteFile();
    list.erase(list.begin() + i); saved.erase(saved.begin() + i); return true;
}
