#include "fohcore/rig_profile.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace foh {
namespace {
bool validControl(RigProfile::Control c) { return c == RigProfile::Control::Grouped || c == RigProfile::Control::Individual; }
int count(const Json& j, int low = 0) {
    double n = j.asNumber(-1);
    if (!std::isfinite(n) || n < low || n > 256 || std::floor(n) != n)
        throw std::runtime_error("Invalid rig count");
    return static_cast<int>(n);
}
RigProfile::Control readControl(const Json& j) {
    auto c = j.asString();
    if (c == "individual") return RigProfile::Control::Individual;
    if (c == "grouped") return RigProfile::Control::Grouped;
    throw std::runtime_error("Invalid rig control");
}
const char* controlName(RigProfile::Control c) { return c == RigProfile::Control::Individual ? "individual" : "grouped"; }
const char* arrangements[] = {"centre", "leftRight", "distributed", "other", "cardioid", "endFire"};
std::string units(int count, const char* one, const char* many) { return std::to_string(count) + " " + (count == 1 ? one : many); }
}
bool RigProfile::valid() const {
    return !clusterCabinets.empty() && clusterCabinets.size() <= 256
        && std::all_of(clusterCabinets.begin(), clusterCabinets.end(), [](int n) { return n >= 1 && n <= 256; })
        && validControl(control) && validControl(fillControl) && validControl(subControl)
        && frontFillCount >= 0 && frontFillCount <= 256 && subCount >= 0 && subCount <= 256
        && fillGroups >= 1 && fillGroups <= std::max(1, frontFillCount)
        && subGroups >= 1 && subGroups <= std::max(1, subCount)
        && subArrangement >= SubArrangement::Centre && subArrangement <= SubArrangement::EndFire
        && (subPlacement == SubPlacement::Ground || subPlacement == SubPlacement::Flown);
}
int RigProfile::mainCabinetCount() const { return std::accumulate(clusterCabinets.begin(), clusterCabinets.end(), 0); }
int RigProfile::mainRoutes() const { return control == Control::Individual ? mainCabinetCount() : (int)clusterCabinets.size(); }
int RigProfile::fillRoutes() const { return frontFillCount == 0 ? 0 : fillControl == Control::Individual ? frontFillCount : fillGroups; }
int RigProfile::subRoutes() const { return subCount == 0 ? 0 : subControl == Control::Individual ? subCount : subGroups; }
std::string RigProfile::clusterName(size_t i) const {
    if (clusterCabinets.size() == 2) return i == 0 ? "L" : "R";
    if (clusterCabinets.size() == 3) return i == 0 ? "L" : i == 1 ? "C" : "R";
    return "Cluster " + std::to_string(i + 1);
}
std::string RigProfile::mainSummary() const {
    std::string s = "Mains: ";
    for (size_t i = 0; i < clusterCabinets.size(); ++i) {
        if (i) s += ", ";
        s += clusterName(i) + "=" + std::to_string(clusterCabinets[i]);
    }
    s += " (" + units(mainRoutes(), "control route", "control routes") + "). ";
    return s;
}
std::string RigProfile::routingGuidance() const {
    std::string s = mainSummary();
    s += units(frontFillCount, "fill", "fills") + " / " + units(fillRoutes(), "route", "routes") + "; ";
    s += units(subCount, "sub", "subs") + " / " + units(subRoutes(), "route", "routes") + ". ";
    s += "Send the PA output to the console/DSP; start with only the first main route unmuted. Select each remaining route there and keep the reference loopback connected.";
    return s;
}
std::string RigProfile::guideGuidance(int guide) const {
    if (!valid()) return {};
    if (guide == 0 || guide == 5) {
        return routingGuidance() + (guide == 0
            ? " Measure on-axis in each cluster's coverage. For arrays, use a representative listening distance where the cabinets combine; repeat reference delay at every new mic position."
            : " Check all " + units(mainCabinetCount() + frontFillCount + subCount, "cabinet", "cabinets") + " individually; label each measurement by cluster and cabinet or fill/sub number.");
    }
    if (guide == 1) {
        if (!subCount) return "No subs in this rig; continue with mains" + std::string(frontFillCount ? " and fills." : ".");
        std::string s = mainSummary() + units(subCount, "sub", "subs") + " in " + units(subRoutes(), "control group", "control groups") + ". ";
        s += subPlacement == SubPlacement::Flown ? "Use listening positions in the flown subs' coverage. " : "Use listening positions in the ground subs' coverage. ";
        switch (subArrangement) {
            case SubArrangement::LeftRight: s += "Check both L/R crossover regions. "; break;
            case SubArrangement::Centre: s += "Start at the centre crossover region, then check off-centre seats. "; break;
            case SubArrangement::Cardioid: case SubArrangement::EndFire:
                return s + "Keep the designed array's internal delays and polarity intact; align the complete array to mains, then verify rear rejection. Measure all array members together, even with individual cabinet control. Repeat W2 for each separate array and check every main cluster's crossover region.";
            default: s += "Check each sub coverage region. "; break;
        }
        return s + (subRoutes() == 1 ? "Align this sub group once, then verify with every main cluster."
            : "Repeat W2 for each independently controlled sub group against the main cluster covering that area; mute unrelated groups.");
    }
    if (guide == 2) {
        if (!frontFillCount) return "No front fills in this rig; continue with mains" + std::string(subCount ? " and subs." : ".");
        return mainSummary() + units(frontFillCount, "front fill", "front fills") + " in " + units(fillRoutes(), "control group", "control groups") + ". "
            + (fillRoutes() == 1 ? "Align this fill group at its main/fill seam. " : "Repeat W3 at each fill group's main/fill seam; mute the other fill groups. ")
            + "For a shared group, check every fill's coverage before accepting one delay and level for the group.";
    }
    return mainSummary() + (guide == 4 ? "Check " : "Tune and verify ") + units((int)clusterCabinets.size(), "main cluster", "main clusters") + " (" + units(mainCabinetCount(), "cabinet", "cabinets") + ")"
        + (subCount ? ", then mains plus " + units(subCount, "sub", "subs") : "")
        + (frontFillCount ? ", then " + units(frontFillCount, "fill", "fills") : "")
        + ". Check each control group's coverage and overlaps, then the complete system at representative seats. Repeat Store / Apply / Verify for each change.";
}
Json RigProfile::toJson() const {
    if (!valid()) throw std::runtime_error("Invalid rig profile");
    auto j = Json::object(), cabinets = Json::array();
    for (int n : clusterCabinets) cabinets.push(n);
    j.set("version", 2); j.set("clusterCabinets", cabinets); j.set("control", controlName(control));
    j.set("frontFillCount", frontFillCount); j.set("fillControl", controlName(fillControl)); j.set("fillGroups", fillGroups);
    j.set("subCount", subCount); j.set("subControl", controlName(subControl)); j.set("subGroups", subGroups);
    j.set("subArrangement", arrangements[static_cast<int>(subArrangement)]);
    j.set("subPlacement", subPlacement == SubPlacement::Flown ? "flown" : "ground");
    return j;
}
RigProfile RigProfile::fromJson(const Json& j) {
    if (!j.isObject()) throw std::runtime_error("Invalid rig profile");
    RigProfile r;
    r.control = readControl(j["control"]);
    if (!j.has("version")) {
        if (j["frontFills"].type() != Json::Bool || j["subs"].type() != Json::Bool)
            throw std::runtime_error("Invalid legacy rig flags");
        r.clusterCabinets.assign(count(j["clusters"], 1), count(j["cabinetsPerCluster"], 1));
        r.frontFillCount = j["frontFills"].asBool() ? 1 : 0; r.subCount = j["subs"].asBool() ? 1 : 0;
    } else {
        if (j["version"].type() != Json::Number || j["version"].asNumber() != 2 || !j["clusterCabinets"].isArray()
            || j["clusterCabinets"].items().size() > 256) throw std::runtime_error("Invalid rig version or clusters");
        r.clusterCabinets.clear();
        for (const auto& n : j["clusterCabinets"].items()) r.clusterCabinets.push_back(count(n, 1));
        r.frontFillCount = count(j["frontFillCount"]); r.subCount = count(j["subCount"]);
        r.fillControl = readControl(j["fillControl"]); r.subControl = readControl(j["subControl"]);
        r.fillGroups = count(j["fillGroups"], 1); r.subGroups = count(j["subGroups"], 1);
        auto p = j["subPlacement"].asString();
        if (p != "flown" && p != "ground") throw std::runtime_error("Invalid sub placement");
        r.subPlacement = p == "flown" ? SubPlacement::Flown : SubPlacement::Ground;
    }
    auto a = j["subArrangement"].asString();
    auto it = std::find(std::begin(arrangements), std::end(arrangements), a);
    if (it == std::end(arrangements)) throw std::runtime_error("Invalid sub arrangement");
    r.subArrangement = static_cast<SubArrangement>(it - std::begin(arrangements));
    if (!r.valid()) throw std::runtime_error("Invalid rig profile");
    return r;
}
std::vector<RigStep> rigSteps(const std::optional<RigProfile>& profile) {
    std::vector<RigStep> steps {RigStep::ReferenceDelay};
    if (!profile || !profile->valid() || profile->subCount > 0) steps.push_back(RigStep::SubToMains);
    if (!profile || !profile->valid() || profile->frontFillCount > 0) steps.push_back(RigStep::FillAlignment);
    return steps;
}
}
