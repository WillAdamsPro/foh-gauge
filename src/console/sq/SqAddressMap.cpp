#include "SqAddressMap.h"
namespace foh::sq {
namespace {
// Capture-derived register table, scoped to the pending 1.6.3 profile.
struct BandRegisters { std::uint8_t gain, freq, q, shape; const char* name; };
constexpr std::array<BandRegisters, 4> bands{{
    {0x0c,0x0d,0x0e,0x0f,"LF"}, {0x10,0x11,0x12,0xff,"LM"},
    {0x13,0x14,0x15,0xff,"HM"}, {0x16,0x17,0x18,0x19,"HF"}
}};
const std::array<Strip, 2> outputs{{{StripKind::Aux,1}, {StripKind::Matrix,1}}};
}
const FirmwareProfile& SqAddressMap::profile() {
    // TODO docs/console/sq-protocol.md §6: record exact 0x02 version payload
    // from capture 3 before adding 1.6.3 to the firmware write allow-list.
    static const FirmwareProfile fw{"1.6.3 (version bytes pending capture 3)", {}};
    return fw;
}
bool SqAddressMap::firmwareVerified(const Bytes& bytes) {
    return !profile().versionBytes.empty() && bytes == profile().versionBytes;
}
std::optional<std::uint8_t> SqAddressMap::stripAddress(Strip s) {
    if (s.kind == StripKind::Input && s.number >= 1 && s.number <= 48) return std::uint8_t(s.number - 1);
    for (auto verified : outputs) {
        if (verified.kind == s.kind && s.number == verified.number)
            return s.kind == StripKind::Aux ? 0x73 : 0x58;
    }
    // TODO docs/console/sq-protocol.md §6: other Aux/Matrix addresses unverified.
    return {};
}
const std::array<Strip,2>& SqAddressMap::verifiedOutputs() { return outputs; }
std::optional<Address> SqAddressMap::address(Target t) {
    auto b3 = stripAddress(t.strip); if (!b3) return {};
    auto band = static_cast<unsigned>(t.band); if (band >= bands.size()) return {};
    Address a{0x11,0,*b3,0};
    switch (t.parameter) {
        case Parameter::Gain: a.reg = bands[band].gain; break;
        case Parameter::Frequency: a.reg = bands[band].freq; break;
        case Parameter::Q: a.reg = bands[band].q; break;
        case Parameter::Shape: a.reg = bands[band].shape; break;
        case Parameter::PeqIn: a.reg = 0x1a; break;
        case Parameter::DelayTime: a.cat = 0x14; a.reg = 0x0d; break;
        case Parameter::DelayIn: a.cat = 0x14; a.reg = 0x0c; break;
        default: return {};
    }
    if (a.reg == 0xff) return {};
    return a;
}
bool SqAddressMap::writable(Target t) {
    if (!address(t)) return false;
    // Input delay is deliberately read-only. No PEQ enable, Q, or shape writes.
    return t.parameter == Parameter::Gain || t.parameter == Parameter::Frequency ||
        ((t.parameter == Parameter::DelayTime || t.parameter == Parameter::DelayIn) && t.strip.kind != StripKind::Input);
}
std::optional<Target> SqAddressMap::writableTarget(const Frame& frame) {
    Strip strip;
    if (frame.b3 < 48) strip = {StripKind::Input, int(frame.b3) + 1};
    else if (frame.b3 == 0x73) strip = {StripKind::Aux,1};
    else if (frame.b3 == 0x58) strip = {StripKind::Matrix,1};
    else return {};
    for (auto p : {Parameter::Gain, Parameter::Frequency, Parameter::DelayTime, Parameter::DelayIn}) {
        for (auto b : {Band::LF, Band::LM, Band::HM, Band::HF}) {
            Target t{strip,p,b}; const auto a = address(t);
            if (writable(t) && a && frame.cat == a->cat && frame.reg == a->reg && frame.mod == a->mod) return t;
        }
    }
    return {};
}
std::string SqAddressMap::describe(Target t) {
    const auto prefix = t.strip.kind == StripKind::Input ? "Input " : t.strip.kind == StripKind::Aux ? "Aux " : "Matrix ";
    std::string out = prefix + std::to_string(t.strip.number);
    if (t.parameter == Parameter::DelayTime || t.parameter == Parameter::DelayIn) return out + " output delay";
    const auto i = static_cast<unsigned>(t.band);
    return out + " PEQ " + (i < bands.size() ? bands[i].name : "?");
}
}
