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
    REQUIRE(loaded.splCal == s.splCal);
    REQUIRE(foh::SplCal::fromJson((*loaded.splCal)["Synthetic\nTest Input\n1"]).level(-20, -2.1) == 94);
    REQUIRE(foh::SplCal::fromJson((*loaded.splCal)["Synthetic\nTest Input\n2"]).level(-30, 1.5) == 114); REQUIRE(loaded.targets == s.targets); REQUIRE(loaded.workspace == s.workspace); REQUIRE(loaded.measurement == s.measurement);
    REQUIRE(loaded.captures.size() == 1); const auto& t = loaded.captures[0]; const auto& original = s.captures[0];
    REQUIRE(t.name == original.name); REQUIRE(t.visible == original.visible); REQUIRE(t.refDelayMs == original.refDelayMs);
    REQUIRE(t.tf.sampleRate == original.tf.sampleRate); REQUIRE(t.tf.fftSize == original.tf.fftSize); REQUIRE(t.tf.averages == original.tf.averages); REQUIRE(t.tf.blankThreshold == original.tf.blankThreshold);
    REQUIRE(t.tf.freq == original.tf.freq); REQUIRE(t.tf.magDb == original.tf.magDb); REQUIRE(t.tf.phaseDeg == original.tf.phaseDeg); REQUIRE(t.tf.coherence == original.tf.coherence); REQUIRE(t.tf.H == original.tf.H); REQUIRE(t.tf.blanked == original.tf.blanked);
    s.captures.clear(); s.micCalText.clear(); s.micCalEnabled = false; s.targets.reset(); s.splCal.reset();
    REQUIRE(foh::saveSession(bundle.path, s, error)); REQUIRE(foh::loadSession(bundle.path, loaded, error));
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
    s.captures.resize(foh::TraceStore::kMax + 1); REQUIRE_FALSE(foh::saveSession(bundle.path, s, error));
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
