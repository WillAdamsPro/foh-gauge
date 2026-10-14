#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace foh::console {
using Bytes = std::vector<std::uint8_t>;
enum class Desk { sq, x32 };
enum class Bus { mix, matrix };
enum class Parameter { eqOn, frequency, gain, q, shape, delayOn, delayMs };
struct Target {
    Desk desk = Desk::x32;
    Bus bus = Bus::mix;
    int number = 1; // One based.
    int output = 0; // X32 delay: explicit output slot, never inferred from bus number.
};
struct Value {
    Target target;
    Parameter parameter;
    int band = 0; // One based for EQ bands, zero for enable/delay.
    double value = 0;
};
struct OscMessage { std::string address; char type = 0; double value = 0; };
struct SqFrame { std::uint8_t type; Bytes payload; };

// Control/worker thread only. These functions allocate; never call from audio callbacks.
bool validTarget(const Target&);
bool validValue(const Value&);
std::string describe(const Value&);
std::string x32Address(const Value&);
int x32Source(const Target&);
Bytes oscQuery(const std::string& address);
std::optional<OscMessage> parseOsc(const Bytes&);
std::optional<Value> decodeX32(const Target&, const Bytes&);
Bytes encodeWrite(const Value&); // Wire codec only; session transport enforces confirmation.
std::vector<Bytes> readRequests(const Target&);
Bytes sqFrame(std::uint8_t type, const Bytes& payload = {});
// SQ published MIDI does not include EQ/delay. MixPad uses TCP port 51326.
std::optional<Value> decodeSq(const Target&, const SqFrame&);
class SqFramer {
public:
    std::vector<SqFrame> push(const Bytes&);
    void reset() { buffer.clear(); }
private:
    Bytes buffer;
};
}
