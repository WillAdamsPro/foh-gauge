#include <catch2/catch_test_macros.hpp>
#include <fohcore/rig_profile.h>
#include <stdexcept>
using namespace foh;
namespace {
Json legacy(int clusters = 3, int cabinets = 3) {
    auto j = Json::object(); j.set("clusters", clusters); j.set("cabinetsPerCluster", cabinets);
    j.set("control", "grouped"); j.set("frontFills", true); j.set("subs", true); j.set("subArrangement", "leftRight"); return j;
}
}
TEST_CASE("Asymmetric clusters and flexible auxiliary counts drive steps routes and guidance", "[rig]") {
    for (int fills : {0, 1, 12, 256}) for (int subs : {0, 1, 16, 64, 256}) {
        RigProfile rig; rig.clusterCabinets = {2, 3, 3}; rig.frontFillCount = fills; rig.subCount = subs;
        REQUIRE(rig.valid()); CHECK(rig.mainCabinetCount() == 8); CHECK(rig.mainRoutes() == 3);
        CHECK(rig.fillRoutes() == fills); CHECK(rig.subRoutes() == (subs ? 1 : 0));
        CHECK(rig.routingGuidance().find("L=2, C=3, R=3") != std::string::npos);
        auto steps = rigSteps(rig); CHECK(steps.size() == 1 + (fills > 0) + (subs > 0));
        CHECK(rig.guideGuidance(2).find(fills ? std::to_string(fills) + " front fill" : "No front fills") != std::string::npos);
        CHECK(rig.guideGuidance(1).find(subs ? std::to_string(subs) + " sub" : "No subs") != std::string::npos);
        CHECK(RigProfile::fromJson(Json::parse(rig.toJson().dump())).toJson() == rig.toJson());
        rig.control = RigProfile::Control::Individual; CHECK(rig.mainRoutes() == 8);
        rig.fillControl = RigProfile::Control::Grouped; rig.fillGroups = std::max(1, fills / 2);
        rig.subControl = RigProfile::Control::Individual;
        CHECK(rig.fillRoutes() == (fills ? rig.fillGroups : 0)); CHECK(rig.subRoutes() == subs);
        CHECK(RigProfile::fromJson(rig.toJson()).toJson() == rig.toJson());
    }
    CHECK(rigSteps(std::nullopt) == std::vector<RigStep>{RigStep::ReferenceDelay, RigStep::SubToMains, RigStep::FillAlignment});
}
TEST_CASE("Sub arrangements placement and groups tailor alignment", "[rig]") {
    RigProfile rig; rig.subCount = 24; rig.subGroups = 4; rig.subPlacement = RigProfile::SubPlacement::Flown;
    for (auto a : {RigProfile::SubArrangement::Centre, RigProfile::SubArrangement::LeftRight,
                  RigProfile::SubArrangement::Distributed, RigProfile::SubArrangement::Other,
                  RigProfile::SubArrangement::Cardioid, RigProfile::SubArrangement::EndFire}) {
        rig.subArrangement = a;
        CHECK(RigProfile::fromJson(rig.toJson()).toJson() == rig.toJson());
        CHECK(rig.guideGuidance(1).find("24 subs in 4 control groups") != std::string::npos);
        CHECK(rig.guideGuidance(1).find("flown") != std::string::npos);
        if (a == RigProfile::SubArrangement::Cardioid || a == RigProfile::SubArrangement::EndFire) {
            CHECK(rig.guideGuidance(1).find("internal delays and polarity intact") != std::string::npos);
            rig.subControl = RigProfile::Control::Individual;
            CHECK(rig.guideGuidance(1).find("all array members together") != std::string::npos);
            rig.subControl = RigProfile::Control::Grouped;
        }
    }
}
TEST_CASE("Legacy shared cabinet counts migrate without guessing auxiliary quantities", "[rig][migration]") {
    for (int n : {1, 3, 256}) {
        auto j = legacy(n, 4); auto rig = RigProfile::fromJson(j);
        CHECK(rig.clusterCabinets == std::vector<int>((size_t)n, 4));
        CHECK(rig.frontFillCount == 1); CHECK(rig.subCount == 1);
        CHECK(rigSteps(rig).size() == 3);
        CHECK(RigProfile::fromJson(rig.toJson()).toJson() == rig.toJson());
        j.set("frontFills", false); j.set("subs", false); rig = RigProfile::fromJson(j);
        CHECK(rig.frontFillCount == 0); CHECK(rig.subCount == 0); CHECK(rigSteps(rig).size() == 1);
    }
    for (const char* key : {"clusters", "cabinetsPerCluster", "control", "frontFills", "subs", "subArrangement"}) {
        auto j = legacy(), partial = Json::object();
        for (const auto& m : j.members()) if (m.first != key) partial.set(m.first, m.second);
        CHECK_THROWS(RigProfile::fromJson(partial));
    }
    for (const char* key : {"clusters", "cabinetsPerCluster"}) for (auto n : {Json(0), Json(-1), Json(257), Json(1.5), Json("3")}) {
        auto j = legacy(); j.set(key, n); CHECK_THROWS(RigProfile::fromJson(j));
    }
}
TEST_CASE("Rig JSON rejects missing malformed and unsafe answers", "[rig][regression]") {
    auto full = RigProfile{}.toJson();
    for (const auto& m : full.members()) {
        auto partial = Json::object();
        for (const auto& other : full.members()) if (other.first != m.first) partial.set(other.first, other.second);
        CAPTURE(m.first); CHECK_THROWS(RigProfile::fromJson(partial));
    }
    for (const char* key : {"frontFillCount", "subCount", "fillGroups", "subGroups"}) {
        for (auto n : {Json(-1), Json(257), Json(1e100), Json(1.5), Json("2"), Json(true), Json(std::numeric_limits<double>::infinity())}) {
            auto j = full; j.set(key, n); CHECK_THROWS(RigProfile::fromJson(j));
        }
    }
    for (const char* key : {"control", "fillControl", "subControl", "subArrangement", "subPlacement"}) {
        auto j = full; j.set(key, "unknown"); CHECK_THROWS(RigProfile::fromJson(j));
    }
    for (auto array : {Json::array(), Json(2)}) {
        auto j = full; j.set("clusterCabinets", array); CHECK_THROWS(RigProfile::fromJson(j));
    }
    for (auto n : {Json(0), Json(-1), Json(257), Json(1.5), Json("2"), Json(true)}) {
        auto array = Json::array(); array.push(n); auto j = full; j.set("clusterCabinets", array); CHECK_THROWS(RigProfile::fromJson(j));
    }
    auto j = full; auto array = Json::array(); for (int i = 0; i < 257; ++i) array.push(1);
    j.set("clusterCabinets", array); CHECK_THROWS(RigProfile::fromJson(j));
    j = full; j.set("fillGroups", 2); CHECK_THROWS(RigProfile::fromJson(j));
    j = full; j.set("version", 3); CHECK_THROWS(RigProfile::fromJson(j));
    j = full; j.set("futureAnswer", Json::array()); CHECK(RigProfile::fromJson(j).toJson() == full);
    for (const char* text : {"", "{", "{broken}", "null", "[]", "\"\"", "{}"}) CHECK_THROWS(RigProfile::fromJson(Json::parse(text)));
}
TEST_CASE("Invalid programmatic profiles fall back to the default guides", "[rig]") {
    for (int n : {0, -1, 257, std::numeric_limits<int>::max()}) {
        RigProfile rig; rig.clusterCabinets = {n}; CHECK_FALSE(rig.valid()); CHECK_THROWS(rig.toJson()); CHECK(rigSteps(rig) == rigSteps(std::nullopt));
    }
    RigProfile rig; rig.clusterCabinets.assign(256, 256); REQUIRE(rig.valid()); CHECK(rig.mainCabinetCount() == 65536);
    rig.clusterCabinets.push_back(1); CHECK_FALSE(rig.valid());
    rig = {}; rig.subPlacement = static_cast<RigProfile::SubPlacement>(2); CHECK_FALSE(rig.valid());
    rig = {}; rig.subArrangement = static_cast<RigProfile::SubArrangement>(6); CHECK_FALSE(rig.valid());
    rig = {}; rig.fillControl = static_cast<RigProfile::Control>(2); CHECK_FALSE(rig.valid());
}
