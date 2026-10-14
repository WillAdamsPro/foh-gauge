#pragma once
#include <cstdint>
#include <optional>
#include <vector>
#include <string>
namespace foh::sq {
using Bytes = std::vector<std::uint8_t>;
struct Frame {
    std::uint8_t prefixCategory{}, cat{}, reg{}, b3{}, mod{};
    std::uint16_t value{};
};
struct Envelope { std::uint8_t sub{}; Bytes payload; };
class SqCodec {
public:
    static constexpr bool kExperimentalParams = false;
    static std::optional<Frame> parse(const Bytes&);
    static Bytes encode(const Frame&);
    static std::optional<Envelope> parseEnvelope(const Bytes&);
    static Bytes envelope(std::uint8_t sub, const Bytes& payload = {});
    static std::uint16_t gainWire(double db);
    static double gainDb(std::uint16_t);
    static std::uint16_t freqWire(double hz);
    static double freqHz(std::uint16_t);
    static std::uint16_t delayWire(double ms, bool output = true);
    static double delayMs(std::uint16_t);
    static std::uint16_t boolWire(bool enabled) { return enabled ? 1 : 0; }
    static std::optional<bool> boolean(const Frame&);
    static std::optional<std::uint16_t> qWire(double q);
    static std::optional<std::uint16_t> shapeWire(int shape);
    static std::string hex(const Bytes&);
};
// TCP can fragment/coalesce both frame kinds. Never scan envelope payloads for
// f7: an unverified init-dump layout cannot establish read-back evidence.
class StreamDecoder {
public:
    static constexpr std::size_t maxPayload = 1024 * 1024;
    std::vector<Bytes> feed(const Bytes&);
    void reset() { buffer.clear(); failed = false; }
    bool hasError() const { return failed; }
private:
    Bytes buffer;
    bool failed = false;
};
}
