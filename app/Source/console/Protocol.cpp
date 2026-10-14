#include "Protocol.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace foh::console {
namespace {
void string(Bytes& b, const std::string& s) {
    b.insert(b.end(), s.begin(), s.end()); b.push_back(0);
    while (b.size() % 4) b.push_back(0);
}
void u32(Bytes& b, std::uint32_t v, bool little) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> ((little ? i : 3-i) * 8)));
}
std::uint32_t read32(const Bytes& b, std::size_t off, bool little) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= std::uint32_t(b[off + i]) << ((little ? i : 3-i) * 8);
    return v;
}
std::string two(int n) { return (n < 10 ? "0" : "") + std::to_string(n); }
bool integer(double v) { return v == std::floor(v); }
bool boolean(Parameter p) { return p == Parameter::eqOn || p == Parameter::delayOn; }
bool bandParameter(Parameter p) { return p == Parameter::frequency || p == Parameter::gain || p == Parameter::q || p == Parameter::shape; }
int sqChannel(const Target& t) { return 0x58 + t.number - 1; }
int sqRegister(const Value& v) {
    if (v.parameter == Parameter::eqOn) return 0x1a;
    if (v.parameter == Parameter::delayOn) return 0x0c;
    if (v.parameter == Parameter::delayMs) return 0x0d;
    const int base[] {0x0c, 0x10, 0x13, 0x16};
    return base[v.band - 1] + (v.parameter == Parameter::gain ? 0 : v.parameter == Parameter::frequency ? 1 : v.parameter == Parameter::q ? 2 : 3);
}
double x32Wire(const Value& v) {
    switch (v.parameter) {
        case Parameter::frequency: return std::log(v.value / 20.0) / std::log(1000.0);
        case Parameter::gain: return (v.value + 15.0) / 30.0;
        case Parameter::q: return std::log(v.value / 10.0) / std::log(0.03);
        case Parameter::delayMs: return (v.value - 0.3) / 499.7;
        default: return v.value;
    }
}
double x32Physical(Parameter p, double v) {
    switch (p) {
        case Parameter::frequency: return 20.0 * std::pow(1000.0, v);
        case Parameter::gain: return v * 30.0 - 15.0;
        case Parameter::q: return 10.0 * std::pow(0.03, v);
        case Parameter::delayMs: return 0.3 + 499.7 * v;
        default: return v;
    }
}
std::vector<Value> parameters(const Target& t) {
    std::vector<Value> values {{t, Parameter::eqOn}, {t, Parameter::delayOn}, {t, Parameter::delayMs}};
    for (int band = 1; band <= (t.desk == Desk::sq ? 4 : 6); ++band)
        for (auto p : {Parameter::frequency, Parameter::gain, Parameter::q, Parameter::shape}) {
            if (t.desk == Desk::sq && p == Parameter::shape && band != 1 && band != 4) continue;
            values.push_back({t, p, band});
        }
    return values;
}
}
bool validTarget(const Target& t) {
    if ((t.desk != Desk::sq && t.desk != Desk::x32) || (t.bus != Bus::mix && t.bus != Bus::matrix)) return false;
    if (t.desk == Desk::sq && t.bus == Bus::matrix) return false; // No verified matrix wire addresses.
    const int max = t.desk == Desk::sq ? (t.bus == Bus::mix ? 12 : 3) : (t.bus == Bus::mix ? 16 : 6);
    return t.number >= 1 && t.number <= max && t.output >= 0 && t.output <= (t.desk == Desk::sq ? 0 : 16);
}
bool validValue(const Value& v) {
    if (!validTarget(v.target) || !std::isfinite(v.value)) return false;
    const bool sq = v.target.desk == Desk::sq;
    if (bandParameter(v.parameter)) {
        if (v.band < 1 || v.band > (sq ? 4 : 6)) return false;
    } else if (v.band != 0) return false;
    switch (v.parameter) {
        case Parameter::eqOn: case Parameter::delayOn: return v.value == 0 || v.value == 1;
        case Parameter::frequency: return v.value >= 20 && v.value <= 20000;
        case Parameter::gain: return v.value >= -15 && v.value <= 15;
        case Parameter::q: return v.value >= (sq ? 0.5 : 0.3) && v.value <= 10;
        case Parameter::shape:
            if (!integer(v.value)) return false;
            if (sq) return (v.band == 1 || v.band == 4) && (v.value == 0 || v.value == 6 || v.value == 11);
            return v.value >= 0 && v.value <= (v.target.bus == Bus::matrix ? 8 : 5);
        case Parameter::delayMs: return v.value >= (sq ? 0 : 0.3) && v.value <= (sq ? 341 : 500);
    }
    return false;
}
std::string describe(const Value& v) {
    const char* names[] {"EQ enabled", "frequency (Hz)", "gain (dB)", "Q", "filter type", "delay enabled", "delay (ms)"};
    std::ostringstream s;
    s << (v.target.desk == Desk::sq ? "SQ" : "X32") << " " << (v.target.bus == Bus::mix ? "mix " : "matrix ") << v.target.number;
    if (v.parameter == Parameter::delayOn || v.parameter == Parameter::delayMs)
        if (v.target.desk == Desk::x32) s << " / output " << v.target.output;
    if (v.band) s << " / EQ band " << v.band;
    s << ": " << names[static_cast<int>(v.parameter)] << " = " << std::setprecision(8) << v.value;
    return s.str();
}
std::string x32Address(const Value& v) {
    if (!validTarget(v.target) || v.target.desk != Desk::x32) return {};
    if (v.parameter == Parameter::delayOn || v.parameter == Parameter::delayMs) {
        if (!v.target.output) return {};
        return "/outputs/main/" + two(v.target.output) + "/delay/" + (v.parameter == Parameter::delayOn ? "on" : "time");
    }
    auto root = std::string(v.target.bus == Bus::mix ? "/bus/" : "/mtx/") + two(v.target.number) + "/eq/";
    if (v.parameter == Parameter::eqOn) return root + "on";
    if (v.band < 1 || v.band > 6) return {};
    const char* leaf = v.parameter == Parameter::frequency ? "f" : v.parameter == Parameter::gain ? "g" : v.parameter == Parameter::q ? "q" : v.parameter == Parameter::shape ? "type" : "";
    return root + std::to_string(v.band) + "/" + leaf;
}
int x32Source(const Target& t) { return (t.bus == Bus::mix ? 3 : 19) + t.number; }
Bytes oscQuery(const std::string& address) { Bytes b; string(b, address); return b; }
std::optional<OscMessage> parseOsc(const Bytes& b) {
    std::size_t pos = 0;
    auto getString = [&]() -> std::optional<std::string> {
        auto start = pos;
        while (pos < b.size() && b[pos]) ++pos;
        if (pos == b.size()) return {};
        std::string s(b.begin() + static_cast<std::ptrdiff_t>(start), b.begin() + static_cast<std::ptrdiff_t>(pos));
        ++pos;
        while (pos % 4) { if (pos >= b.size() || b[pos++] != 0) return {}; }
        return s;
    };
    auto address = getString();
    if (!address || address->empty() || (*address)[0] != '/') return {};
    auto type = getString();
    if (!type || (*type != ",i" && *type != ",f") || pos + 4 != b.size()) return {};
    auto raw = read32(b, pos, false);
    double value;
    if (*type == ",f") { float f; std::memcpy(&f, &raw, 4); value = f; }
    else { std::int32_t i; std::memcpy(&i, &raw, 4); value = i; }
    if (!std::isfinite(value)) return {};
    return OscMessage {*address, (*type)[1], value};
}
std::optional<Value> decodeX32(const Target& t, const Bytes& b) {
    if (!validTarget(t) || t.desk != Desk::x32) return {};
    auto m = parseOsc(b); if (!m) return {};
    for (auto v : parameters(t)) if (!x32Address(v).empty() && x32Address(v) == m->address) {
        const bool intType = boolean(v.parameter) || v.parameter == Parameter::shape;
        if (m->type != (intType ? 'i' : 'f') || (!intType && (m->value < 0 || m->value > 1))) return {};
        v.value = x32Physical(v.parameter, m->value);
        if (validValue(v)) return v;
    }
    return {};
}
Bytes encodeWrite(const Value& v) {
    if (!validValue(v)) throw std::invalid_argument("Invalid console parameter");
    if (v.target.desk == Desk::x32) {
        auto address = x32Address(v); if (address.empty()) throw std::invalid_argument("Select a routed X32 output for delay");
        auto b = oscQuery(address);
        const bool intType = boolean(v.parameter) || v.parameter == Parameter::shape;
        string(b, intType ? ",i" : ",f");
        std::uint32_t bits;
        if (intType) bits = static_cast<std::uint32_t>(v.value);
        else { float f = static_cast<float>(x32Wire(v)); std::memcpy(&bits, &f, 4); }
        u32(b, bits, false); return b;
    }
    double wire = v.value;
    if (v.parameter == Parameter::gain) wire = 32768 + v.value * 256;
    if (v.parameter == Parameter::frequency) wire = -9206 + 15308 * std::log10(v.value);
    if (v.parameter == Parameter::q) wire = v.value * 8;
    if (v.parameter == Parameter::delayMs) wire = v.value * 96;
    const auto n = static_cast<unsigned>(std::lround(wire));
    const bool delay = v.parameter == Parameter::delayOn || v.parameter == Parameter::delayMs;
    return {0xf7, 0x38, static_cast<std::uint8_t>(delay ? 0x14 : 0x11), static_cast<std::uint8_t>(sqRegister(v)),
        static_cast<std::uint8_t>(sqChannel(v.target)), 0, static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8)};
}
std::vector<Bytes> readRequests(const Target& t) {
    if (!validTarget(t)) return {};
    if (t.desk == Desk::sq) return {sqFrame(0x0a, Bytes(8192, 0xff))};
    std::vector<Bytes> out;
    if (t.output) out.push_back(oscQuery("/outputs/main/" + two(t.output) + "/src"));
    for (const auto& v : parameters(t)) { auto a = x32Address(v); if (!a.empty()) out.push_back(oscQuery(a)); }
    return out;
}
Bytes sqFrame(std::uint8_t type, const Bytes& p) {
    Bytes b {0x7f, type}; u32(b, static_cast<std::uint32_t>(p.size()), true); b.insert(b.end(), p.begin(), p.end()); return b;
}
std::optional<Value> decodeSq(const Target& t, const SqFrame& f) {
    if (!validTarget(t) || t.desk != Desk::sq || f.type != 0xf7 || f.payload.size() != 7) return {};
    const auto& b = f.payload;
    if (b[3] != sqChannel(t) || b[4] != 0) return {};
    const unsigned wire = b[5] | (unsigned(b[6]) << 8);
    for (auto v : parameters(t)) {
        const bool delay = v.parameter == Parameter::delayOn || v.parameter == Parameter::delayMs;
        if (b[1] != (delay ? 0x14 : 0x11) || b[2] != sqRegister(v)) continue;
        v.value = wire;
        if (v.parameter == Parameter::gain) v.value = (double(wire) - 32768) / 256;
        if (v.parameter == Parameter::frequency) v.value = std::pow(10.0, (double(wire) + 9206) / 15308);
        if (v.parameter == Parameter::q) v.value = double(wire) / 8;
        if (v.parameter == Parameter::delayMs) v.value = double(wire) / 96;
        if (validValue(v)) return v;
    }
    return {};
}
std::vector<SqFrame> SqFramer::push(const Bytes& bytes) {
    // Bounded accumulation: reject corrupt lengths rather than scanning inside a payload.
    constexpr std::size_t limit = 131072;
    if (bytes.size() > limit || buffer.size() + bytes.size() > limit) { reset(); throw std::runtime_error("SQ receive buffer overflow"); }
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    std::vector<SqFrame> frames;
    std::size_t pos = 0;
    while (pos < buffer.size()) {
        const auto marker = buffer[pos];
        if (marker != 0xf7 && marker != 0x7f) { reset(); throw std::runtime_error("Invalid SQ frame marker"); }
        const std::size_t header = marker == 0xf7 ? 1 : 6;
        if (buffer.size() - pos < header) break;
        const std::size_t length = marker == 0xf7 ? 7 : read32(buffer, pos + 2, true);
        if (length > limit - 6) { reset(); throw std::runtime_error("Invalid SQ frame length"); }
        if (buffer.size() - pos < header + length) break;
        frames.push_back({marker == 0xf7 ? marker : buffer[pos + 1], Bytes(buffer.begin() + static_cast<std::ptrdiff_t>(pos + header), buffer.begin() + static_cast<std::ptrdiff_t>(pos + header + length))});
        pos += header + length;
    }
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(pos));
    return frames;
}
}
