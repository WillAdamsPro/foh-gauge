#include <catch2/catch_test_macros.hpp>
#include <fohcore/session.h>
#include <chrono>
#include <fstream>
#include <limits>

namespace {
struct Bundle {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("foh-synthetic-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".fohsession");
    ~Bundle() { std::error_code ec; std::filesystem::remove_all(path, ec); }
};
foh::Session synthetic() {
    foh::Session s; s.deviceType = "Synthetic"; s.inputDevice = "Test Input"; s.outputDevice = "Test Output";
    s.sampleRate = 96000; s.bufferSize = 256; s.inputChannels = "11"; s.outputChannels = "101";
    s.mic = 1; s.reference = 0; s.outputA = 2; s.outputB = 0;
    s.micCalName = "synthetic.txt"; s.micCalText = "20 1\n1000 0\n20000 -1\n"; s.micCalEnabled = true;
    s.splCal = foh::Json::object();
    foh::SplCal first, second; first.calibrate(-20, 94, -2.1); second.calibrate(-30, 114, 1.5);
    s.splCal->set("Synthetic\nTest Input\n1", first.toJson());
    s.splCal->set("Synthetic\nTest Input\n2", second.toJson());
    s.rigProfile = foh::RigProfile{}; s.rigProfile->clusters = 3; s.rigProfile->cabinetsPerCluster = 4;
    s.rigProfile->control = foh::RigProfile::Control::Individual; s.rigProfile->subs = true; s.rigProfile->frontFills = true;
    s.rigProfile->subArrangement = foh::RigProfile::SubArrangement::Distributed;
    s.targets = foh::Json::object(); s.targets->set("futureField", "preserved");
    s.workspace.set("id", "alignment"); s.measurement.set("averages", 16);
    foh::Trace t; t.name = "Synthetic capture / portable"; t.visible = false; t.refDelayMs = 1.2345678901234567;
    t.tf.sampleRate = 96000; t.tf.fftSize = 8; t.tf.averages = 16; t.tf.blankThreshold = 0.6;
    t.tf.freq = {0, 12000}; t.tf.magDb = {-0.12345678901234567, 2}; t.tf.phaseDeg = {1, -90}; t.tf.coherence = {0.8, 0.99}; t.tf.H = {{1, 0}, {0, -1}}; t.tf.blanked = {1, 0};
    s.captures.push_back(t); return s;
}
}
TEST_CASE("Session bundle round trips all data and copied assets", "[session]") {
    Bundle bundle; auto s = synthetic(); std::string error;
    REQUIRE(foh::saveSession(bundle.path, s, error));
    REQUIRE(std::filesystem::exists(bundle.path / "session.json"));
    REQUIRE(std::filesystem::exists(bundle.path / "mic-cal.txt"));
    REQUIRE(std::filesystem::exists(bundle.path / "captures/0.json"));
    REQUIRE(std::filesystem::exists(bundle.path / "targets/targets.json"));
    // Moving the bundle must not depend on its old path or the original calibration.
    auto moved = bundle.path; moved += ".fohsession"; std::filesystem::rename(bundle.path, moved); bundle.path = moved;
    foh::Session loaded; REQUIRE(foh::loadSession(bundle.path, loaded, error));
    REQUIRE(loaded.deviceType == s.deviceType); REQUIRE(loaded.inputDevice == s.inputDevice); REQUIRE(loaded.outputDevice == s.outputDevice);
    REQUIRE(loaded.sampleRate == s.sampleRate); REQUIRE(loaded.bufferSize == s.bufferSize);
    REQUIRE(loaded.inputChannels == s.inputChannels); REQUIRE(loaded.outputChannels == s.outputChannels);
    REQUIRE(loaded.mic == s.mic); REQUIRE(loaded.reference == s.reference); REQUIRE(loaded.outputA == s.outputA); REQUIRE(loaded.outputB == s.outputB);
    REQUIRE(loaded.micCalName == s.micCalName); REQUIRE(loaded.micCalText == s.micCalText); REQUIRE(loaded.micCalEnabled);
    REQUIRE(loaded.rigProfile); REQUIRE(loaded.rigProfile->toJson() == s.rigProfile->toJson());
    REQUIRE(loaded.splCal == s.splCal);
    REQUIRE(foh::SplCal::fromJson((*loaded.splCal)["Synthetic\nTest Input\n1"]).level(-20, -2.1) == 94);
    REQUIRE(foh::SplCal::fromJson((*loaded.splCal)["Synthetic\nTest Input\n2"]).level(-30, 1.5) == 114); REQUIRE(loaded.targets == s.targets); REQUIRE(loaded.workspace == s.workspace); REQUIRE(loaded.measurement == s.measurement);
    REQUIRE(loaded.captures.size() == 1); const auto& t = loaded.captures[0]; const auto& original = s.captures[0];
    REQUIRE(t.name == original.name); REQUIRE(t.visible == original.visible); REQUIRE(t.refDelayMs == original.refDelayMs);
    REQUIRE(t.tf.sampleRate == original.tf.sampleRate); REQUIRE(t.tf.fftSize == original.tf.fftSize); REQUIRE(t.tf.averages == original.tf.averages); REQUIRE(t.tf.blankThreshold == original.tf.blankThreshold);
    REQUIRE(t.tf.freq == original.tf.freq); REQUIRE(t.tf.magDb == original.tf.magDb); REQUIRE(t.tf.phaseDeg == original.tf.phaseDeg); REQUIRE(t.tf.coherence == original.tf.coherence); REQUIRE(t.tf.H == original.tf.H); REQUIRE(t.tf.blanked == original.tf.blanked);
    s.captures.clear(); s.micCalText.clear(); s.micCalEnabled = false; s.targets.reset(); s.splCal.reset(); s.rigProfile.reset();
    REQUIRE(foh::saveSession(bundle.path, s, error)); REQUIRE(foh::loadSession(bundle.path, loaded, error));
    REQUIRE_FALSE(loaded.rigProfile); REQUIRE(foh::rigSteps(loaded.rigProfile).size() == 3);
    REQUIRE(loaded.captures.empty()); REQUIRE(loaded.micCalText.empty()); REQUIRE_FALSE(loaded.targets); REQUIRE_FALSE(loaded.splCal);
    REQUIRE_FALSE(std::filesystem::exists(bundle.path / "captures/0.json"));
}
TEST_CASE("Session rejects damaged bundles without changing output", "[session]") {
    Bundle bundle; auto s = synthetic(); std::string error; REQUIRE(foh::saveSession(bundle.path, s, error));
    foh::Session out; out.inputDevice = "unchanged";
    SECTION("missing capture") { std::filesystem::remove(bundle.path / "captures/0.json"); }
    SECTION("unsupported version") { std::ofstream(bundle.path / "session.json") << "{\"version\":999}"; }
    SECTION("nonfinite manifest number") { std::ofstream(bundle.path / "session.json") << "{\"version\":1e999}"; }
    SECTION("malformed manifest") { std::ofstream(bundle.path / "session.json") << "{"; }
    SECTION("asset symlink") { std::filesystem::remove(bundle.path / "mic-cal.txt"); std::filesystem::create_symlink(bundle.path / "session.json", bundle.path / "mic-cal.txt"); }
    SECTION("mismatched capture arrays") { std::ofstream(bundle.path / "captures/0.json") << "{\"freq\":[1],\"magDb\":[],\"phaseDeg\":[],\"coherence\":[],\"real\":[],\"imag\":[],\"blanked\":[]}"; }
    REQUIRE_FALSE(foh::loadSession(bundle.path, out, error)); REQUIRE_FALSE(error.empty()); REQUIRE(out.inputDevice == "unchanged");
}
TEST_CASE("Failed session save preserves existing bundle", "[session]") {
    Bundle bundle; auto s = synthetic(); std::string error; REQUIRE(foh::saveSession(bundle.path, s, error));
    SECTION("too many captures") { s.captures.resize(foh::TraceStore::kMax + 1); }
    SECTION("invalid SPL type") { s.splCal = foh::Json::array(); }
    SECTION("invalid SPL value") { auto c = foh::Json::object(); c.set("offsetDb", 1000); c.set("sensitivityDb", 0); s.splCal->set("synthetic", c); }
    SECTION("nonfinite SPL value") { auto c = foh::Json::object(); c.set("offsetDb", std::numeric_limits<double>::infinity()); c.set("sensitivityDb", 0); s.splCal->set("synthetic", c); }
    SECTION("invalid rig answers") { s.rigProfile->clusters = 0; }
    REQUIRE_FALSE(foh::saveSession(bundle.path, s, error));
    foh::Session loaded; REQUIRE(foh::loadSession(bundle.path, loaded, error)); REQUIRE(loaded.captures.size() == 1);
}
TEST_CASE("Missing session devices request Setup fallback", "[session]") {
    auto s = synthetic();
    REQUIRE(foh::sessionDevicesAvailable(s, "Synthetic", {"Test Input"}, {"Test Output"}));
    REQUIRE_FALSE(foh::sessionDevicesAvailable(s, "Synthetic", {}, {"Test Output"}));
    REQUIRE_FALSE(foh::sessionDevicesAvailable(s, "Synthetic", {"Test Input"}, {}));
    REQUIRE_FALSE(foh::sessionDevicesAvailable(s, "Other", {"Test Input"}, {"Test Output"}));
}
TEST_CASE("Session retains undefined measurement bins as JSON null", "[session]") {
    Bundle bundle; auto s = synthetic(); s.captures[0].tf.magDb[0] = std::numeric_limits<double>::quiet_NaN();
    std::string error; REQUIRE(foh::saveSession(bundle.path, s, error)); foh::Session loaded;
    REQUIRE(foh::loadSession(bundle.path, loaded, error)); REQUIRE(std::isnan(loaded.captures[0].tf.magDb[0]));
}

TEST_CASE("Session asset corruption leaves the entire prior session intact", "[session][files]") {
    Bundle bundle; auto s = synthetic(); std::string error; REQUIRE(foh::saveSession(bundle.path, s, error));
    auto out = s;
    auto rewrite = [&](const std::string& file, auto mutate) {
        std::ifstream f(bundle.path / file); std::string text{std::istreambuf_iterator<char>(f), {}};
        auto j = foh::Json::parse(text); mutate(j); std::ofstream(bundle.path / file) << j.dump();
    };
    SECTION("invalid rig profile") { rewrite("session.json", [](auto& j) { auto r = j["rigProfile"]; r.set("clusters", -1); j.set("rigProfile", r); }); }
    SECTION("missing manifest") { std::filesystem::remove(bundle.path / "session.json"); }
    SECTION("empty manifest") { std::ofstream(bundle.path / "session.json"); }
    SECTION("huge manifest") { std::filesystem::resize_file(bundle.path / "session.json", 17 * 1024 * 1024); }
    SECTION("wrong encoding") { std::ofstream(bundle.path / "session.json", std::ios::binary) << "\xff\xfe{}"; }
    SECTION("empty mic cal") { std::ofstream(bundle.path / "mic-cal.txt"); }
    SECTION("bad mic row") { std::ofstream(bundle.path / "mic-cal.txt") << "20 0\n1000 1\n2000 nan\n"; }
    SECTION("missing mic calibration") { std::filesystem::remove(bundle.path / "mic-cal.txt"); }
    SECTION("huge mic calibration") { std::filesystem::resize_file(bundle.path / "mic-cal.txt", 17 * 1024 * 1024); }
    SECTION("empty target") { std::ofstream(bundle.path / "targets/targets.json"); }
    SECTION("huge target") { std::filesystem::resize_file(bundle.path / "targets/targets.json", 17 * 1024 * 1024); }
    SECTION("truncated SPL calibration") { std::ofstream(bundle.path / "session.json") << "{\"splCal\":{\"offsetDb\":"; }
    SECTION("missing target") { std::filesystem::remove(bundle.path / "targets/targets.json"); }
    SECTION("garbled target") { std::ofstream(bundle.path / "targets/targets.json") << "{x"; }
    SECTION("wrong target type") { std::ofstream(bundle.path / "targets/targets.json") << "[]"; }
    SECTION("truncated capture") { std::ofstream(bundle.path / "captures/0.json") << "{\"freq\":[0,"; }
    SECTION("bad capture frequencies") { rewrite("captures/0.json", [](auto& j) { auto a = foh::Json::array(); a.push(20); a.push(10); j.set("freq", a); }); }
    SECTION("duplicate capture frequencies") { rewrite("captures/0.json", [](auto& j) { auto a = foh::Json::array(); a.push(0); a.push(0); j.set("freq", a); }); }
    SECTION("null capture frequency") { rewrite("captures/0.json", [](auto& j) { auto a = foh::Json::array(); a.push(foh::Json()); a.push(100); j.set("freq", a); }); }
    SECTION("absurd capture magnitude") { rewrite("captures/0.json", [](auto& j) { auto a = foh::Json::array(); a.push(0); a.push(1000); j.set("magDb", a); }); }
    SECTION("negative capture frequency") { rewrite("captures/0.json", [](auto& j) { auto a = foh::Json::array(); a.push(-1); a.push(100); j.set("freq", a); }); }
    SECTION("nonnumeric capture") { rewrite("captures/0.json", [](auto& j) { j.set("sampleRate", "bad"); }); }
    SECTION("absurd measurement delay") { rewrite("session.json", [](auto& j) { auto m = j["measurement"]; m.set("refDelayMs", 1e100); j.set("measurement", m); }); }
    SECTION("wrong measurement type") { rewrite("session.json", [](auto& j) { j.set("measurement", "bad"); }); }
    SECTION("invalid SPL calibration") { rewrite("session.json", [](auto& j) { auto a = foh::Json::object(); auto c = foh::Json::object(); c.set("offsetDb", 1000); c.set("sensitivityDb", 0); a.set("synthetic", c); j.set("splCal", a); }); }
    REQUIRE_FALSE(foh::loadSession(bundle.path, out, error)); REQUIRE_FALSE(error.empty());
    REQUIRE(out.inputDevice == s.inputDevice); REQUIRE(out.micCalText == s.micCalText); REQUIRE(out.workspace == s.workspace);
    REQUIRE(out.rigProfile); REQUIRE(out.rigProfile->toJson() == s.rigProfile->toJson());
    REQUIRE(out.targets == s.targets); REQUIRE(out.captures[0].tf.freq == s.captures[0].tf.freq);
}

TEST_CASE("Session preserves disabled channel selections", "[session][files]") {
    Bundle bundle; auto s = synthetic(); s.mic = s.reference = s.outputA = s.outputB = -1;
    std::string error; REQUIRE(foh::saveSession(bundle.path, s, error)); foh::Session loaded;
    REQUIRE(foh::loadSession(bundle.path, loaded, error));
    REQUIRE(loaded.mic == -1); REQUIRE(loaded.reference == -1); REQUIRE(loaded.outputA == -1); REQUIRE(loaded.outputB == -1);
}

TEST_CASE("Missing and empty session paths preserve the output", "[session][files]") {
    Bundle bundle; auto original = synthetic(); auto out = original; std::string error;
    for (const auto& path : {std::filesystem::path{}, bundle.path}) {
        REQUIRE_FALSE(foh::loadSession(path, out, error)); REQUIRE_FALSE(error.empty());
        REQUIRE(out.micCalText == original.micCalText); REQUIRE(out.targets == original.targets);
    }
}
