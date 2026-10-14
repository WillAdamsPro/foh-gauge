// Message-thread comparison only; reuses the existing analysis snapshot.
#pragma once
#include <fohcore/fohcore.h>
#include <algorithm>
#include <cmath>

namespace fohui {
struct ShowDrift {
    bool valid = false;
    double rmsDb = 0;
};
inline ShowDrift showDrift(const foh::TFResult& live, const foh::TFResult& reference) {
    ShowDrift result;
    if (live.sampleRate != reference.sampleRate || live.fftSize != reference.fftSize) return result;
    auto sample = [](const foh::TFResult& tf, double frequency, double& value) {
        auto it = std::lower_bound(tf.freq.begin(), tf.freq.end(), frequency);
        if (it == tf.freq.end() || it == tf.freq.begin()) return false;
        const size_t hi = size_t(it - tf.freq.begin()), lo = hi - 1;
        if (hi >= tf.magDb.size() || hi >= tf.coherence.size()) return false;
        for (auto i : {lo, hi})
            if (tf.isBlanked(i) || !std::isfinite(tf.coherence[i]) || tf.coherence[i] < 0.6
                || !std::isfinite(tf.magDb[i])) return false;
        const double span = tf.freq[hi] - tf.freq[lo];
        if (!(span > 0)) return false;
        value = tf.magDb[lo] + (tf.magDb[hi] - tf.magDb[lo]) * (frequency - tf.freq[lo]) / span;
        return std::isfinite(value);
    };
    int eligible = 0, coherent = 0;
    double energy = 0;
    // Equal log-frequency weighting avoids high-frequency FFT-bin dominance.
    for (double f = 100; f <= 10000; f *= std::pow(2.0, 1.0 / 12.0)) {
        ++eligible;
        double a, b;
        if (!sample(live, f, a) || !sample(reference, f, b)) continue;
        energy += (a - b) * (a - b); ++coherent;
    }
    result.valid = coherent >= (eligible + 3) / 4;
    if (result.valid) result.rmsDb = std::sqrt(energy / coherent);
    return result;
}
struct ShowDriftAlert {
    int consecutive = 0;
    bool update(ShowDrift drift) {
        consecutive = drift.valid && drift.rmsDb >= 3.0 ? std::min(3, consecutive + 1) : 0;
        return consecutive == 3;
    }
};
}
