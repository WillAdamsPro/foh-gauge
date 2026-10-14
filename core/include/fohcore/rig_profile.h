#pragma once
#include "json.h"
#include <optional>
#include <vector>

namespace foh {
// Pure session data. Questionnaire and JSON APIs are message/worker-thread only.
struct RigProfile {
    enum class Control { Grouped, Individual };
    enum class SubArrangement { Centre, LeftRight, Distributed, Other, Cardioid, EndFire };
    enum class SubPlacement { Ground, Flown };
    std::vector<int> clusterCabinets {1, 1};
    Control control = Control::Grouped;
    int frontFillCount = 0, subCount = 0;
    Control fillControl = Control::Individual, subControl = Control::Grouped;
    // Retain the grouped answer when switched to individual control; absent sections store 1.
    int fillGroups = 1, subGroups = 1;
    SubArrangement subArrangement = SubArrangement::Centre;
    SubPlacement subPlacement = SubPlacement::Ground;
    int mainCabinetCount() const;
    int mainRoutes() const;
    int fillRoutes() const;
    int subRoutes() const;
    std::string clusterName(size_t index) const;
    std::string mainSummary() const;
    std::string routingGuidance() const;
    std::string guideGuidance(int guide) const; // W1-W6, zero based
    bool valid() const;
    Json toJson() const;
    // Versionless rc5 answers expand the shared count; true fills/subs migrate to one.
    // Throws on malformed data; session loading discards invalid optional answers.
    static RigProfile fromJson(const Json&);
};
enum class RigStep { ReferenceDelay = 1, SubToMains = 2, FillAlignment = 3 };
std::vector<RigStep> rigSteps(const std::optional<RigProfile>& profile);
}
