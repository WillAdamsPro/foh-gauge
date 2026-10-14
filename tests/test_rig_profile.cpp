#include <catch2/catch_test_macros.hpp>
#include <fohcore/rig_profile.h>

using namespace foh;
TEST_CASE("Three sample rigs map to existing W step lists", "[rig]") {
    RigProfile rig;
    SECTION("Single main cabinet, grouped, no fills or subs") {
        rig.clusters = 1; rig.cabinetsPerCluster = 1;
        CHECK(rigSteps(rig) == std::vector<RigStep>{RigStep::ReferenceDelay});
    }
    SECTION("Two clusters of three cabinets, individual control, left-right subs") {
        rig.clusters = 2; rig.cabinetsPerCluster = 3; rig.control = RigProfile::Control::Individual;
        rig.subs = true; rig.subArrangement = RigProfile::SubArrangement::LeftRight;
        CHECK(rigSteps(rig) == std::vector<RigStep>{RigStep::ReferenceDelay, RigStep::SubToMains});
    }
    SECTION("Four grouped clusters, front fills and distributed subs") {
        rig.clusters = 4; rig.cabinetsPerCluster = 2; rig.frontFills = true; rig.subs = true;
        rig.subArrangement = RigProfile::SubArrangement::Distributed;
        CHECK(rigSteps(rig) == std::vector<RigStep>{RigStep::ReferenceDelay, RigStep::SubToMains, RigStep::FillAlignment});
    }
    CHECK(RigProfile::fromJson(Json::parse(rig.toJson().dump())).toJson() == rig.toJson());
}
TEST_CASE("Skipped questionnaire preserves default steps, fills need no subs", "[rig]") {
    CHECK(rigSteps(std::nullopt) == std::vector<RigStep>{RigStep::ReferenceDelay, RigStep::SubToMains, RigStep::FillAlignment});
    RigProfile rig; rig.frontFills = true;
    CHECK(rigSteps(rig) == std::vector<RigStep>{RigStep::ReferenceDelay, RigStep::FillAlignment});
    for (auto arrangement : {RigProfile::SubArrangement::Centre, RigProfile::SubArrangement::LeftRight,
                            RigProfile::SubArrangement::Distributed, RigProfile::SubArrangement::Other}) {
        rig.subArrangement = arrangement;
        CHECK(RigProfile::fromJson(rig.toJson()).toJson() == rig.toJson());
    }
}
TEST_CASE("Rig JSON rejects malformed answers", "[rig]") {
    RigProfile rig; auto j = rig.toJson();
    SECTION("zero clusters") { j.set("clusters", 0); }
    SECTION("fractional cabinets") { j.set("cabinetsPerCluster", 1.5); }
    SECTION("oversized count") { j.set("clusters", 257); }
    SECTION("wrong count type") { j.set("clusters", "2"); }
    SECTION("wrong flag type") { j.set("subs", "yes"); }
    SECTION("missing data") { j = Json::object(); }
    SECTION("wrong control") { j.set("control", "unknown"); }
    SECTION("wrong arrangement") { j.set("subArrangement", "unknown"); }
    SECTION("wrong object type") { j = Json::array(); }
    CHECK_THROWS(RigProfile::fromJson(j));
}
