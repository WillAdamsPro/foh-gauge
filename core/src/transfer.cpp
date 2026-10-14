#include "fohcore/fohcore.h"
#include <algorithm>
#include <cmath>
namespace foh {
double wrapDeg(double d) { d = std::fmod(d + 180.0, 360.0); if (d < 0) d += 360.0; return d - 180.0; }
TransferFunction::TransferFunction(int n, double sr) : fft(n), N(n), fs(sr), win(hannWindow(n)),
    xbuf((size_t) n), ybuf((size_t) n), refHistory(1 << 17), Sxx((size_t) n / 2 + 1), Syy((size_t) n / 2 + 1), Sxy((size_t) n / 2 + 1), X((size_t) n), Y((size_t) n) { resetConvergence(); }
double settleDeltaDb(const std::vector<double>& prev, const std::vector<double>& cur, const std::vector<double>& coh,
                     const std::vector<double>& f, const ConvergenceSettings& s, int* used) {
    size_t n = std::min(std::min(prev.size(), cur.size()), std::min(coh.size(), f.size()));
    double acc = 0; int cnt = 0, inBand = 0;
    for (size_t k = 0; k < n; ++k) {
        if (!(f[k] >= s.fLo && f[k] <= s.fHi)) continue; ++inBand;
        if (!(coh[k] >= s.minCoherence)) continue;
        double d = cur[k] - prev[k]; if (!std::isfinite(d)) continue;
        acc += d * d; ++cnt; }
    if (used) *used = cnt;
    if (cnt == 0 || cnt < s.minCoherentFraction * inBand) return std::numeric_limits<double>::quiet_NaN();
    return std::sqrt(acc / cnt);
}
void TransferFunction::resetConvergence() {
    double bin = fs / N; int M = N / 2;
    convLo = std::max(1, (int) std::ceil(conv.fLo / bin)); convHi = std::min(M, (int) std::floor(conv.fHi / bin));
    convPrevDb.assign((size_t) M + 1, 0.0); convHavePrev = false; settle = SettleDetector(conv); cstate = ConvergenceState{};
}
void TransferFunction::updateConvergence() {
    // allocation-free: per-bin raw (unsmoothed) estimate, compared with the previous update
    double acc = 0, csum = 0; int cnt = 0, nb = 0;
    for (int k = convLo; k <= convHi; ++k) {
        size_t i = (size_t) k; double sxx = Sxx[i], syy = Syy[i];
        double coh = (sxx * syy > 1e-40) ? std::min(1.0, std::norm(Sxy[i]) / (sxx * syy)) : 0.0;
        double db = 20.0 * std::log10(std::abs(sxx > 1e-30 ? Sxy[i] / sxx : cpx{}) + 1e-12);
        if (convHavePrev && coh >= conv.minCoherence) { double d = db - convPrevDb[i]; if (std::isfinite(d)) { acc += d * d; ++cnt; } }
        convPrevDb[i] = db; csum += coh; ++nb;
    }
    ConvergenceState& c = cstate; c.frames = frameCount; c.elapsedSeconds = frameCount * frameSeconds();
    c.meanCoherence = nb ? csum / nb : 0; c.coherentBins = cnt;
    bool enough = cnt > 0 && cnt >= conv.minCoherentFraction * nb;
    c.deltaDb = (convHavePrev && enough) ? std::sqrt(acc / cnt) : std::numeric_limits<double>::quiet_NaN();
    convHavePrev = true;
    if (!settle.finished()) settle.update(c.deltaDb, c.elapsedSeconds);
    c.stableFrames = settle.streakCount(); c.settled = settle.settled(); c.unsettled = c.timedOut = settle.unsettled();
    c.finished = settle.finished();
    c.progress = c.finished ? 1.0 : std::min(1.0, (double) c.stableFrames / std::max(1, conv.consecutive));
}
void TransferFunction::reset() { resetConvergence(); std::fill(Sxx.begin(), Sxx.end(), 0.0); std::fill(Syy.begin(), Syy.end(), 0.0);
    std::fill(Sxy.begin(), Sxy.end(), cpx{}); frameCount = 0; fill = 0; }
void TransferFunction::setReferenceDelaySamples(int d) { refDelay = std::clamp(d, 0, (int) refHistory.size() - 1); reset(); }
void TransferFunction::process(const float* ref, const float* meas, int n) {
    int H = (int) refHistory.size();
    for (int i = 0; i < n; ++i) {
        refHistory[(size_t) histPos] = ref[i];
        float delayed = refHistory[(size_t) ((histPos - refDelay + H) % H)];
        histPos = (histPos + 1) % H;
        xbuf[(size_t) fill] = delayed; ybuf[(size_t) fill] = meas[i];
        if (++fill == N) { frame(); // 50% overlap
            std::copy(xbuf.begin() + N / 2, xbuf.end(), xbuf.begin());
            std::copy(ybuf.begin() + N / 2, ybuf.end(), ybuf.begin()); fill = N / 2; }
    }
}
void TransferFunction::frame() {
    for (int i = 0; i < N; ++i) { X[(size_t) i] = xbuf[(size_t) i] * win[(size_t) i]; Y[(size_t) i] = ybuf[(size_t) i] * win[(size_t) i]; }
    fft.forward(X); fft.forward(Y);
    ++frameCount;
    double a = (avgN <= 0) ? 1.0 / frameCount : std::max(1.0 / frameCount, 1.0 / avgN);
    for (size_t k = 0; k <= (size_t) N / 2; ++k) {
        Sxx[k] += a * (std::norm(X[k]) - Sxx[k]);
        Syy[k] += a * (std::norm(Y[k]) - Syy[k]);
        Sxy[k] += a * (std::conj(X[k]) * Y[k] - Sxy[k]);
    }
    updateConvergence();
}
TFResult TransferFunction::result(int frac) const {
    TFResult r; r.sampleRate = fs; r.fftSize = N; r.averages = frameCount;
    size_t M = (size_t) N / 2 + 1; double bin = fs / N;
    auto sxx = smoothPower(Sxx, bin, frac), syy = smoothPower(Syy, bin, frac); auto sxy = smoothComplex(Sxy, bin, frac);
    r.freq.resize(M); r.H.resize(M); r.magDb.resize(M); r.phaseDeg.resize(M); r.coherence.resize(M); r.blanked.assign(M, 0); r.blankThreshold = blankThr;
    for (size_t k = 0; k < M; ++k) {
        r.freq[k] = k * bin;
        cpx h = sxx[k] > 1e-30 ? sxy[k] / sxx[k] : cpx{};
        r.H[k] = h; r.magDb[k] = 20.0 * std::log10(std::abs(h) + 1e-12);
        r.phaseDeg[k] = std::arg(h) * 180.0 / M_PI;
        double den = sxx[k] * syy[k]; r.coherence[k] = den > 1e-40 ? std::min(1.0, std::norm(sxy[k]) / den) : 0.0;
        if (blankThr > 0 && r.coherence[k] < blankThr) r.blanked[k] = 1;
    }
    return r;
}
// ---- traces & guide helpers
bool TraceStore::store(const std::string& name, const TFResult& tf, double ms) {
    if ((int) traces.size() >= kMax) return false; traces.push_back({name, tf, ms, true}); return true; }
bool TraceStore::remove(int i) { if (i < 0 || i >= count()) return false; traces.erase(traces.begin() + i); return true; }
Band2 acousticCrossover(const TFResult& a, const TFResult& b, double within, double fmin, double fmax) {
    Band2 out; size_t n = std::min(a.freq.size(), b.freq.size());
    for (size_t k = 1; k < n; ++k) { double f = a.freq[k]; if (f < fmin || f > fmax) continue;
        if (std::fabs(a.magDb[k] - b.magDb[k]) <= within) { if (!out.found) { out.lo = f; out.found = true; } out.hi = f; } }
    return out;
}
PhaseOffset phaseOffset(const TFResult& a, const TFResult& b, double lo, double hi) {
    cpx acc{}; double wsum = 0, fsum = 0; size_t n = std::min(a.H.size(), b.H.size());
    for (size_t k = 1; k < n; ++k) { double f = a.freq[k]; if (f < lo || f > hi) continue;
        double w = std::min(a.coherence[k], b.coherence[k]);
        cpx d = b.H[k] * std::conj(a.H[k]); double m = std::abs(d); if (m < 1e-20) continue;
        acc += w * d / m; wsum += w; fsum += w * f; }
    PhaseOffset p; if (wsum <= 0) return p;
    p.meanDeg = std::arg(acc) * 180.0 / M_PI; p.centerHz = fsum / wsum;
    p.impliedMs = -p.meanDeg / 360.0 / p.centerHz * 1000.0; // positive => b lags a
    return p;
}
CoherenceSummary coherenceSummary(const TFResult& tf, double lo, double hi) {
    CoherenceSummary c; double s = 0;
    for (size_t k = 1; k < tf.freq.size() && k < tf.coherence.size(); ++k)
        if (tf.freq[k] >= lo && tf.freq[k] <= hi) { s += tf.coherence[k]; ++c.bins; }
    if (c.bins) c.meanCoherence = s / c.bins;
    c.light = classifyCoherence(c.meanCoherence); return c;
}
TFResult virtualSum(const TFResult& a, const TFResult& b) {
    TFResult r = a; size_t n = std::min(a.H.size(), b.H.size());
    for (size_t k = 0; k < n; ++k) { r.H[k] = a.H[k] + b.H[k];
        r.magDb[k] = 20.0 * std::log10(std::abs(r.H[k]) + 1e-12); r.phaseDeg[k] = std::arg(r.H[k]) * 180.0 / M_PI;
        r.coherence[k] = std::min(a.coherence[k], b.coherence[k]);
        if (k < r.blanked.size()) r.blanked[k] = a.isBlanked(k) || b.isBlanked(k); }
    return r;
}
}
