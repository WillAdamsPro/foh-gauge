#pragma once
#include <array>
#include <cstddef>
#include <string_view>

namespace foh {
// Physical, zero-based channel indices. Immutable preset data; no device I/O.
struct InterfacePreset {
    const char* name;
    int micInput, referenceInput, loopbackOutput, consoleOutput;
};
const InterfacePreset* findInterfacePreset(std::string_view deviceName) noexcept;

enum class ConnectionResult { pending, pass, noSignal, swapInputs, invalid };
const char* connectionHint(ConnectionResult) noexcept;
// Fixed storage, suitable for the audio callback. Accumulates a one-second test
// window in the app; missing channels are silence. Detection uses RMS >= -60 dBFS
// and rejects an expected input more than 6 dB below another active input.
class ConnectionDetector {
public:
    static constexpr int maxInputs = 64;
    void reset(int expectedInput) noexcept;
    void process(const float* const* inputs, int channels, int samples) noexcept;
    ConnectionResult result() const noexcept;
private:
    std::array<double, maxInputs> energy {};
    std::size_t frames = 0;
    int expected = -1, channelCount = 0;
    bool invalid = false;
};
}
