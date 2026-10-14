#include "dsp_safety.h"
#include <algorithm>
#include <cmath>
namespace foh {
double wrapDeg(double d) { d = std::fmod(d + 180.0, 360.0); if (d < 0) d += 360.0; return d - 180.0; }
TransferFunction::TransferFunction(int n, double sr) : fft(detail::analysisSize(n)), N(n), fs(detail::sampleRate(sr)), win(hannWindow(n)),
    xbuf((size_t) n), ybuf((size_t) n), refHistory(1 << 17), Sxx((size_t) n / 2 + 1), Syy((size_t) n / 2 + 1), Sxy((size_t) n / 2 + 1), X((size_t) n), Y((size_t) n) { resetConvergence(); }
double settleDeltaDb(const std::vector<double>& prev, const std::vector<double>& cur, const std::vector<double>& coh,
                     const std::vector<double>& f, const ConvergenceSettings& s, int* used) {
    size_t n = std::min(std::min(prev.size(), cur.size()), std::min(coh.size(), f.size()));
    double acc = 0; int cnt = 0, inBand = 0;
    for (size_t k = 0; k < n; ++k) {
        if (!(f[k] >= s.fLo && f[k] <= s.fHi)) continue; ++inBand;
        if (!std::isfinite(coh[k]) || coh[k] < 0 || coh[k] > 1 || !(coh[k] >= s.minCoherence)) continue;
        double d = cur[k] - prev[k]; if (!std::isfinite(d)) continue;
        acc += d * d; ++cnt; }
    if (used) *used = cnt;
    if (cnt == 0 || !std::isfinite(acc) || cnt < s.minCoherentFraction * inBand) return std::numeric_limits<double>::quiet_NaN();
    return std::sqrt(acc / cnt);
}
void TransferFunction::resetConvergence() {
    double bin = fs / N; int M = N / 2;
    convLo = std::isfinite(conv.fLo) ? (int) std::clamp(std::ceil(conv.fLo / bin), 1.0, (double) M + 1) : M + 1;
    convHi = std::isfinite(conv.fHi) ? (int) std::clamp(std::floor(conv.fHi / bin), 0.0, (double) M) : 0;
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
    std::fill(Sxy.begin(), Sxy.end(), cpx{}); frameCount = 0; fill = 0; std::fill(refHistory.begin(), refHistory.end(), 0.f); histPos = 0; }
void TransferFunction::setReferenceDelaySamples(double d) {
    if (!std::isfinite(d)) d = 0;
    d = std::clamp(d, 0.0, (double) refHistory.size() - 1.0);
    refDelayInt = (int) std::floor(d); refDelayFrac = d - refDelayInt;
    if (refDelayInt >= (int) refHistory.size() - 1) refDelayFrac = 0;
    fracRot.resize((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) fracRot[(size_t) k] = std::polar(1.0, -2.0 * M_PI * k * refDelayFrac / N);
    reset();
}
void TransferFunction::process(const float* ref, const float* meas, int n) {
    ScopedFlushDenormals ftz;
    if (!ref || !meas || n <= 0) return;
    int H = (int) refHistory.size();
    for (int i = 0; i < n; ++i) {
        refHistory[(size_t) histPos] = detail::cleanSample(ref[i]);
        float delayed = refHistory[(size_t) ((histPos - refDelayInt + H) % H)];
        histPos = (histPos + 1) % H;
        xbuf[(size_t) fill] = delayed; ybuf[(size_t) fill] = detail::cleanSample(meas[i]);
        if (++fill == N) { frame(); // 50% overlap
            std::copy(xbuf.begin() + N / 2, xbuf.end(), xbuf.begin());
            std::copy(ybuf.begin() + N / 2, ybuf.end(), ybuf.begin()); fill = N / 2; }
    }
}
void TransferFunction::frame() {
    for (int i = 0; i < N; ++i) { X[(size_t) i] = xbuf[(size_t) i] * win[(size_t) i]; Y[(size_t) i] = ybuf[(size_t) i] * win[(size_t) i]; }
    fft.forward(X); fft.forward(Y);
    if (refDelayFrac != 0) for (int k = 0; k <= N / 2; ++k) X[(size_t) k] *= fracRot[(size_t) k];
    ++frameCount;
    double a = (avgN <= 0) ? 1.0 / frameCount : std::max(1.0 / frameCount, 1.0 / avgN);
    for (size_t k = 0; k <= (size_t) N / 2; ++k) {
        Sxx[k] += a * (std::norm(X[k]) - Sxx[k]);
        Syy[k] += a * (std::norm(Y[k]) - Syy[k]);
        Sxy[k] += a * (std::conj(X[k]) * Y[k] - Sxy[k]);
    }
    updateConvergence();
}
TFResult TransferFunction::result(int frac, const LoopbackCorrection* loopback) const {
    TFResult r; r.sampleRate = fs; r.fftSize = N; r.averages = frameCount;
    size_t M = (size_t) N / 2 + 1; double bin = fs / N;
    auto correctedXY = Sxy;
    auto correctedYY = Syy;
    if (loopback && loopback->valid() && loopback->sampleRate == fs && loopback->fftSize == N) {
        for (size_t k = 0; k < M; ++k) {
            correctedXY[k] /= loopback->H[k];
            correctedYY[k] /= std::norm(loopback->H[k]);
        }
    }
    auto sxx = smoothPower(Sxx, bin, frac), syy = smoothPower(correctedYY, bin, frac);
    auto sxy = smoothComplex(correctedXY, bin, frac);
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
        if (tf.freq[k] >= lo && tf.freq[k] <= hi) { s += std::isfinite(tf.coherence[k]) ? std::clamp(tf.coherence[k], 0.0, 1.0) : 0.0; ++c.bins; }
    if (c.bins) c.meanCoherence = s / c.bins;
    c.light = classifyCoherence(c.meanCoherence); return c;
}
SubDelayEstimate subDelayFromPhase(const TFResult& a, const TFResult& b, double lo, double hi, double xo, const DelayResult* hint) {
    SubDelayEstimate e;
    if (hint && !hint->arrivals.empty()) { e.hasHint = true; e.hintMs = hint->arrivals[0].delayMs; e.hintInverted = hint->arrivals[0].inverted; }
    if (!(lo > 0) || !(hi > lo)) return e;
    PhaseOffset p = phaseOffset(a, b, lo, hi); if (!(p.centerHz > 0)) return e;
    e.xoHz = xo > 0 ? xo : p.centerHz; e.periodMs = 1000.0 / e.xoHz;
    // weighted LS slope of unwrapped phase difference (deg) vs f, and phase at xo
    size_t n = std::min(a.H.size(), b.H.size()); std::vector<double> fv, ph, wv; double prev = 0, cs = 0; bool first = true;
    for (size_t k = 1; k < n && k < a.freq.size(); ++k) { double f = a.freq[k]; if (f < lo || f > hi) continue;
        cpx d = b.H[k] * std::conj(a.H[k]); if (std::abs(d) < 1e-20) continue;
        double w = std::min(a.coherence[k], b.coherence[k]); double v = std::arg(d) * 180.0 / M_PI;
        if (!first) { while (v - prev > 180) v -= 360; while (v - prev < -180) v += 360; } first = false; prev = v;
        fv.push_back(f); ph.push_back(v); wv.push_back(w); cs += w; }
    if (fv.size() < 2 || cs <= 0) return e;
    double sw = 0, sf = 0, sp = 0, sff = 0, sfp = 0;
    for (size_t i = 0; i < fv.size(); ++i) { sw += wv[i]; sf += wv[i]*fv[i]; sp += wv[i]*ph[i]; sff += wv[i]*fv[i]*fv[i]; sfp += wv[i]*fv[i]*ph[i]; }
    double den = sw * sff - sf * sf; if (std::fabs(den) < 1e-30) return e;
    double slope = (sw * sfp - sf * sp) / den, icpt = (sp - slope * sf) / sw; // deg per Hz
    e.slopeMs = -slope / 360.0 * 1000.0;
    e.phaseDeg = wrapDeg(icpt + slope * e.xoHz);
    e.wrappedMs = -e.phaseDeg / 360.0 * e.periodMs;
    double k = std::round((e.slopeMs - e.wrappedMs) / e.periodMs);
    e.primaryMs = e.wrappedMs + k * e.periodMs;
    e.alternativesMs[0] = e.primaryMs - e.periodMs; e.alternativesMs[1] = e.primaryMs + e.periodMs;
    e.coherence = cs / (double) fv.size(); e.valid = true;
    return e;
}
TFResult virtualSum(const TFResult& a, const TFResult& b) {
    // TFResult is public data: imported/synthetic callers need not have all
    // parallel arrays populated. Reject incomplete or incompatible inputs
    // before indexing them, just as the stored-trace preview does.
    const size_t n = a.freq.size();
    auto complete = [n](const TFResult& t) {
        return t.freq.size() == n && t.H.size() == n && t.magDb.size() == n
            && t.phaseDeg.size() == n && t.coherence.size() == n;
    };
    if (n == 0 || !complete(a) || !complete(b) || a.freq != b.freq) return {};
    TFResult r = a;
    for (size_t k = 0; k < n; ++k) { r.H[k] = a.H[k] + b.H[k];
        r.magDb[k] = 20.0 * std::log10(std::abs(r.H[k]) + 1e-12); r.phaseDeg[k] = std::arg(r.H[k]) * 180.0 / M_PI;
        r.coherence[k] = std::min(a.coherence[k], b.coherence[k]);
        if (k < r.blanked.size()) r.blanked[k] = a.isBlanked(k) || b.isBlanked(k); }
    return r;
}
}
