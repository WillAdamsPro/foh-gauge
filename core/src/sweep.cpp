#include "fohcore/fohcore.h"
#include <algorithm>

namespace foh {
SweepResult deconvolveSweep(const std::vector<float>& ref, const std::vector<float>& mic, double fs) {
    SweepResult result;
    auto fail = [&](const char* error) { result.response.error = error; return result; };
    if (!std::isfinite(fs) || fs < 1) return fail("Invalid sweep sample rate");
    if (ref.size() != mic.size() || ref.size() < 64 || ref.size() > (1u << 22))
        return fail("Invalid sweep capture length");
    double refMean = 0, micMean = 0;
    for (size_t i = 0; i < ref.size(); ++i) {
        if (!std::isfinite(ref[i]) || !std::isfinite(mic[i])) return fail("Non-finite sweep capture");
        if (std::abs(ref[i]) >= 1 || std::abs(mic[i]) >= 1) return fail("Clipped sweep capture");
        refMean += ref[i]; micMean += mic[i];
    }
    refMean /= ref.size(); micMean /= mic.size();
    double refEnergy = 0, micEnergy = 0;
    int n = 1; while (size_t(n) < 2 * ref.size()) n *= 2;
    FFT fft(n); std::vector<cpx> x(n), y(n);
    for (size_t i = 0; i < ref.size(); ++i) {
        x[i] = double(ref[i]) - refMean; y[i] = double(mic[i]) - micMean;
        refEnergy += std::norm(x[i]); micEnergy += std::norm(y[i]);
    }
    if (refEnergy / ref.size() <= 1e-20) return fail("No loopback reference signal");
    if (micEnergy / mic.size() <= 1e-20) return fail("No measurement signal");
    fft.forward(x); fft.forward(y);
    double power = 0;
    for (auto v : x) power = std::max(power, std::norm(v));
    if (!(power > 1e-20)) return fail("No loopback reference signal");
    // Relative regularization limits amplification outside the excited sweep band.
    for (int k = 0; k < n; ++k) y[k] = y[k] * std::conj(x[k]) / (std::norm(x[k]) + power * 1e-8);
    x = y; fft.inverse(y);
    const size_t count = std::min(ref.size(), size_t(std::min(fs * 0.5 + 1, double(ref.size()))));
    auto& response = result.response;
    response.impulse.resize(count);
    size_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        response.impulse[i] = y[i].real();
        if (std::abs(y[i].real()) > std::abs(y[peak].real())) peak = i;
    }
    if (!(std::abs(y[peak].real()) > 1e-12)) { response.impulse.clear(); return fail("No measurement signal"); }
    double offset = 0;
    if (peak > 0 && peak + 1 < count) {
        double a = std::abs(y[peak-1].real()), b = std::abs(y[peak].real()), c = std::abs(y[peak+1].real());
        double denominator = a - 2*b + c;
        if (std::abs(denominator) > 1e-30) offset = std::clamp(0.5*(a-c)/denominator, -0.5, 0.5);
    }
    Arrival arrival{}; arrival.delaySamples = double(peak) + offset;
    arrival.delayMs = arrival.delaySamples / fs * 1000;
    arrival.distanceM = arrival.delaySamples / fs * 343;
    arrival.inverted = y[peak].real() < 0;
    response.arrivals.push_back(arrival); response.samplesUsed = int(ref.size());
    // Analytic impulse: keep DC/Nyquist, double positive frequencies, zero negatives.
    for (int k = 1; k < n/2; ++k) x[k] *= 2;
    for (int k = n/2 + 1; k < n; ++k) x[k] = 0;
    fft.inverse(x);
    double energy = 1e-30;
    for (size_t i = 0; i < count; ++i) energy = std::max(energy, std::norm(x[i]));
    result.etcDb.resize(count);
    for (size_t i = 0; i < count; ++i)
        result.etcDb[i] = std::max(-120.0, 10 * std::log10(std::max(1e-30, std::norm(x[i])) / energy));
    return result;
}
}
