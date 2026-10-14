#include "Control.h"
namespace foh::console {
void Control::select(Target t) { cancel(); selected = t; values.clear(); outputSource.reset(); }
bool Control::read() {
    if (!validTarget(selected)) return false;
    cancel(); outputSource.reset(); values.clear();
    for (const auto& b : readRequests(selected)) if (!send(b)) return false;
    return true;
}
void Control::receive(const Bytes& b) {
    if (selected.desk != Desk::x32) return;
    auto m = parseOsc(b);
    if (m && selected.output) {
        auto t = selected; auto address = x32Address({t, Parameter::delayMs});
        address = address.substr(0, address.find("/delay/")) + "/src";
        if (m->address == address && m->type == 'i') {
            const auto source = static_cast<int>(m->value);
            if (outputSource && *outputSource != source) cancel();
            outputSource = source;
        }
    }
    if (auto value = decodeX32(selected, b)) {
        for (auto& known : values) if (known.parameter == value->parameter && known.band == value->band) { known = *value; return; }
        values.push_back(*value);
    }
}
std::optional<Value> Control::readValue(Parameter p, int band) const {
    for (const auto& v : values) if (v.parameter == p && v.band == band) return v;
    return {};
}
std::optional<std::uint64_t> Control::propose(Value v) {
    cancel();
    if (!validValue(v) || v.target.desk != selected.desk || v.target.bus != selected.bus
        || v.target.number != selected.number || v.target.output != selected.output) return {};
    // SQ matrices and 1.6.3 compatibility are not established by available captures.
    // Fail closed until protocol evidence is supplied; no guessed SQ writes.
    if (selected.desk == Desk::sq) return {};
    if (v.parameter == Parameter::delayOn || v.parameter == Parameter::delayMs)
        if (!selected.output || !outputSource || *outputSource != x32Source(selected)) return {};
    pending = v;
    return serial;
}
std::string Control::preview() const { return pending ? describe(*pending) : std::string{}; }
bool Control::confirm(std::uint64_t proposal, bool accepted) {
    if (!pending || proposal != serial) return false;
    auto value = *pending;
    cancel(); // Consume before calling transport, including failure and reentrancy.
    if (!accepted) return false;
    if (value.parameter == Parameter::delayOn || value.parameter == Parameter::delayMs)
        if (!outputSource || *outputSource != x32Source(selected)) return false;
    return send(encodeWrite(value));
}
void Control::cancel() { pending.reset(); ++serial; }
}
