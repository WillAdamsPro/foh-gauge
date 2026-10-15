#include <catch2/catch_test_macros.hpp>
#include <fohcore/fohcore.h>
#include <fohcore/json.h>
#include <fohcore/input.h>
#include <chrono>
#include <limits>

TEST_CASE("User curve files reject corrupt rows rather than importing partial curves", "[files]") {
    const std::string good = "20 0\n1000 1\n";
    for (const auto& row : {"100 bad", "100 nan", "100 inf", "100 -inf", "-100 1", "100 1000", "100 -1000",
                           "100 1 2 3 4", "100", "100 1 junk", "1000001 0", "garbled row"}) {
        INFO(row);
        auto cal = foh::parseMicCal(good + row + "\n");
        REQUIRE_FALSE(cal.ok()); REQUIRE_FALSE(cal.error.empty());
        auto target = foh::parseTarget(good + row + "\n");
        REQUIRE_FALSE(target.ok()); REQUIRE_FALSE(target.error.empty());
    }
    for (const auto* prefix : {"nan 0\n", "inf 0\n", "bad 0\n"}) REQUIRE_FALSE(foh::parseTarget(std::string(prefix) + good).ok());
    for (const auto* sens : {"nan", "inf", "1000", "garbled", "2 junk", "1dirty"})
        REQUIRE_FALSE(foh::parseMicCal(std::string("Sens Factor: ") + sens + "\n" + good).ok());
    for (const auto* metadata : {"Tolerance: 1000", "Tolerance: 3 junk", "UseTolerance: 1 junk"})
        REQUIRE_FALSE(foh::parseTarget(std::string(metadata) + "\n" + good).ok());
    REQUIRE_FALSE(foh::parseTarget(good + "2000 0 0 2\n").ok());
    for (const auto* row : {"2000,,0", "2000,0,", "2000; ;0"}) REQUIRE_FALSE(foh::parseMicCal(good + row).ok());
}

TEST_CASE("User curve encodings and resource limits are validated", "[files]") {
    for (const auto& text : {std::string(), std::string("\xff\xfe" "2\0" "0\0", 6),
                            std::string("20 0\n1000 1\n\xff"), std::string("20 0\n1000 1\n") + char(0),
                            std::string(foh::input::maxBytes + 1, ' '), std::string("\xc0\xaf")}) {
        REQUIRE_FALSE(foh::parseMicCal(text).ok()); REQUIRE_FALSE(foh::parseTarget(text).ok());
    }
    std::string many;
    for (size_t i = 0; i <= foh::input::maxRows; ++i) many += "20 0\n";
    REQUIRE_FALSE(foh::parseMicCal(many).ok()); REQUIRE_FALSE(foh::parseTarget(many).ok());
    auto cal = foh::parseMicCal("\xef\xbb\xbf" "1000 1\r\n20 0\r\n1000 2\r\n");
    REQUIRE(cal.ok()); REQUIRE(cal.hz == std::vector<double>{20, 1000}); REQUIRE(cal.db == std::vector<double>{0, 1});
    auto target = foh::parseTarget("\xef\xbb\xbf" "1000 1\r\n20 0\r\n1000 2\r\n2000 * * *\r\n");
    REQUIRE(target.ok()); REQUIRE(target.hz == cal.hz); REQUIRE(target.db == cal.db);
}

TEST_CASE("Bounded binary file reads reject missing empty and huge calibration files", "[files]") {
    auto path = std::filesystem::temp_directory_path() / ("foh-synthetic-input-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    REQUIRE_FALSE(foh::loadMicCal(path.string()).ok());
    std::ofstream(path, std::ios::binary).close(); REQUIRE_FALSE(foh::loadMicCal(path.string()).ok());
    { std::ofstream f(path, std::ios::binary); f << "\xef\xbb\xbf" "20 0\r\n1000 1\r\n"; }
    REQUIRE(foh::loadMicCal(path.string()).ok());
    std::filesystem::resize_file(path, foh::input::maxBytes + 1);
    REQUIRE_FALSE(foh::loadMicCal(path.string()).ok()); REQUIRE_THROWS(foh::input::read(path));
}

TEST_CASE("JSON rejects invalid grammar escapes encoding and excessive complexity", "[json][files]") {
    for (const auto& text : {std::string(), std::string("{"), std::string("[1,"), std::string("01"), std::string("1."),
                            std::string("-.1"), std::string("0x10"), std::string("1e"), std::string("1e999"),
                            std::string("NaN"), std::string("Infinity"), std::string("{\"a\":1,\"a\":2}"),
                            std::string("\"\\q\""), std::string("\"\\uXX00\""), std::string("\"\\ud800\""),
                            std::string("\"\\udc00\""), std::string("\"\\ud800\\u0000\""), std::string("\"line\nfeed\""),
                            std::string("\xff\xfe{}"), std::string("\"\xed\xa0\x80\""),
                            std::string(foh::input::maxBytes + 1, ' '), std::string(70, '[') + std::string(70, ']')}) {
        INFO(text.substr(0, 80)); std::string error;
        REQUIRE(foh::Json::parse(text, &error).isNull()); REQUIRE_FALSE(error.empty());
    }
    std::string keys = "{";
    for (int i = 0; i < 4097; ++i) { if (i) keys += ','; keys += "\"" + std::to_string(i) + "\":0"; }
    keys += '}'; std::string error; foh::Json::parse(keys, &error); REQUIRE_FALSE(error.empty());
    std::string values = "[";
    for (int i = 0; i < 262145; ++i) { if (i) values += ','; values += "0"; }
    values += ']'; foh::Json::parse(values, &error); REQUIRE_FALSE(error.empty());
    auto valid = foh::Json::parse("\xef\xbb\xbf" "{\r\n\"unicode\":\"\\ud83d\\ude00\",\"number\":-1.25e+2}", &error);
    REQUIRE(error.empty()); REQUIRE(valid["unicode"].asString() == "\xf0\x9f\x98\x80"); REQUIRE(valid["number"].asNumber() == -125);
    for (double v : {1e100, -1e100, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE(foh::Json(v).asInt(42) == 42);
}

TEST_CASE("Damaged calibration rows containing Hz are not treated as headings", "[files][regression]") {
    for (const auto* row : {"2000Hz 1", "frequency-broken 1", "bad Hz", "Hz 1"}) {
        INFO(row);
        CHECK_FALSE(foh::parseMicCal(std::string("20 0\n1000 1\n") + row).ok());
    }
    CHECK_FALSE(foh::parseMicCal("2000Hz 1\n20 0\n1000 1\n").ok());
    CHECK(foh::parseMicCal("Freq(Hz) dB Phase\n20 0\n1000 1\n").ok());
}

TEST_CASE("Truncated curve cells and inconsistent columns never publish partial data", "[files]") {
    const std::string good = "20 0\n1000 1\n";
    for (const auto* row : {"2000 2e", "2000 2e+", "2000 2e-", "2000 2 3e", "2000 2 0 1e",
                            "2000 2 0 1", "NAN 0", "INF 0", "INFINITY 0"}) {
        INFO(row);
        auto cal = foh::parseMicCal(good + row);
        REQUIRE_FALSE(cal.ok()); REQUIRE_FALSE(cal.error.empty());
        REQUIRE(cal.hz.empty()); REQUIRE(cal.db.empty()); REQUIRE(cal.phaseDeg.empty());
        auto target = foh::parseTarget(good + row);
        REQUIRE_FALSE(target.ok()); REQUIRE_FALSE(target.error.empty());
        REQUIRE(target.hz.empty()); REQUIRE(target.db.empty());
    }
    for (const auto* text : {"20 0\n1000 1\nfrequency bad row",
                            "20 0\n1000 1\nbadHz 3"}) REQUIRE_FALSE(foh::parseMicCal(text).ok());
    REQUIRE_FALSE(foh::parseTarget("20 0 0 1\n1000 1").ok());
    REQUIRE_FALSE(foh::parseTarget(good + "2000 2 0").ok());
    REQUIRE(foh::parseMicCal("20 0 0\n1000 1").ok()); // optional mic phase is a supported format
    for (const auto* text : {"20 0", "20 0\n20 1", "Tolerance: nan\n20 0\n1000 1"}) {
        auto target = foh::parseTarget(text); REQUIRE_FALSE(target.ok()); REQUIRE(target.hz.empty());
    }
    // Complete final records need no trailing newline; conventional headers still work.
    REQUIRE(foh::parseMicCal("Frequency Hz dB\n20 0 0\n1000 1 0").ok());
    REQUIRE(foh::parseTarget("Frequency (Hz) Magnitude (dB)\n20 0 0 1\n1000 1 0 1").ok());
}

TEST_CASE("Curve file paths reject empty paths directories and missing files", "[files]") {
    for (const auto& path : {std::string{}, std::filesystem::temp_directory_path().string(),
                             (std::filesystem::temp_directory_path() / "foh-synthetic-missing-input.csv").string()}) {
        INFO(path);
        auto cal = foh::loadMicCal(path); REQUIRE_FALSE(cal.ok()); REQUIRE_FALSE(cal.error.empty()); REQUIRE(cal.hz.empty());
        REQUIRE_THROWS(foh::input::read(path)); // also the target import's bounded file reader
    }
}

TEST_CASE("Synthetic truncated CSV FRD and ASCII files fail through the import readers", "[files]") {
    for (const auto* name : {"synthetic-truncated-cal.csv", "synthetic-truncated-cal.frd"}) {
        auto cal = foh::loadMicCal(std::string(FOH_TEST_DATA) + "/" + name);
        REQUIRE_FALSE(cal.ok()); REQUIRE_FALSE(cal.error.empty()); REQUIRE(cal.hz.empty());
    }
    auto target = foh::parseTarget(foh::input::read(std::string(FOH_TEST_DATA) + "/synthetic-truncated-target.txt"));
    REQUIRE_FALSE(target.ok()); REQUIRE_FALSE(target.error.empty()); REQUIRE(target.hz.empty());
}

TEST_CASE("Non-finite target frequencies cannot hide in the export preamble", "[files][target][regression]") {
    for (const auto* row : {"NaN(payload) 0", "NAN(synthetic) nan", "nan( 1e", "inf nan", "infinity inf"}) {
        INFO(row);
        auto target = foh::parseTarget(std::string(row) + "\n20 0\n1000 1\n");
        CHECK_FALSE(target.ok()); CHECK_FALSE(target.error.empty());
        CHECK(target.hz.empty()); CHECK(target.db.empty());
    }
}

TEST_CASE("Insufficient distinct calibration rows leave no partial correction or sensitivity", "[files][calibration][regression]") {
    for (const auto* rows : {"", "20 1", "20 1\n20 2"}) {
        auto cal = foh::parseMicCal(std::string("Sens Factor: 2\n") + rows);
        CHECK_FALSE(cal.ok()); CHECK_FALSE(cal.error.empty());
        CHECK(cal.hz.empty()); CHECK(cal.db.empty()); CHECK(cal.phaseDeg.empty());
        CHECK_FALSE(cal.hasSensFactor); CHECK_FALSE(cal.hasPhase);
        CHECK(cal.dbAt(20) == 0); CHECK(cal.phaseAt(20) == 0);
    }
}

TEST_CASE("A picked curve removed before loading fails through both import readers", "[files][regression]") {
    const auto path = std::filesystem::temp_directory_path() / ("foh-synthetic-removed-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".txt");
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); } } cleanup{path};
    { std::ofstream file(path); file << "20 0\n1000 1\n"; REQUIRE(file.good()); }
    REQUIRE(std::filesystem::is_regular_file(path)); // selection
    REQUIRE(std::filesystem::remove(path));
    auto cal = foh::loadMicCal(path.string());
    CHECK_FALSE(cal.ok()); CHECK_FALSE(cal.error.empty()); CHECK(cal.hz.empty());
    REQUIRE_THROWS(foh::input::read(path)); // target import uses the same bounded reader
}
