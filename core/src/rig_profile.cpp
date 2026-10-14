#include "fohcore/rig_profile.h"
#include <stdexcept>

namespace foh {
bool RigProfile::valid() const {
    return clusters >= 1 && clusters <= 256 && cabinetsPerCluster >= 1 && cabinetsPerCluster <= 256
        && (control == Control::Grouped || control == Control::Individual)
        && subArrangement >= SubArrangement::Centre && subArrangement <= SubArrangement::Other;
}
Json RigProfile::toJson() const {
    if (!valid()) throw std::runtime_error("Invalid rig profile");
    auto j = Json::object();
    j.set("clusters", clusters); j.set("cabinetsPerCluster", cabinetsPerCluster);
    j.set("control", control == Control::Individual ? "individual" : "grouped");
    j.set("frontFills", frontFills); j.set("subs", subs);
    const char* arrangements[] = {"centre", "leftRight", "distributed", "other"};
    j.set("subArrangement", arrangements[static_cast<int>(subArrangement)]);
    return j;
}
RigProfile RigProfile::fromJson(const Json& j) {
    RigProfile r;
    auto count = [&](const char* key) {
        double n = j[key].asNumber(0);
        if (!std::isfinite(n) || n < 1 || n > 256 || std::floor(n) != n)
            throw std::runtime_error("Invalid rig cabinet or cluster count");
        return static_cast<int>(n);
    };
    if (!j.isObject() || j["frontFills"].type() != Json::Bool || j["subs"].type() != Json::Bool)
        throw std::runtime_error("Invalid rig profile");
    r.clusters = count("clusters"); r.cabinetsPerCluster = count("cabinetsPerCluster");
    auto control = j["control"].asString();
    if (control != "individual" && control != "grouped") throw std::runtime_error("Invalid rig control");
    r.control = control == "individual" ? Control::Individual : Control::Grouped;
    r.frontFills = j["frontFills"].asBool(); r.subs = j["subs"].asBool();
    auto arrangement = j["subArrangement"].asString();
    if (arrangement == "centre") r.subArrangement = SubArrangement::Centre;
    else if (arrangement == "leftRight") r.subArrangement = SubArrangement::LeftRight;
    else if (arrangement == "distributed") r.subArrangement = SubArrangement::Distributed;
    else if (arrangement == "other") r.subArrangement = SubArrangement::Other;
    else throw std::runtime_error("Invalid sub arrangement");
    return r;
}
std::vector<RigStep> rigSteps(const std::optional<RigProfile>& profile) {
    std::vector<RigStep> steps {RigStep::ReferenceDelay};
    if (!profile || !profile->valid() || profile->subs) steps.push_back(RigStep::SubToMains);
    if (!profile || !profile->valid() || profile->frontFills) steps.push_back(RigStep::FillAlignment);
    return steps;
}
}
