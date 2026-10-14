#pragma once
#include <array>
#include <cstdint>
namespace foh {
// Peak-referenced dBFS, matching SplCal; convert with SplCal::level on the UI.
struct LeqReading {
    std::array<double, 3> level {{-120, -120, -120}};
    std::array<double, 3> minute {{-120, -120, -120}};
    std::array<double, 3> fifteenMinutes {{-120, -120, -120}};
    std::array<double, 3> session {{-120, -120, -120}};
    double seconds = 0;
};
// A/C/Z in that order. Worker-thread processing; fixed storage, no process allocations.
// Rolling windows use 100 ms energy buckets (oldest partial bucket prorated).
class LeqMeter {
public:
    explicit LeqMeter(double sampleRate);
    void reset() noexcept;
    void process(const float*, int) noexcept;
    LeqReading reading() const noexcept;
private:
    struct Pole { double b0=0, b1=0, a1=0, x=0, y=0; double tick(double) noexcept; };
    std::array<std::array<Pole, 6>, 2> filters {};
    std::array<double, 2> gain {{1, 1}};
    using Energy = std::array<double, 3>;
    std::array<Energy, 9000> history {};
    Energy partial {}, total {}, recent {};
    double fs;
    int bucketSamples, fill=0, head=0, count=0, recentSamples=0;
    uint64_t samples=0;
    Energy window(int buckets) const noexcept;
};
enum class ExposurePreset { WHO, NIOSH };
struct ExposureWarning { bool exceeded = false; double dosePercent = 0; };
// Always uses calibrated A-weighted levels, independent of display weighting.
ExposureWarning exposureWarning(ExposurePreset, double laeq15, double laeqSession, double seconds) noexcept;
}
