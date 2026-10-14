#include "fohcore/format.h"
#include <cmath>
#include <cstdio>

namespace foh {
std::string formatConsoleDelayMs(double ms) {
    if (!std::isfinite(ms) || std::fabs(ms) > 1e9) return "--";
    // Snap before rounding so decimal ties survive their binary representation.
    const long long tenthsUs = std::llround(std::fabs(ms) * 10000.0);
    const long long hundredthsMs = (tenthsUs + 50) / 100;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s%lld.%02lld",
                  (ms < 0 && hundredthsMs) ? "-" : "",
                  hundredthsMs / 100, hundredthsMs % 100);
    return buf;
}
}
