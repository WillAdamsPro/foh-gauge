#pragma once
#include "fohcore/fohcore.h"

namespace fohui {
// Shared with the TF note so its wording can be tested without constructing a GUI.
inline const char* settleStatusText(const foh::ConvergenceState& convergence,
                                    bool captureActive, bool captureUnsettled) {
    if (convergence.timedOut || (!captureActive && captureUnsettled))
        return "\xe2\x96\xb2 unsettled";
    return convergence.settled ? "settled" : "averaging...";
}
}
