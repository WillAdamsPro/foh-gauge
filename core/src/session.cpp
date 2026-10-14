#include "fohcore/session.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace foh {
namespace {
using Path = std::filesystem::path;
int integer(const Json& j, int lo, int hi) {
    const double n = j.asNumber(std::numeric_limits<double>::quiet_NaN());
    if (!std::isfinite(n) || n < lo || n > hi || std::floor(n) != n)
        throw std::runtime_error("Invalid session integer");
    return (int) n;
}
Json numbers(const std::vector<double>& values) {
    Json j = Json::array();
    for (double v : values) j.push(std::isfinite(v) ? Json(v) : Json());
    return j;
}
std::vector<double> numbers(const Json& j) {
    if (!j.isArray()) throw std::runtime_error("Invalid capture array");
    std::vector<double> result;
    for (const auto& v : j.items()) {
        if (!v.isNull() && v.type() != Json::Number) throw std::runtime_error("Invalid capture value");
        result.push_back(v.isNull() ? std::numeric_limits<double>::quiet_NaN() : v.asNumber());
    }
    return result;
}
Json captureJson(const Trace& t) {
    Json j = Json::object(); j.set("name", t.name); j.set("visible", t.visible); j.set("refDelayMs", t.refDelayMs);
    const auto& f = t.tf;
    j.set("sampleRate", f.sampleRate); j.set("fftSize", f.fftSize); j.set("averages", f.averages); j.set("blankThreshold", f.blankThreshold);
    j.set("freq", numbers(f.freq)); j.set("magDb", numbers(f.magDb)); j.set("phaseDeg", numbers(f.phaseDeg)); j.set("coherence", numbers(f.coherence));
    std::vector<double> real, imag; Json blank = Json::array();
    for (auto v : f.H) { real.push_back(v.real()); imag.push_back(v.imag()); }
    for (auto v : f.blanked) blank.push((int) v);
    j.set("real", numbers(real)); j.set("imag", numbers(imag)); j.set("blanked", blank); return j;
}
Trace captureFromJson(const Json& j) {
    if (!j.isObject()) throw std::runtime_error("Invalid capture");
    Trace t; t.name = j["name"].asString(); t.visible = j["visible"].asBool(true); t.refDelayMs = j["refDelayMs"].asNumber();
    auto& f = t.tf; f.sampleRate = j["sampleRate"].asNumber(); f.fftSize = integer(j["fftSize"], 0, 1 << 24); f.averages = integer(j["averages"], 0, std::numeric_limits<int>::max()); f.blankThreshold = j["blankThreshold"].asNumber();
    f.freq = numbers(j["freq"]); f.magDb = numbers(j["magDb"]); f.phaseDeg = numbers(j["phaseDeg"]); f.coherence = numbers(j["coherence"]);
    auto real = numbers(j["real"]), imag = numbers(j["imag"]), blank = numbers(j["blanked"]);
    const auto n = f.freq.size();
    if (f.magDb.size() != n || f.phaseDeg.size() != n || f.coherence.size() != n || real.size() != n || imag.size() != n || (!blank.empty() && blank.size() != n))
        throw std::runtime_error("Capture arrays have different lengths");
    for (size_t i = 0; i < n; ++i) f.H.emplace_back(real[i], imag[i]);
    for (double v : blank) { if (v != 0 && v != 1) throw std::runtime_error("Invalid blanking flag"); f.blanked.push_back((uint8_t) v); }
    return t;
}
void write(const Path& p, const std::string& text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text; f.close(); if (!f) throw std::runtime_error("Cannot write " + p.filename().string());
}
std::string read(const Path& p) {
    if (std::filesystem::is_symlink(p) || !std::filesystem::is_regular_file(p) || std::filesystem::file_size(p) > 64 * 1024 * 1024)
        throw std::runtime_error("Missing or invalid session file: " + p.filename().string());
    std::ifstream f(p, std::ios::binary); if (!f) throw std::runtime_error("Cannot read session file");
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
Json readJson(const Path& p, bool objectRequired = true) {
    std::string err; auto j = Json::parse(read(p), &err);
    if (!err.empty() || (objectRequired && !j.isObject())) throw std::runtime_error("Invalid session JSON: " + p.filename().string());
    return j;
}
void checkFolder(const Path& p) {
    if (p.extension() != ".fohsession" || std::filesystem::is_symlink(p)) throw std::runtime_error("Select a .fohsession folder");
}
void checkChild(const Path& p) {
    if (std::filesystem::is_symlink(p)) throw std::runtime_error("Session subfolders must not be symbolic links");
}
}
bool saveSession(const Path& folder, const Session& s, std::string& error) {
    Path staging = folder; staging += ".saving";
    Path backup = folder; backup += ".backup";
    bool staged = false, backed = false;
    try {
        checkFolder(folder);
        if (!s.workspace.isObject() || !s.measurement.isObject()) throw std::runtime_error("Invalid session metadata");
        if (!std::isfinite(s.sampleRate) || s.sampleRate <= 0 || s.bufferSize <= 0) throw std::runtime_error("Invalid device settings");
        if (!s.micCalText.empty() && !parseMicCal(s.micCalText).ok()) throw std::runtime_error("Invalid session mic calibration");
        for (const auto& t : s.captures) captureFromJson(captureJson(t));
        if (s.captures.size() > TraceStore::kMax) throw std::runtime_error("Too many captures");
        if (std::filesystem::exists(staging) || std::filesystem::is_symlink(staging) || std::filesystem::exists(backup) || std::filesystem::is_symlink(backup)) throw std::runtime_error("A session save recovery folder already exists");
        std::filesystem::create_directories(staging); staged = true;
        std::filesystem::create_directory(staging / "captures"); std::filesystem::create_directory(staging / "targets");
        Json j = Json::object(); j.set("version", 1);
        Json d = Json::object(); d.set("type", s.deviceType); d.set("input", s.inputDevice); d.set("output", s.outputDevice); d.set("sampleRate", s.sampleRate); d.set("bufferSize", s.bufferSize); d.set("inputChannels", s.inputChannels); d.set("outputChannels", s.outputChannels); j.set("devices", d);
        Json c = Json::object(); c.set("mic", s.mic); c.set("reference", s.reference); c.set("outputA", s.outputA); c.set("outputB", s.outputB); j.set("channels", c);
        Json cal = Json::object(); cal.set("name", s.micCalName); cal.set("enabled", s.micCalEnabled); cal.set("present", !s.micCalText.empty()); j.set("micCal", cal);
        if (!s.micCalText.empty()) write(staging / "mic-cal.txt", s.micCalText);
        if (s.splCal) j.set("splCal", *s.splCal);
        j.set("hasTargets", s.targets.has_value());
        if (s.targets) write(staging / "targets" / "targets.json", s.targets->dump());
        j.set("captureCount", (int) s.captures.size());
        for (size_t i = 0; i < s.captures.size(); ++i) write(staging / "captures" / (std::to_string(i) + ".json"), captureJson(s.captures[i]).dump());
        j.set("workspace", s.workspace); j.set("measurement", s.measurement); write(staging / "session.json", j.dump());
        if (std::filesystem::exists(folder)) { std::filesystem::rename(folder, backup); backed = true; }
        std::filesystem::rename(staging, folder); staged = false;
        if (backed) { std::error_code ignored; std::filesystem::remove_all(backup, ignored); }
        error.clear(); return true;
    } catch (const std::exception& e) {
        std::error_code ignored;
        if (staged) std::filesystem::remove_all(staging, ignored);
        if (backed) std::filesystem::rename(backup, folder, ignored);
        error = e.what(); return false;
    }
}
bool loadSession(const Path& folder, Session& out, std::string& error) {
    try {
        checkFolder(folder); checkChild(folder / "captures"); checkChild(folder / "targets");
        auto j = readJson(folder / "session.json");
        if (j["version"].asNumber() != 1) throw std::runtime_error("Unsupported session version");
        Session s; const auto& d = j["devices"]; const auto& c = j["channels"]; const auto& cal = j["micCal"];
        if (!d.isObject() || !c.isObject() || !cal.isObject() || !j["workspace"].isObject()) throw std::runtime_error("Incomplete session manifest");
        s.deviceType = d["type"].asString(); s.inputDevice = d["input"].asString(); s.outputDevice = d["output"].asString(); s.sampleRate = d["sampleRate"].asNumber(); s.bufferSize = integer(d["bufferSize"], 1, 1 << 20); s.inputChannels = d["inputChannels"].asString(); s.outputChannels = d["outputChannels"].asString();
        s.mic = integer(c["mic"], 0, 1023); s.reference = integer(c["reference"], 0, 1023); s.outputA = integer(c["outputA"], 0, 1023); s.outputB = integer(c["outputB"], 0, 1023);
        if (!std::isfinite(s.sampleRate) || s.sampleRate <= 0 || s.bufferSize <= 0 || s.mic < 0 || s.reference < 0 || s.outputA < 0 || s.outputB < 0 || s.mic > 1023 || s.reference > 1023 || s.outputA > 1023 || s.outputB > 1023) throw std::runtime_error("Invalid device or channel settings");
        for (const auto& mask : {s.inputChannels, s.outputChannels})
            if (mask.size() > 1024 || mask.find_first_not_of("01") != std::string::npos) throw std::runtime_error("Invalid channel mask");
        s.micCalName = cal["name"].asString(); s.micCalEnabled = cal["enabled"].asBool();
        if (cal["present"].asBool()) s.micCalText = read(folder / "mic-cal.txt");
        if (!s.micCalText.empty() && !parseMicCal(s.micCalText).ok()) throw std::runtime_error("Invalid session mic calibration");
        if (j.has("splCal")) s.splCal = j["splCal"];
        if (j["hasTargets"].asBool()) s.targets = readJson(folder / "targets" / "targets.json", false);
        double count = j["captureCount"].asNumber(-1);
        if (count < 0 || count > TraceStore::kMax || std::floor(count) != count) throw std::runtime_error("Invalid capture count");
        for (int i = 0; i < count; ++i) s.captures.push_back(captureFromJson(readJson(folder / "captures" / (std::to_string(i) + ".json"))));
        s.workspace = j["workspace"]; s.measurement = j["measurement"]; out = std::move(s); error.clear(); return true;
    } catch (const std::exception& e) { error = e.what(); return false; }
}
bool sessionDevicesAvailable(const Session& s, const std::string& type, const std::vector<std::string>& inputs, const std::vector<std::string>& outputs) {
    auto contains = [](const auto& names, const auto& name) { return name.empty() || std::find(names.begin(), names.end(), name) != names.end(); };
    return type == s.deviceType && contains(inputs, s.inputDevice) && contains(outputs, s.outputDevice);
}
}
