#pragma once
#include "json.h"
#include <optional>
#include <vector>

namespace foh {
// Pure session data. Questionnaire and JSON APIs are message/worker-thread only.
struct RigProfile {
    enum class Control { Grouped, Individual };
    enum class SubArrangement { Centre, LeftRight, Distributed, Other };
    int clusters = 2;
    int cabinetsPerCluster = 1;
    Control control = Control::Grouped;
    bool frontFills = false;
    bool subs = false;
    SubArrangement subArrangement = SubArrangement::Centre;
    bool valid() const;
    Json toJson() const;
    // Throws on malformed data; session loading discards invalid optional answers.
    // Unknown keys are ignored for compatibility with later questionnaire versions.
    static RigProfile fromJson(const Json&);
};
enum class RigStep { ReferenceDelay = 1, SubToMains = 2, FillAlignment = 3 };
// No profile (including Skip) preserves the full default guide flow. Only existing
// W steps are filtered: cluster/control/arrangement answers describe the rig but
// do not invent per-box or array-alignment workflows.
std::vector<RigStep> rigSteps(const std::optional<RigProfile>& profile);
}
