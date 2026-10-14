#include "fohcore/fohcore.h"
#include <cmath>
#include "fohcore/json.h"
namespace foh {
bool SplCal::calibrate(double measured, double known, double sensitivity) noexcept {
    if (!std::isfinite(measured) || !std::isfinite(known) || !std::isfinite(sensitivity)
        || measured <= -100 || measured >= 0 || known <= 0 || known > 160) return false;
    offsetDb = known - measured; sensitivityAtCalibrationDb = sensitivity; calibrated = true;
    return true;
}
double SplCal::level(double dbfs, double sensitivity) const noexcept {
    return calibrated ? dbfs + offsetDb + sensitivity - sensitivityAtCalibrationDb : dbfs;
}
Json SplCal::toJson() const {
    if (!calibrated) return {};
    auto j = Json::object(); j.set("offsetDb", offsetDb); j.set("sensitivityDb", sensitivityAtCalibrationDb); return j;
}
SplCal SplCal::fromJson(const Json& j) {
    if (j["offsetDb"].type() != Json::Number || j["sensitivityDb"].type() != Json::Number) return {};
    double offset = j["offsetDb"].asNumber(), sens = j["sensitivityDb"].asNumber();
    if (!std::isfinite(offset) || !std::isfinite(sens) || offset <= 0 || offset >= 260 || std::abs(sens) > 200) return {};
    return {offset, sens, true};
}
}
