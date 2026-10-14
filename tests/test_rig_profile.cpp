#include <catch2/catch_test_macros.hpp>
#include <fohcore/rig_profile.h>
#include <stdexcept>

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

TEST_CASE("Rig count bounds and flag types reject unsafe numeric answers", "[rig][regression]") {
    for (const char* key : {"clusters", "cabinetsPerCluster", "frontFills", "subs"}) {
        for (double value : {0.0, -1.0, 257.0, 1e100, 1.5,
                             std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
            INFO(key << " = " << value);
            auto j = RigProfile{}.toJson(); j.set(key, value);
            CHECK_THROWS_AS(RigProfile::fromJson(j), std::runtime_error);
        }
    }
    for (const char* key : {"clusters", "cabinetsPerCluster", "control", "frontFills", "subs", "subArrangement"}) {
        auto full = RigProfile{}.toJson(), partial = Json::object();
        for (const auto& member : full.members()) if (member.first != key) partial.set(member.first, member.second);
        INFO("missing " << key);
        CHECK_THROWS_AS(RigProfile::fromJson(partial), std::runtime_error);
    }
    for (int count : {0, -1, 257, std::numeric_limits<int>::max()}) {
        for (bool cluster : {false, true}) {
            RigProfile rig;
            (cluster ? rig.clusters : rig.cabinetsPerCluster) = count;
            CHECK_FALSE(rig.valid()); CHECK_THROWS(rig.toJson());
            CHECK(rigSteps(rig) == rigSteps(std::nullopt));
        }
    }
}

TEST_CASE("Empty truncated and malformed rig JSON cannot become valid answers", "[rig][regression]") {
    for (const char* text : {"", "{", "{\"clusters\":2,", "{broken}", "null", "[]", "\"\"", "{}",
                             "{\"clusters\":1,\"clusters\":2}"}) {
        INFO(text);
        std::string error;
        auto j = Json::parse(text, &error);
        CHECK_THROWS_AS(RigProfile::fromJson(j), std::runtime_error);
    }
}

TEST_CASE("Rig boundary answers and extra keys preserve the supported profile", "[rig]") {
    for (int count : {1, 256}) {
        RigProfile rig; rig.clusters = rig.cabinetsPerCluster = count;
        auto j = rig.toJson(); j.set("futureAnswer", Json::array()); j.set("subCount", 1e100);
        CHECK(RigProfile::fromJson(Json::parse(j.dump())).toJson() == rig.toJson());
    }
    for (auto control : {static_cast<RigProfile::Control>(-1), static_cast<RigProfile::Control>(2)}) {
        RigProfile rig; rig.control = control;
        CHECK_FALSE(rig.valid()); CHECK_THROWS(rig.toJson()); CHECK(rigSteps(rig) == rigSteps(std::nullopt));
    }
    for (auto arrangement : {static_cast<RigProfile::SubArrangement>(-1), static_cast<RigProfile::SubArrangement>(4)}) {
        RigProfile rig; rig.subArrangement = arrangement;
        CHECK_FALSE(rig.valid()); CHECK_THROWS(rig.toJson()); CHECK(rigSteps(rig) == rigSteps(std::nullopt));
    }
}
