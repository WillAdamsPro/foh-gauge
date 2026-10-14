// Thin adapters for QA decisions whose core functions aren't on master yet. Each keeps one call site in
// the UI so swapping in Orion's fohcore function is a one-line change.
#pragma once
#include "fohcore/fohcore.h"
#include <cmath>

namespace qa {
// Console delay values are shown rounded to 0.01 ms.
inline double consoleMs(double ms) { return std::round(ms * 100.0) / 100.0; }

// Coherence traffic light over [lo, hi] Hz: 0 good (> 0.9), 1 fair (0.6-0.9), 2 poor (< 0.6).
// TODO(fohcore): replace with Orion's core traffic-light function once it lands on master.
struct CoherenceLight { int state = 2; double mean = 0; };
inline CoherenceLight coherenceLight(const foh::TFResult& tf, double lo = 200, double hi = 10000) {
    double s = 0; int n = 0;
    for (size_t k = 0; k < tf.freq.size() && k < tf.coherence.size(); ++k) if (tf.freq[k] >= lo && tf.freq[k] <= hi) { s += tf.coherence[k]; ++n; }
    CoherenceLight r; r.mean = n ? s / n : 0; r.state = r.mean > 0.9 ? 0 : r.mean >= 0.6 ? 1 : 2; return r;
}

// Settle state comes from fohcore's ConvergenceState (on master): settled / timedOut.
inline bool unsettled(const foh::ConvergenceState& c) { return !c.settled; }

// "Two similar arrivals": a second arrival within 3 dB of the first.
// TODO(fohcore): use the core's flag on DelayResult once Orion adds one.
inline bool twoSimilarArrivals(const foh::DelayResult& d) { return d.arrivals.size() > 1 && d.arrivals[1].relLevelDb > -3.0; }
}
