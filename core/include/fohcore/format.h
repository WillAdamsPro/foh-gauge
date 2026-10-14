#pragma once
#include <string>

namespace foh {
// Console delay display (QA D-DSP): milliseconds rounded to 0.01 ms, half away
// from zero on the decimal value (4.805 -> "4.81", -4.805 -> "-4.81").
// Values are snapped to 0.1 us first: ties are detected to within 0.05 us,
// so 4.80495 also rounds to "4.81". Rounded zero has no minus sign.
// Non-finite values and magnitudes above 1e9 ms return the display sentinel "--".
// Message-thread only: std::string may allocate and snprintf may use locale locks.
// Do not call from the audio or analysis thread.
std::string formatConsoleDelayMs(double ms);
}
