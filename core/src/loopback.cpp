#include "fohcore/fohcore.h"
#include "fohcore/json.h"

namespace foh {
bool LoopbackCorrection::valid() const {
    if (!std::isfinite(sampleRate) || sampleRate < 1 || !isPowerOfTwo(fftSize)
        || fftSize < 2 || fftSize > (1 << 18) || H.size() != size_t(fftSize / 2 + 1)) return false;
    for (auto h : H)
        if (!std::isfinite(h.real()) || !std::isfinite(h.imag()) || std::abs(h) < 1e-6 || std::abs(h) > 1e6) return false;
    return true;
}
LoopbackCorrection LoopbackCorrection::measure(const TFResult& raw) {
    LoopbackCorrection c {raw.sampleRate, raw.fftSize, raw.H};
    // DC is outside the measured audio band. Do not reject an AC-coupled input.
    if (!c.H.empty()) c.H[0] = 1;
    if (!c.valid() || raw.averages < 3 || raw.coherence.size() != raw.H.size()) return {};
    // DC is not excited reliably; require coherent broadband data in the audio band.
    for (size_t k = 1; k < raw.H.size(); ++k) {
        double hz = k * raw.sampleRate / raw.fftSize;
        if (hz >= 20 && hz <= 20000 && !(raw.coherence[k] >= 0.9)) return {};
    }
    return c;
}
bool LoopbackCorrection::apply(TFResult& tf) const {
    if (!valid() || tf.sampleRate != sampleRate || tf.fftSize != fftSize || tf.H.size() != H.size()
        || tf.magDb.size() != H.size() || tf.phaseDeg.size() != H.size()) return false;
    for (size_t k = 0; k < H.size(); ++k) {
        tf.H[k] /= H[k];
        tf.magDb[k] = 20 * std::log10(std::abs(tf.H[k]) + 1e-12);
        tf.phaseDeg[k] = std::arg(tf.H[k]) * 180 / M_PI;
    }
    return true;
}
Json LoopbackCorrection::toJson() const {
    auto j = Json::object(); j.set("sampleRate", sampleRate); j.set("fftSize", fftSize);
    auto bins = Json::array();
    for (auto h : H) { auto b = Json::array(); b.push(h.real()); b.push(h.imag()); bins.push(std::move(b)); }
    j.set("H", std::move(bins)); return j;
}
LoopbackCorrection LoopbackCorrection::fromJson(const Json& j) {
    if (!j.isObject() || j["sampleRate"].type() != Json::Number || j["fftSize"].type() != Json::Number
        || !std::isfinite(j["fftSize"].asNumber()) || std::floor(j["fftSize"].asNumber()) != j["fftSize"].asNumber()) return {};
    LoopbackCorrection c; c.sampleRate = j["sampleRate"].asNumber(); c.fftSize = j["fftSize"].asInt();
    if (!j["H"].isArray() || j["H"].items().size() > (1 << 17) + 1) return {};
    for (const auto& b : j["H"].items()) {
        if (!b.isArray() || b.items().size() != 2 || b.items()[0].type() != Json::Number || b.items()[1].type() != Json::Number) return {};
        c.H.emplace_back(b.items()[0].asNumber(), b.items()[1].asNumber());
    }
    return c.valid() ? c : LoopbackCorrection{};
}
}
