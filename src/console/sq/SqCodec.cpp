#include "SqCodec.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace foh::sq {
namespace {
double bounded(double value, double low, double high) {
    if (!std::isfinite(value)) throw std::invalid_argument("Non-finite SQ parameter");
    return std::clamp(value, low, high);
}
std::uint32_t length(const Bytes& b) {
    return std::uint32_t(b[2]) | (std::uint32_t(b[3]) << 8) |
           (std::uint32_t(b[4]) << 16) | (std::uint32_t(b[5]) << 24);
}
}
std::optional<Frame> SqCodec::parse(const Bytes& b) {
    if (b.size() != 8 || b[0] != 0xf7) return {};
    return Frame{b[1], b[2], b[3], b[4], b[5], std::uint16_t(b[6] | (unsigned(b[7]) << 8))};
}
Bytes SqCodec::encode(const Frame& f) {
    return {0xf7, f.prefixCategory, f.cat, f.reg, f.b3, f.mod,
            std::uint8_t(f.value), std::uint8_t(f.value >> 8)};
}
Bytes SqCodec::envelope(std::uint8_t sub, const Bytes& payload) {
    if (payload.size() > StreamDecoder::maxPayload) throw std::length_error("SQ envelope too large");
    auto n = std::uint32_t(payload.size());
    Bytes b{0x7f, sub, std::uint8_t(n), std::uint8_t(n >> 8), std::uint8_t(n >> 16), std::uint8_t(n >> 24)};
    b.insert(b.end(), payload.begin(), payload.end()); return b;
}
std::optional<Envelope> SqCodec::parseEnvelope(const Bytes& b) {
    if (b.size() < 6 || b[0] != 0x7f || length(b) > StreamDecoder::maxPayload || b.size() != 6 + length(b)) return {};
    return Envelope{b[1], Bytes(b.begin() + 6, b.end())};
}
std::uint16_t SqCodec::gainWire(double db) { return std::uint16_t(std::lround(32768 + bounded(db, -15, 15) * 256)); }
double SqCodec::gainDb(std::uint16_t v) { return (int(v) - 32768) / 256.0; }
std::uint16_t SqCodec::freqWire(double hz) { return std::uint16_t(std::lround(-9206 + 15308 * std::log10(bounded(hz, 20, 20000)))); }
double SqCodec::freqHz(std::uint16_t v) { return std::pow(10.0, (double(v) + 9206) / 15308); }
std::uint16_t SqCodec::delayWire(double ms, bool output) { return std::uint16_t(std::lround(bounded(ms, 0, output ? 682 : 341) * 96)); }
double SqCodec::delayMs(std::uint16_t v) { return v / 96.0; }
std::optional<bool> SqCodec::boolean(const Frame& f) {
    if (!((f.cat == 0x11 && f.reg == 0x1a) || (f.cat == 0x14 && f.reg == 0x0c)) || f.value > 1) return {};
    return f.value == 1;
}
std::optional<std::uint16_t> SqCodec::qWire(double q) {
    // TODO docs/console/sq-protocol.md §6: Q scale/range await capture 3.
    if (!kExperimentalParams) return {};
    return std::uint16_t(std::lround(bounded(q * 8, 0, 65535)));
}
std::optional<std::uint16_t> SqCodec::shapeWire(int shape) {
    // TODO docs/console/sq-protocol.md §6: 0 bell / 6 shelf / 11 pass unverified.
    if (!kExperimentalParams || (shape != 0 && shape != 6 && shape != 11)) return {};
    return std::uint16_t(shape);
}
std::string SqCodec::hex(const Bytes& bytes) {
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (auto b : bytes) out << std::setw(2) << unsigned(b);
    return out.str();
}
std::vector<Bytes> StreamDecoder::feed(const Bytes& bytes) {
    std::vector<Bytes> frames;
    if (buffer.size() + bytes.size() > maxPayload + 6) { reset(); failed = true; return frames; }
    buffer.insert(buffer.end(), bytes.begin(), bytes.end());
    std::size_t offset = 0;
    while (offset < buffer.size()) {
        const auto marker = buffer[offset];
        if (marker != 0xf7 && marker != 0x7f) { ++offset; continue; }
        const auto remaining = buffer.size() - offset;
        if (remaining < (marker == 0xf7 ? 8u : 6u)) break;
        std::size_t count = 8;
        if (marker == 0x7f) {
            Bytes header(buffer.begin() + offset, buffer.begin() + offset + 6);
            const auto n = length(header);
            if (n > maxPayload) { reset(); failed = true; return {}; }
            count = 6 + n;
        }
        if (remaining < count) break;
        frames.emplace_back(buffer.begin() + offset, buffer.begin() + offset + count);
        offset += count;
    }
    buffer.erase(buffer.begin(), buffer.begin() + offset);
    return frames;
}
}
