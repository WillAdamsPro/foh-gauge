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
    s.measurement.set("smoothing", 12); s.measurement.set("rtaFraction", 3);
    s.measurement.set("blankThreshold", 0.7); s.measurement.set("refDelayMs", 3.125);
    s.measurement.set("showMode", true);
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
    SECTION("missing manifest") { std::filesystem::remove(bundle.path / "session.json"); }
    SECTION("empty manifest") { std::ofstream(bundle.path / "session.json"); }
    SECTION("huge manifest") { std::filesystem::resize_file(bundle.path / "session.json", 17 * 1024 * 1024); }
    SECTION("wrong encoding") { std::ofstream(bundle.path / "session.json", std::ios::binary) << "\xff\xfe{}"; }
    SECTION("empty mic cal") { std::ofstream(bundle.path / "mic-cal.txt"); }
    SECTION("huge mic cal") { std::filesystem::resize_file(bundle.path / "mic-cal.txt", 17 * 1024 * 1024); }
    SECTION("bad mic row") { std::ofstream(bundle.path / "mic-cal.txt") << "20 0\n1000 1\n2000 nan\n"; }
    SECTION("missing mic calibration") { std::filesystem::remove(bundle.path / "mic-cal.txt"); }
    SECTION("huge mic calibration") { std::filesystem::resize_file(bundle.path / "mic-cal.txt", 17 * 1024 * 1024); }
    SECTION("empty target") { std::ofstream(bundle.path / "targets/targets.json"); }
    SECTION("huge target") { std::filesystem::resize_file(bundle.path / "targets/targets.json", 17 * 1024 * 1024); }
    SECTION("truncated SPL calibration") { std::ofstream(bundle.path / "session.json") << "{\"splCal\":{\"offsetDb\":"; }
    SECTION("missing target") { std::filesystem::remove(bundle.path / "targets/targets.json"); }
    SECTION("garbled target") { std::ofstream(bundle.path / "targets/targets.json") << "{x"; }
    SECTION("wrong target type") { std::ofstream(bundle.path / "targets/targets.json") << "[]"; }
    SECTION("empty target") { std::ofstream(bundle.path / "targets/targets.json"); }
    SECTION("huge target") { std::filesystem::resize_file(bundle.path / "targets/targets.json", 17 * 1024 * 1024); }
    SECTION("huge capture") { std::filesystem::resize_file(bundle.path / "captures/0.json", 17 * 1024 * 1024); }
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

TEST_CASE("Missing or damaged optional rig answers load with default guides and a notice", "[session][rig][regression]") {
    Bundle bundle; auto s = synthetic(); std::string error, notice;
    REQUIRE(foh::saveSession(bundle.path, s, error));
    std::ifstream file(bundle.path / "session.json");
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    auto manifest = foh::Json::parse(text);
    std::vector<foh::Json> bad{foh::Json(), foh::Json::object(), foh::Json::array(), foh::Json(""), foh::Json("{\"clusters\":")};
    for (const char* key : {"clusters", "cabinetsPerCluster", "control", "frontFills", "subs", "subArrangement"}) {
        auto partial = foh::Json::object();
        for (const auto& m : manifest["rigProfile"].members()) if (m.first != key) partial.set(m.first, m.second);
        bad.push_back(partial);
    }
    for (const char* key : {"clusters", "cabinetsPerCluster", "frontFills", "subs"}) {
        for (auto value : {foh::Json(0), foh::Json(-1), foh::Json(257), foh::Json(1e100), foh::Json(1.5), foh::Json("2")}) {
            auto rig = manifest["rigProfile"]; rig.set(key, value); bad.push_back(rig);
        }
    }
    for (const char* key : {"control", "subArrangement"}) {
        auto rig = manifest["rigProfile"]; rig.set(key, "unknown"); bad.push_back(rig);
    }
    // The first iteration models older sessions and a saved Skip (no rig key).
    for (size_t i = 0; i <= bad.size(); ++i) {
        auto j = foh::Json::object();
        for (const auto& m : manifest.members()) if (m.first != "rigProfile") j.set(m.first, m.second);
        if (i) j.set("rigProfile", bad[i - 1]);
        INFO(j.dump());
        std::ofstream(bundle.path / "session.json") << j.dump();
        auto loaded = s; notice = "stale"; error = "stale";
        REQUIRE(foh::loadSession(bundle.path, loaded, error, &notice));
        CHECK(error.empty()); CHECK_FALSE(notice.empty()); CHECK(notice != "stale");
        CHECK_FALSE(loaded.rigProfile); CHECK(foh::rigSteps(loaded.rigProfile) == foh::rigSteps(std::nullopt));
        CHECK(loaded.workspace == s.workspace); CHECK(loaded.measurement == s.measurement);
        CHECK(loaded.captures.size() == s.captures.size()); CHECK(loaded.micCalText == s.micCalText);
        REQUIRE(foh::saveSession(bundle.path, loaded, error));
        auto reloaded = s;
        REQUIRE(foh::loadSession(bundle.path, reloaded, error, &notice));
        CHECK_FALSE(reloaded.rigProfile); CHECK_FALSE(notice.empty());
    }
}

TEST_CASE("Rig answers round trip session boundaries flags and arrangements", "[session][rig]") {
    Bundle bundle; auto s = synthetic(); std::string error, notice;
    for (int count : {1, 256}) for (bool subs : {false, true}) for (bool fills : {false, true})
        for (auto control : {foh::RigProfile::Control::Grouped, foh::RigProfile::Control::Individual})
            for (auto arrangement : {foh::RigProfile::SubArrangement::Centre, foh::RigProfile::SubArrangement::LeftRight,
                                     foh::RigProfile::SubArrangement::Distributed, foh::RigProfile::SubArrangement::Other}) {
                auto& rig = *s.rigProfile; rig.clusters = rig.cabinetsPerCluster = count;
                rig.subs = subs; rig.frontFills = fills; rig.control = control; rig.subArrangement = arrangement;
                REQUIRE(foh::saveSession(bundle.path, s, error));
                std::ifstream f(bundle.path / "session.json"); std::string text{std::istreambuf_iterator<char>(f), {}};
                auto j = foh::Json::parse(text), extra = j["rigProfile"];
                extra.set("futureAnswer", foh::Json::object()); j.set("rigProfile", extra); j.set("futureSessionKey", true);
                std::ofstream(bundle.path / "session.json") << j.dump();
                foh::Session loaded; notice = "stale";
                REQUIRE(foh::loadSession(bundle.path, loaded, error, &notice));
                CHECK(notice.empty()); REQUIRE(loaded.rigProfile);
                CHECK(loaded.rigProfile->toJson() == rig.toJson());
                CHECK(foh::rigSteps(loaded.rigProfile) == foh::rigSteps(s.rigProfile));
            }
}

TEST_CASE("Syntactically damaged rig manifests fail safely without partial restoration", "[session][rig]") {
    Bundle bundle; auto s = synthetic(); std::string error, notice;
    REQUIRE(foh::saveSession(bundle.path, s, error));
    std::ifstream f(bundle.path / "session.json"); const std::string text{std::istreambuf_iterator<char>(f), {}};
    const auto start = text.find("\"rigProfile\""); REQUIRE(start != std::string::npos);
    for (const auto& ending : {std::string("\"rigProfile\": {\"clusters\":"), std::string("\"rigProfile\": {broken}}")}) {
        std::ofstream(bundle.path / "session.json") << text.substr(0, start) << ending;
        auto loaded = s; notice = "stale";
        REQUIRE_FALSE(foh::loadSession(bundle.path, loaded, error, &notice));
        CHECK_FALSE(error.empty()); CHECK(notice.empty());
        REQUIRE(loaded.rigProfile); CHECK(loaded.rigProfile->toJson() == s.rigProfile->toJson());
        CHECK(loaded.workspace == s.workspace); CHECK(loaded.captures.size() == s.captures.size());
    }
}

TEST_CASE("Session manifest rejects wrong types before replacing any data", "[session][regression]") {
    Bundle bundle; auto original = synthetic(); std::string error, notice;
    REQUIRE(foh::saveSession(bundle.path, original, error));
    std::ifstream file(bundle.path / "session.json");
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    const auto manifest = foh::Json::parse(text);
    for (const auto& group : {std::string("devices"), std::string("channels"), std::string("micCal"), std::string("measurement"), std::string()}) {
        std::vector<std::string> keys;
        if (group == "devices") keys = {"type", "input", "output", "sampleRate", "bufferSize", "inputChannels", "outputChannels"};
        else if (group == "channels") keys = {"mic", "reference", "outputA", "outputB"};
        else if (group == "micCal") keys = {"name", "enabled", "present"};
        else if (group == "measurement") keys = {"showMode", "averages", "smoothing", "rtaFraction", "blankThreshold", "refDelayMs", "traceColours"};
        else keys = {"version", "captureCount", "hasTargets", "workspace", "measurement", "splCal"};
        for (const auto& key : keys) {
            INFO(group << "." << key);
            auto j = manifest;
            if (group.empty()) j.set(key, foh::Json::array());
            else { auto child = j[group]; child.set(key, foh::Json::object()); j.set(group, child); }
            std::ofstream(bundle.path / "session.json") << j.dump();
            auto out = original; notice = "stale";
            REQUIRE_FALSE(foh::loadSession(bundle.path, out, error, &notice));
            CHECK_FALSE(error.empty()); CHECK(notice.empty());
            CHECK(out.deviceType == original.deviceType); CHECK(out.inputDevice == original.inputDevice);
            CHECK(out.outputDevice == original.outputDevice); CHECK(out.sampleRate == original.sampleRate);
            CHECK(out.bufferSize == original.bufferSize); CHECK(out.inputChannels == original.inputChannels);
            CHECK(out.outputChannels == original.outputChannels); CHECK(out.mic == original.mic);
            CHECK(out.reference == original.reference); CHECK(out.outputA == original.outputA); CHECK(out.outputB == original.outputB);
            CHECK(out.micCalEnabled == original.micCalEnabled); CHECK(out.micCalName == original.micCalName);
            CHECK(out.micCalText == original.micCalText); CHECK(out.splCal == original.splCal); CHECK(out.targets == original.targets);
            REQUIRE(out.rigProfile); CHECK(out.rigProfile->toJson() == original.rigProfile->toJson());
            CHECK(out.workspace == original.workspace); CHECK(out.measurement == original.measurement);
            REQUIRE(out.captures.size() == original.captures.size()); CHECK(out.captures[0].tf.H == original.captures[0].tf.H);
        }
    }
}

TEST_CASE("Session requires its asset directories and calibration copy", "[session][regression]") {
    Bundle bundle; auto s = synthetic(); std::string error;
    REQUIRE(foh::saveSession(bundle.path, s, error));
    SECTION("missing captures directory") { std::filesystem::remove_all(bundle.path / "captures"); }
    SECTION("missing calibration") { std::filesystem::remove(bundle.path / "mic-cal.txt"); }
    SECTION("empty captures still require directory") {
        s.captures.clear(); REQUIRE(foh::saveSession(bundle.path, s, error));
        std::filesystem::remove_all(bundle.path / "captures");
    }
    auto out = synthetic(); REQUIRE_FALSE(foh::loadSession(bundle.path, out, error));
    CHECK_FALSE(error.empty()); CHECK(out.captures.size() == 1);
}

TEST_CASE("Save rejects data that Open cannot accept and preserves the destination", "[session][regression]") {
    Bundle bundle; auto s = synthetic(); std::string error;
    REQUIRE(foh::saveSession(bundle.path, s, error));
    SECTION("wrong target type") { s.targets = foh::Json::array(); }
    SECTION("wrong SPL type") { s.splCal = foh::Json::array(); }
    SECTION("wrong device mask") { s.inputChannels = "bad"; }
    SECTION("wrong channel") { s.mic = 2048; }
    SECTION("enabled missing calibration") { s.micCalText.clear(); }
    SECTION("invalid measurement") { s.measurement.set("showMode", "true"); }
    SECTION("invalid trace colour") { auto a = foh::Json::array(); a.push("oops"); s.measurement.set("traceColours", a); }
    REQUIRE_FALSE(foh::saveSession(bundle.path, s, error)); CHECK_FALSE(error.empty());
    CHECK_FALSE(std::filesystem::exists(bundle.path.string() + ".saving"));
    CHECK_FALSE(std::filesystem::exists(bundle.path.string() + ".backup"));
    foh::Session loaded; REQUIRE(foh::loadSession(bundle.path, loaded, error));
    CHECK(loaded.measurement == synthetic().measurement); CHECK(loaded.inputChannels == "11");
}

#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
TEST_CASE("Write failures preserve session data and remove staging files", "[session][files][regression]") {
    Bundle root; std::filesystem::create_directory(root.path);
    const auto destination = root.path / "Synthetic.fohsession";
    auto s = synthetic(); std::string error;
    REQUIRE(foh::saveSession(destination, s, error));
    SECTION("read only destination parent") {
        if (geteuid() == 0) { WARN("Permission denial requires an unprivileged user"); return; }
        std::filesystem::permissions(root.path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
        const bool saved = foh::saveSession(destination, s, error);
        std::filesystem::permissions(root.path, std::filesystem::perms::owner_all);
        CHECK_FALSE(saved); CHECK_FALSE(error.empty());
    }
    SECTION("exhausted write capacity") {
        // Isolate the process-wide limit and SIGXFSZ handling. A tiny file-size
        // quota deterministically fails a staged asset write, like a full disk,
        // without filling the developer's disk or adding a production test hook.
        const auto pid = fork(); REQUIRE(pid >= 0);
        if (pid == 0) {
            std::signal(SIGXFSZ, SIG_IGN);
            struct rlimit limit {8, 8};
            if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
            const bool saved = foh::saveSession(destination, s, error);
            _exit(!saved && !error.empty() ? 0 : 3);
        }
        int status = 0; REQUIRE(waitpid(pid, &status, 0) == pid);
        REQUIRE(WIFEXITED(status)); CHECK(WEXITSTATUS(status) == 0);
    }
    CHECK_FALSE(std::filesystem::exists(destination.string() + ".saving"));
    CHECK_FALSE(std::filesystem::exists(destination.string() + ".backup"));
    foh::Session loaded; REQUIRE(foh::loadSession(destination, loaded, error));
    CHECK(loaded.micCalText == s.micCalText); CHECK(loaded.measurement == s.measurement);
    REQUIRE(loaded.captures.size() == 1); CHECK(loaded.captures[0].tf.H == s.captures[0].tf.H);
}
#endif

TEST_CASE("Save refuses to replace a regular file masquerading as a session folder", "[session][regression]") {
    Bundle bundle; std::ofstream(bundle.path) << "synthetic original";
    std::string error;
    REQUIRE_FALSE(foh::saveSession(bundle.path, synthetic(), error));
    CHECK_FALSE(error.empty());
    std::ifstream file(bundle.path); const std::string text{std::istreambuf_iterator<char>(file), {}};
    CHECK(text == "synthetic original");
    CHECK_FALSE(std::filesystem::exists(bundle.path.string() + ".saving"));
    CHECK_FALSE(std::filesystem::exists(bundle.path.string() + ".backup"));
}

TEST_CASE("Unnamed session devices round trip and require Setup for routed directions", "[session][regression]") {
    Bundle bundle; auto s = synthetic(); std::string error;
    SECTION("missing input") { s.inputDevice.clear(); }
    SECTION("missing output") { s.outputDevice.clear(); }
    SECTION("both names missing") { s.inputDevice.clear(); s.outputDevice.clear(); }
    REQUIRE(foh::saveSession(bundle.path, s, error));
    foh::Session loaded; REQUIRE(foh::loadSession(bundle.path, loaded, error));
    CHECK(loaded.inputDevice == s.inputDevice); CHECK(loaded.outputDevice == s.outputDevice);
    CHECK_FALSE(foh::sessionDevicesAvailable(loaded, "Synthetic", {"Test Input"}, {"Test Output"}));
    s.inputDevice = "Test Input"; s.outputDevice.clear(); s.outputA = s.outputB = -1;
    CHECK(foh::sessionDevicesAvailable(s, "Synthetic", {"Test Input"}, {}));
    s.inputDevice.clear(); s.outputDevice = "Test Output"; s.mic = s.reference = -1;
    CHECK(foh::sessionDevicesAvailable(s, "Synthetic", {}, {"Test Output"}));
}

TEST_CASE("Maximum synthetic capture set round trips and can be replaced by an empty folder", "[session][stress]") {
    Bundle bundle; auto s = synthetic(); std::string error;
    auto trace = s.captures.front(); auto& tf = trace.tf;
    tf.fftSize = 32768;
    const size_t bins = size_t(tf.fftSize / 2 + 1);
    tf.freq.resize(bins); tf.magDb.resize(bins); tf.phaseDeg.resize(bins);
    tf.coherence.resize(bins); tf.H.resize(bins); tf.blanked.resize(bins);
    for (size_t i = 0; i < bins; ++i) {
        tf.freq[i] = i * tf.sampleRate / tf.fftSize;
        tf.magDb[i] = double(i % 31) / 10; tf.phaseDeg[i] = -double(i % 180);
        tf.coherence[i] = 0.9; tf.H[i] = {0.5, -0.25}; tf.blanked[i] = uint8_t(i % 2);
    }
    s.captures.assign(foh::TraceStore::kMax, trace);
    for (size_t i = 0; i < s.captures.size(); ++i) {
        s.captures[i].name = "Synthetic capture " + std::to_string(i);
        s.captures[i].refDelayMs = double(i); s.captures[i].visible = i % 2 == 0;
    }
    REQUIRE(foh::saveSession(bundle.path, s, error));
    foh::Session loaded; REQUIRE(foh::loadSession(bundle.path, loaded, error));
    REQUIRE(loaded.captures.size() == s.captures.size());
    for (size_t i = 0; i < s.captures.size(); ++i) {
        const auto& t = loaded.captures[i];
        CHECK(t.name == s.captures[i].name); CHECK(t.visible == s.captures[i].visible);
        CHECK(t.refDelayMs == s.captures[i].refDelayMs);
        CHECK(t.tf.freq == tf.freq); CHECK(t.tf.magDb == tf.magDb); CHECK(t.tf.phaseDeg == tf.phaseDeg);
        CHECK(t.tf.coherence == tf.coherence); CHECK(t.tf.H == tf.H); CHECK(t.tf.blanked == tf.blanked);
    }
    // Oversized input must fail without replacing any part of the large bundle.
    s.captures.push_back(trace);
    REQUIRE_FALSE(foh::saveSession(bundle.path, s, error));
    REQUIRE(foh::loadSession(bundle.path, loaded, error));
    CHECK(loaded.captures.size() == foh::TraceStore::kMax);
    s.captures.clear(); REQUIRE(foh::saveSession(bundle.path, s, error));
    REQUIRE(std::filesystem::is_empty(bundle.path / "captures"));
    REQUIRE(foh::loadSession(bundle.path, loaded, error)); CHECK(loaded.captures.empty());
}
