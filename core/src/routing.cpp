#include "fohcore/routing.h"
#include <algorithm>
#include <cmath>
namespace foh {
void ConnectionDetector::reset(int input) noexcept {
    energy.fill(0); frames = 0; expected = input; channelCount = 0; invalid = false;
}
void ConnectionDetector::process(const float* const* inputs, int channels, int samples) noexcept {
    if (samples <= 0) return;
    if (channels < 0 || channels > maxInputs) { invalid = true; return; }
    channelCount = std::max(channelCount, channels);
    frames += static_cast<std::size_t>(samples);
    for (int c = 0; c < channels; ++c) {
        if (!inputs || !inputs[c]) continue;
        for (int i = 0; i < samples; ++i) {
            const double s = inputs[c][i];
            if (!std::isfinite(s)) { invalid = true; continue; }
            energy[static_cast<std::size_t>(c)] += s * s;
        }
    }
}
ConnectionResult ConnectionDetector::result() const noexcept {
    if (invalid || expected < 0 || expected >= channelCount) return ConnectionResult::invalid;
    if (!frames) return ConnectionResult::noSignal;
    double other = 0;
    for (int c = 0; c < channelCount; ++c)
        if (c != expected) other = std::max(other, energy[static_cast<std::size_t>(c)]);
    const auto wanted = energy[static_cast<std::size_t>(expected)];
    const double threshold = static_cast<double>(frames) * 1e-6;
    if (other >= threshold && wanted < other * 0.25) return ConnectionResult::swapInputs;
    return wanted >= threshold ? ConnectionResult::pass : ConnectionResult::noSignal;
}
const char* connectionHint(ConnectionResult r) noexcept {
    switch (r) {
        case ConnectionResult::pass: return "Pass: signal reaches the expected input.";
        case ConnectionResult::swapInputs: return "Fail: signal is stronger on another input; swap inputs or check the channel choices.";
        case ConnectionResult::invalid: return "Fail: choose available input and output channels.";
        case ConnectionResult::noSignal: return "Fail: no signal on the expected input. Check the cable and input gain.";
        default: return "Testing connection...";
    }
}
}
