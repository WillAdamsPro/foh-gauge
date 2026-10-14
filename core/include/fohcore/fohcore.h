// FOH Gauge DSP core. JUCE-free, allocation-free on the audio-thread paths
// (generators, RingBuffer::push). Analysis runs on a worker/UI thread.
#pragma once
#include "fohcore/format.h"
#include <atomic>
#include <complex>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

// Mark functions callable from the audio thread. With clang >= 20 and
// -fsanitize=realtime, RealtimeSanitizer verifies them; elsewhere it is a no-op.
// Only enabled under RTSan: Apple clang (Xcode 26) rejects the attribute in leading position.
#if defined(__has_feature)
#  if __has_feature(realtime_sanitizer)
#    define FOH_RT [[clang::nonblocking]]
#  endif
#endif
#ifndef FOH_RT
#  define FOH_RT
#endif

namespace foh {
using cpx = std::complex<double>;

// RAII: flush denormals to zero (FTZ/DAZ) for the current thread; restores on exit.
// Put one at the top of the audio callback and of analysis loops.
class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept;
    ~ScopedFlushDenormals() noexcept;
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;
private:
    uint64_t saved = 0;
};

// ---------- FFT (radix-2, precomputed twiddles) ----------
class FFT {
public:
    // Power of two in [1, 2^24]; invalid sizes/storage throw invalid_argument.
    explicit FFT(int size);
    int size() const { return n; }
    void forward(std::vector<cpx>& data) const;   // in place
    void inverse(std::vector<cpx>& data) const;   // in place, scaled 1/N
private:
    void run(std::vector<cpx>& d, bool inv) const;
    int n;
    std::vector<cpx> tw;
    std::vector<int> rev;
};

std::vector<double> hannWindow(int n);
bool isPowerOfTwo(int n);

// ---------- Generators (real-time safe) ----------
// Threading: next()/process() on the audio thread only; setGainDb() from any thread (atomic).
class PinkNoise {   // Paul Kellet refined filter on white noise, ~-3 dB/oct
public:
    explicit PinkNoise(uint32_t seed = 12345) : state(seed ? seed : 1) {}
    float next() noexcept FOH_RT;
    void process(float* out, int n) noexcept FOH_RT { ScopedFlushDenormals ftz; if (out) for (int i = 0; i < n; ++i) out[i] = next(); }
    void setGainDb(float db) noexcept;
private:
    float white() noexcept;
    uint32_t state;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    std::atomic<float> gain{0.1f};
};

// Threading: prepare() only while the sweep is not running (it is not RT-safe w.r.t. next()).
// next()/process() on the audio thread; reset()/setGainDb() from any thread (atomic request).
class LogSweep {    // exponential sine sweep (Farina), loops with silence gap
public:
    // Invalid parameters disable the sweep. Finite gains clamp to [-240, 60] dB;
    // non-finite gains mute both generators.
    void prepare(double sampleRate, double f1, double f2, double seconds, double gapSeconds = 0.5);
    float next() noexcept FOH_RT;
    void process(float* out, int n) noexcept FOH_RT { ScopedFlushDenormals ftz; if (out) for (int i = 0; i < n; ++i) out[i] = next(); }
    void reset() noexcept { resetReq.store(true, std::memory_order_release); }
    void setGainDb(float db) noexcept;
    long lengthSamples() const { return sweepLen; }
private:
    double fs=48000, f1=20, f2=20000, T=5, K=1, phK=0, fadeN=480; long sweepLen=0, gapLen=0, pos=0;
    std::atomic<float> gain{0.25f}; std::atomic<bool> resetReq{false};
};

// ---------- Lock-free SPSC ring buffer (audio thread -> analysis thread) ----------
// Capacity is rounded up to a power of two. push() is wait-free, allocation-free and
// all-or-nothing: a block that does not fit is dropped whole and counted in dropped().
class RingBuffer {
public:
    explicit RingBuffer(int capacityPow2 = 1 << 18);
    bool push(const float* data, int n) noexcept FOH_RT;   // audio thread; false = dropped
    int available() const noexcept;
    int capacity() const noexcept { return (int) buf.size(); }
    int pop(float* dest, int n) noexcept;             // analysis thread
    uint64_t dropped() const noexcept { return drops.load(std::memory_order_relaxed); }
private:
    std::vector<float> buf; uint64_t mask;
    alignas(64) std::atomic<uint64_t> w{0};
    alignas(64) std::atomic<uint64_t> r{0};
    std::atomic<uint64_t> drops{0};
};

// ---------- Fractional-octave smoothing (log-frequency, power domain) ----------
// Non-finite values become zero; extreme components clamp to +/-1e100.
// fraction: 3 => 1/3 oct, 6, 12, 24, 48; 0 => none.
std::vector<double> smoothPower(const std::vector<double>& power, double binHz, int fraction);
std::vector<cpx> smoothComplex(const std::vector<cpx>& h, double binHz, int fraction);

// ---------- Dual-channel transfer function, Welch averaged ----------
// ---------- Coherence traffic light ----------
// Green: coherence >= 0.9. Amber: 0.6 <= coherence < 0.9. Red: < 0.6 (also NaN).
enum class TrafficLight : uint8_t { Red = 0, Amber = 1, Green = 2 };
constexpr double kCoherenceGreen = 0.9, kCoherenceAmber = 0.6;
constexpr TrafficLight classifyCoherence(double c) {
    return !(c >= 0 && c <= 1) ? TrafficLight::Red : c >= kCoherenceGreen ? TrafficLight::Green : (c >= kCoherenceAmber ? TrafficLight::Amber : TrafficLight::Red);
}
inline const char* trafficLightName(TrafficLight t) { return t == TrafficLight::Green ? "green" : t == TrafficLight::Amber ? "amber" : "red"; }

struct TFResult {
    double sampleRate = 48000; int fftSize = 0;
    std::vector<double> freq, magDb, phaseDeg, coherence;
    std::vector<cpx> H;
    // Coherence blanking: 1 where coherence (after smoothing) < blankThreshold.
    // Data is left intact; the UI should hide/gap blanked bins. Empty-safe: all 0 when off.
    std::vector<uint8_t> blanked;
    double blankThreshold = 0.0;
    int averages = 0;
    bool isBlanked(size_t k) const { return k < blanked.size() && blanked[k] != 0; }
    TrafficLight light(size_t k) const { return k < coherence.size() ? classifyCoherence(coherence[k]) : TrafficLight::Red; }
};
// Band summary for the status pill: mean coherence over [lo, hi] Hz and its light.
struct CoherenceSummary {
    double meanCoherence = 0; TrafficLight light = TrafficLight::Red; int bins = 0;
    // QaAdapters.h compatibility: state 0 good (green), 1 fair (amber), 2 poor (red); mean = meanCoherence
    int state() const { return light == TrafficLight::Green ? 0 : light == TrafficLight::Amber ? 1 : 2; }
    double mean() const { return meanCoherence; }
};
CoherenceSummary coherenceSummary(const TFResult& tf, double lo = 100.0, double hi = 10000.0);
// Band traffic light (replaces qa::coherenceLight). Default band 200 Hz-10 kHz as in the UI.
inline CoherenceSummary classifyCoherence(const TFResult& tf, double lo = 200.0, double hi = 10000.0) { return coherenceSummary(tf, lo, hi); }

// ---------- Settle rule (QA D5) ----------
// An "update" is one Welch frame (16k @ 48k, 50% overlap = 170.7 ms of audio).
// deltaDb = RMS over bins with coherence >= minCoherence and fLo <= f <= fHi of the
// change in |H| dB since the previous update. Settled when deltaDb < toleranceDb (strict)
// for `consecutive` updates in a row. If not settled after maxSeconds of audio, the
// capture proceeds with unsettled = true. No coherent bins => delta is NaN (never
// counts as stable, never divides by zero). Same if fewer than minCoherentFraction of the
// in-band bins are coherent.
struct ConvergenceSettings {
    double toleranceDb = 0.5; int consecutive = 3; double maxSeconds = 10.0;
    double minCoherence = 0.6; double fLo = 100.0, fHi = 10000.0;
    // Guard (Orion, pending QA sign-off): at least this fraction of the in-band bins must be
    // coherent, else delta is NaN. Stops a handful of chance-coherent bins (low SNR) or a
    // narrow surviving LF region (drifting DUT) from declaring "settled". 0 disables.
    double minCoherentFraction = 0.25;
    int maxFrames = 0;   // optional override of maxSeconds (frames); 0 = use maxSeconds
};
// RMS dB change over coherent in-band bins. Returns NaN when no bin qualifies.
double settleDeltaDb(const std::vector<double>& prevDb, const std::vector<double>& curDb,
                     const std::vector<double>& coherence, const std::vector<double>& freqHz,
                     const ConvergenceSettings& s = {}, int* binsUsed = nullptr);
// Pure streak counter: feed one delta per update (NaN allowed). Mirrors QA CAP0 exactly.
class SettleDetector {
public:
    explicit SettleDetector(const ConvergenceSettings& s = {}) : cfg(s) {}
    // Returns settled. elapsedSeconds is the audio time measured so far.
    bool update(double deltaDb, double elapsedSeconds = 0.0) {
        ++n; streak = (std::isfinite(deltaDb) && deltaDb >= 0 && deltaDb < cfg.toleranceDb) ? streak + 1 : 0;
        if (!done && streak > 0 && streak >= (cfg.consecutive > 0 ? cfg.consecutive : 1)) { done = true; isSettled = true; }
        bool timeout = cfg.maxFrames > 0 ? n >= cfg.maxFrames : (cfg.maxSeconds > 0 && elapsedSeconds >= cfg.maxSeconds);
        if (!done && timeout) { done = true; isUnsettled = true; }
        return isSettled;
    }
    void reset() { n = streak = 0; done = isSettled = isUnsettled = false; }
    bool settled() const { return isSettled; }
    bool unsettled() const { return isUnsettled; } // timed out without settling
    bool finished() const { return done; }         // capture may proceed
    int streakCount() const { return streak; }
    int updates() const { return n; }
    const ConvergenceSettings& settings() const { return cfg; }
private:
    ConvergenceSettings cfg; int n = 0, streak = 0; bool done = false, isSettled = false, isUnsettled = false;
};
struct ConvergenceState {
    bool settled = false;
    bool unsettled = false;    // hit maxSeconds without settling: proceed, show warning
    bool timedOut = false;     // == unsettled (kept for older callers)
    bool finished = false;     // settled || unsettled
    double deltaDb = std::numeric_limits<double>::quiet_NaN(); // last RMS change (NaN: no coherent bins)
    int coherentBins = 0;
    double meanCoherence = 0;  // over fLo..fHi
    int frames = 0, stableFrames = 0;
    double elapsedSeconds = 0;
    double progress = 0;       // 0..1 for a UI bar (streak/consecutive; 1 when finished)
};

class TransferFunction {
public:
    TransferFunction(int fftSize = 16384, double sampleRate = 48000.0);
    // FFT >= 2, finite sample rate >= 1 Hz; invalid configuration throws.
    // Both channels must share this sample clock; resample mismatched sources upstream.
    // Reconstruct on rate change (do not mix samples from different rates).
    // Non-finite/subnormal samples are treated as silence.
    // Feed blocks of reference (x) and measurement (y) samples; 50% overlap Welch.
    void process(const float* ref, const float* meas, int n);
    // Delay applied to the reference (positive = ref delayed, compensating acoustic delay).
    // Fractional (sub-sample) delay applied to the reference: positive = mic later than ref.
    // Integer part: sample history; fractional part: linear phase e^{-j w frac} on the
    // reference spectrum of every frame. Clamped to [0, 131071]. Resets the averages.
    void setReferenceDelaySamples(double d);
    double referenceDelaySamples() const { return refDelayInt + refDelayFrac; }
    double referenceDelayMs() const { return referenceDelaySamples() * 1000.0 / fs; }
    // averages: 0 = infinite (cumulative); otherwise exponential with ~N frame memory
    void setAverages(int n) { avgN = n; }
    // Bins with coherence below t (0..1) are flagged in TFResult::blanked. 0 disables.
    void setCoherenceBlankThreshold(double t) { blankThr = !std::isfinite(t) || t < 0 ? 0 : (t > 1 ? 1 : t); }
    double coherenceBlankThreshold() const { return blankThr; }
    void reset();
    TFResult result(int smoothingFraction = 0) const;
    int frames() const { return frameCount; }
    void setConvergence(const ConvergenceSettings& c) { conv = c; resetConvergence(); }
    const ConvergenceSettings& convergenceSettings() const { return conv; }
    // Max audio time to wait for the settle rule before proceeding "unsettled" (default 10 s).
    // Restarts the rule. <= 0 disables the timeout.
    void setConvergenceTimeout(double seconds) { conv.maxSeconds = seconds; conv.maxFrames = 0; resetConvergence(); }
    double convergenceTimeout() const { return conv.maxSeconds; }
    ConvergenceState convergence() const { return cstate; }
    bool settled() const { return cstate.settled; }
    double frameSeconds() const { return (N / 2) / fs; }
private:
    void frame();
    void updateConvergence();
    void resetConvergence();
    ConvergenceSettings conv; ConvergenceState cstate;
    SettleDetector settle; int convLo = 1, convHi = 0; std::vector<double> convPrevDb; bool convHavePrev = false;
    FFT fft; int N; double fs; int avgN = 16; int refDelayInt = 0; double refDelayFrac = 0; double blankThr = 0.0;
    std::vector<cpx> fracRot; // per-bin e^{-j 2 pi k frac / N}, k = 0..N/2
    std::vector<double> win; std::vector<float> xbuf, ybuf; int fill = 0;
    std::vector<float> refHistory; int histPos = 0;
    std::vector<double> Sxx, Syy; std::vector<cpx> Sxy; int frameCount = 0;
    std::vector<cpx> X, Y; // preallocated FFT scratch
};

// ---------- Delay finder ----------
// Sign convention: positive delay = mic (meas) later than reference.
// Timing comes from the chosen weighting (GCC-PHAT by default), refined to sub-sample
// precision on the band-limited correlation. relLevelDb is measured on a NON-PHAT
// correlation envelope (|analytic signal|) at each arrival's lag, relative to arrival #1
// (DelayOptions::levelMode), so levels are not distorted by PHAT whitening. Arrivals more
// than 26 dB below #1 on that envelope are dropped (PHAT artefacts).
struct Arrival { double delaySamples; double delayMs; double distanceM; double relLevelDb; bool inverted = false; // inverted: correlation peak negative (polarity flip)
                 double weightedRelLevelDb = 0; }; // level on the timing (PHAT) correlation, for diagnostics
// Two-spike rule (QA D4): flag <=> (#1 dB - #2 dB) <= 3 dB AND |t2 - t1| > 0.5 ms.
constexpr double kTwoSpikeWithinDb = 3.0, kTwoSpikeMinSeparationMs = 0.5;
constexpr bool twoSpikeFlag(double level1Db, double level2Db, double dtMs) {
    return (level1Db - level2Db) <= kTwoSpikeWithinDb && (dtMs < 0 ? -dtMs : dtMs) > kTwoSpikeMinSeparationMs;
}
struct DelayResult {
    std::vector<Arrival> arrivals; double confidence = 0; std::vector<double> impulse;
    bool twoSimilarArrivals = false;          // "Two similar spikes: is only one speaker on?" (confidence x0.75)
    int nonFiniteSamples = 0;       // NaN/Inf input samples, replaced by 0
    int samplesUsed = 0; bool truncated = false; // window is capped at 2^23 samples (~175 s @ 48k)
    std::string error;              // empty on success; set for unusable input (too short, bad rate, silent, all-NaN)
    bool ok() const { return error.empty() && !arrivals.empty(); }
};
bool twoSpikeFlag(const std::vector<Arrival>& arrivals); // applies the rule to arrivals[0] vs arrivals[1]
// Paired samples require a shared sample clock, as for TransferFunction.
// Cross-correlation of meas vs ref (meas = ref delayed). Returns top 3 peaks with
// sub-sample (parabolic) interpolation. phat = GCC-PHAT weighting.
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double sampleRate,
                      bool phat = true, int maxArrivals = 3, double minSeparationMs = 0.5,
                      double speedOfSound = 343.0);
// Options form. lpHz/hpHz apply a zero-phase band weighting (4th-order Butterworth
// magnitude) to the cross-spectrum, e.g. LF-weighting for subs. maxDelayMs bounds the lag
// search (0 = whole window). forSubs(): 20-150 Hz, GCC-PHAT, 10 ms arrival separation (> LF main lobe).
struct DelayOptions {
    bool phat = true; int maxArrivals = 3; double minSeparationMs = 0.5;
    double speedOfSound = 343.0; double hpHz = 0, lpHz = 0; double maxDelayMs = 0;
    // minDelayMs < 0 also searches negative lags (mic earlier than ref, e.g. swapped inputs) down to
    // minDelayMs; NaN or -Inf = symmetric (-window). Default 0 = positive lags only (unchanged).
    double minDelayMs = 0;
    // lpHz (if > 0) is floored at 10 Hz and both edges are clamped to Nyquist; hpHz >= lpHz is an error.
    // Level measurement for relLevelDb / two-spike: Unweighted = envelope of the plain
    // cross-correlation (Vega spec); RefWhitened = envelope of Y X* / |X|^2 (impulse-response
    // estimate; immune to the source spectrum, e.g. pink noise's broad autocorrelation).
    enum class LevelMode : uint8_t { Unweighted, RefWhitened };
    LevelMode levelMode = LevelMode::RefWhitened; // see PR notes: Unweighted mis-reads pink-noise spikes by ~2 dB
    static DelayOptions forSubs() { DelayOptions o; o.hpHz = 20; o.lpHz = 150; o.minSeparationMs = 10; o.maxDelayMs = 500; return o; }
};
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double sampleRate,
                      const DelayOptions& opt);
// Capture length the caller should use: 1 s for mains, 3 s for subs (LF needs
// many periods for a stable estimate; clamp to 2..4 s for sub work).
inline int recommendedDelayWindowSamples(double sampleRate, bool sub, double seconds = 0) {
    if (!std::isfinite(sampleRate) || sampleRate < 1) return 0;
    double s = seconds > 0 ? seconds : (sub ? 3.0 : 1.0);
    if (sub) s = s < 2.0 ? 2.0 : (s > 4.0 ? 4.0 : s);
    double samples = s * sampleRate;
    return samples >= (1 << 23) ? (1 << 23) : (int) samples;
}
// Impulse response from an averaged TF (IFFT of H). Empty if fftSize is not a power of two.
std::vector<double> impulseFromTF(const TFResult& tf);

// ---------- RTA (FFT-banded) ----------
struct Band { double fCenter, fLo, fHi; };
std::vector<Band> octaveBands(int fraction, double fmin = 20.0, double fmax = 20000.0); // base-10 IEC centres
class RTA {
public:
    // Same FFT/rate and sample sanitation contract as TransferFunction.
    RTA(int fftSize = 16384, double sampleRate = 48000.0, int fraction = 3);
    void setFraction(int f);
    void process(const float* x, int n);
    void reset();
    // dBFS per band (sine at full scale ~ 0 dB). Optional cal correction applied (dB per bin added).
    // Corrections clamp to +/-240 dB; non-finite corrections are ignored.
    std::vector<double> bandsDb() const;
    const std::vector<Band>& bands() const { return bandList; }
    void setCalibrationDb(const std::vector<double>& perBinDb) { calDb = perBinDb; }
    double avgAlpha = 0.25; // exponential averaging factor per frame
private:
    void frame();
    FFT fft; int N; double fs; int fraction;
    std::vector<double> win, power, calDb; std::vector<float> buf; int fill = 0; bool any = false;
    std::vector<Band> bandList; double winPowerSum = 0;
    std::vector<cpx> X; // preallocated FFT scratch
};

// ---------- Target traces (message/worker thread only; parser allocates) ----------
struct TargetTrace {
    std::vector<double> hz, db;
    double toleranceDb = 0;
    bool useTolerance = false;
    std::string error;
    bool ok() const { return hz.size() >= 2 && error.empty(); }
};
// Smaart four-column TF export or header + two-column target. Phase and coherence
// are validated but not used; unavailable Hz * * * bins are skipped. Frequencies are sorted; duplicate frequencies keep
// the first row. Invalid numeric rows fail the import rather than hiding errors.
TargetTrace parseTarget(const std::string& text);

// ---------- SPL calibration ----------
// Levels use the RTA convention: full-scale sine = 0 dBFS (RMS + 3.0103 dB).
// Sensitivity is relative to the value present during calibration; never double-count it.
class Json;
struct SplCal {
    double offsetDb = 0, sensitivityAtCalibrationDb = 0;
    bool calibrated = false;
    bool calibrate(double measuredDbfs, double knownSpl, double sensitivityDb = 0) noexcept;
    double level(double dbfs, double sensitivityDb = 0) const noexcept;
    Json toJson() const; // message/worker thread only
    static SplCal fromJson(const Json&);
    const char* unit() const noexcept { return calibrated ? "dB SPL" : "dBFS"; }
};

// ---------- Mic calibration ----------
struct MicCal {
    std::vector<double> hz, db, phaseDeg; bool hasPhase = false;
    // "Sens Factor" header line (miniDSP UMIK/REW style), e.g. "Sens Factor =-1.5dB, SERNO: 123".
    // Frequency/TF correction excludes sensitivity. SplCal applies it relative to calibration.
    double sensitivityOffsetDb = 0; bool hasSensFactor = false;
    std::string sensFactorLine;     // the raw header line, trimmed
    std::string error;
    bool ok() const { return hz.size() >= 2 && error.empty(); }
    double dbAt(double f) const;     // log-freq interpolation, clamped at ends
    double phaseAt(double f) const;
    // Per-bin correction for a spectrum (dB to ADD = -cal response).
    std::vector<double> correctionDb(int numBins, double binHz) const;
    // Apply to a TF result in place (divides by mic response, clamped to +/-240 dB).
    void apply(TFResult& tf) const;
};
MicCal parseMicCal(const std::string& text);
MicCal loadMicCal(const std::string& path);

// ---------- Snapshot out (analysis thread -> UI), lock-free triple buffer ----------
// Single writer, single reader, wait-free on both sides. The writer fills writeBuffer()
// then publish(); the reader calls latest() and gets the newest complete snapshot.
// The reference returned by latest() stays valid until the reader's next latest().
template <typename T>
class TripleBuffer {
public:
    T& writeBuffer() { return slots[writeIdx]; }
    void publish() {
        int prev = mid.exchange(writeIdx | kDirty, std::memory_order_acq_rel);
        writeIdx = prev & kIdx;
    }
    const T& latest() {
        if (mid.load(std::memory_order_relaxed) & kDirty) {
            int prev = mid.exchange(readIdx, std::memory_order_acq_rel);
            readIdx = prev & kIdx;
        }
        return slots[readIdx];
    }
    bool hasNew() const { return (mid.load(std::memory_order_acquire) & kDirty) != 0; }
private:
    static constexpr int kDirty = 4, kIdx = 3;
    T slots[3]{};
    int writeIdx = 0, readIdx = 1;
    std::atomic<int> mid{2};
    static_assert(std::atomic<int>::is_always_lock_free, "atomic<int> must be lock-free");
};

struct AnalysisSnapshot {
    uint64_t seq = 0;           // increments on every publish
    TFResult tf;                // smoothed + blanked TF
    int smoothingFraction = 0;  // smoothing applied to tf (0,3,6,12,24,48)
    std::vector<double> rtaDb;  // per-band dBFS, same order/size as rtaBands
    std::vector<Band> rtaBands; // RTA band centre + edges (fCenter, fLo, fHi)
    int rtaFraction = 3;
    DelayResult delay;          // last delay-finder run (may be empty)
    uint64_t delaySeq = 0;      // id of the FindDelay command that produced `delay` (0 = none)
    bool delayWasSub = false;
    double refDelaySamples = 0, refDelayMs = 0; // compensation currently applied to the reference
    bool calApplied = false;
    char calName[64] = {};      // NUL-terminated, truncated; empty when no cal
    uint64_t resetSeq = 0;      // id of the last ResetAverages command, acknowledges a fresh capture
    ConvergenceState convergence; // progress (0..1), settled, deltaDb, frames...
    double inputPeakDbRef = -120, inputPeakDbMeas = -120;
    double micLevelDbfs = -120;
    uint64_t inputEpoch = 0;
    int micChannel = -1;
    bool micLevelValid = false;
    void setCalName(const char* n) { size_t i = 0; if (n) for (; n[i] && i + 1 < sizeof(calName); ++i) calName[i] = n[i]; calName[i] = 0; }
};
// Lifetime of latest(): the returned reference stays valid (and unchanged) until the SAME
// reader thread calls latest() again. latest() is deliberately non-const: it claims the
// newest slot, so it mutates reader-side state, and only ONE reader thread (the UI) may call
// it. Copy out anything you need to keep across calls.
class SnapshotPublisher {
public:
    AnalysisSnapshot& beginWrite() { return tb.writeBuffer(); }   // analysis thread
    void publish() { tb.writeBuffer().seq = ++seq; tb.publish(); } // analysis thread
    const AnalysisSnapshot& latest() { return tb.latest(); }        // UI thread only
    bool hasNew() const { return tb.hasNew(); }                     // any thread
private:
    TripleBuffer<AnalysisSnapshot> tb; uint64_t seq = 0;
};

// ---------- Commands in (UI -> analysis thread), lock-free SPSC, fixed-size POD ----------
enum class CommandType : uint8_t { SetSmoothing, SetRtaFraction, FindDelay, SetBlankThreshold, SetAverages, ResetAverages, SetReferenceDelay };
struct Command {
    CommandType type = CommandType::ResetAverages;
    bool sub = false;        // FindDelay: true = subs (LF-weighted, 3 s), false = mains
    int32_t i = 0;           // SetSmoothing/SetRtaFraction: fraction; SetAverages: N (0 = infinite)
    double d = 0;            // SetBlankThreshold: 0..1; SetReferenceDelay: samples
    uint64_t id = 0;         // caller tag, echoed in delaySeq (FindDelay) or resetSeq (ResetAverages)
    static Command smoothing(int frac) { Command c; c.type = CommandType::SetSmoothing; c.i = frac; return c; }
    static Command rtaFraction(int frac) { Command c; c.type = CommandType::SetRtaFraction; c.i = frac; return c; }
    static Command findDelay(bool sub, uint64_t id = 0) { Command c; c.type = CommandType::FindDelay; c.sub = sub; c.id = id; return c; }
    static Command blankThreshold(double t) { Command c; c.type = CommandType::SetBlankThreshold; c.d = t; return c; }
    static Command averages(int n) { Command c; c.type = CommandType::SetAverages; c.i = n; return c; }
    static Command resetAverages(uint64_t id = 0) { Command c; c.type = CommandType::ResetAverages; c.id = id; return c; }
    static Command referenceDelay(double samples) { Command c; c.type = CommandType::SetReferenceDelay; c.d = samples; return c; }
};
static_assert(std::is_trivially_copyable<Command>::value, "Command must be POD");

// Bounded wait-free SPSC queue of trivially-copyable T. Storage is inline (no heap).
// push(): producer only, false when full. pop(): consumer only, false when empty.
template <typename T, int CapacityPow2 = 64>
class SpscQueue {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    static_assert(CapacityPow2 >= 2 && (CapacityPow2 & (CapacityPow2 - 1)) == 0, "capacity must be pow2");
public:
    bool push(const T& v) noexcept {
        uint64_t wi = w.load(std::memory_order_relaxed);
        if (wi - r.load(std::memory_order_acquire) >= (uint64_t) CapacityPow2) return false;
        slots[wi & (CapacityPow2 - 1)] = v; w.store(wi + 1, std::memory_order_release); return true;
    }
    bool pop(T& out) noexcept {
        uint64_t ri = r.load(std::memory_order_relaxed);
        if (ri == w.load(std::memory_order_acquire)) return false;
        out = slots[ri & (CapacityPow2 - 1)]; r.store(ri + 1, std::memory_order_release); return true;
    }
    int size() const noexcept { return (int) (w.load(std::memory_order_acquire) - r.load(std::memory_order_acquire)); }
    static constexpr int capacity() { return CapacityPow2; }
private:
    T slots[CapacityPow2]{};
    alignas(64) std::atomic<uint64_t> w{0};
    alignas(64) std::atomic<uint64_t> r{0};
};
using CommandQueue = SpscQueue<Command, 64>;

// ---------- Trace store ----------
struct Trace { std::string name; TFResult tf; double refDelayMs = 0; bool visible = true; };
class TraceStore {
public:
    static constexpr int kMax = 8;
    bool store(const std::string& name, const TFResult& tf, double refDelayMs); // false if full
    bool remove(int i);
    void clear() { traces.clear(); }
    int count() const { return (int) traces.size(); }
    Trace& at(int i) { return traces[(size_t) i]; }
    const Trace& at(int i) const { return traces[(size_t) i]; }
private:
    std::vector<Trace> traces;
};

// ---------- Guide helpers ----------
// Acoustic crossover band: frequencies where |magA - magB| <= withinDb, searched 30..300 Hz.
struct Band2 { double lo = 0, hi = 0; bool found = false; };
Band2 acousticCrossover(const TFResult& a, const TFResult& b, double withinDb = 10.0,
                        double fmin = 30.0, double fmax = 300.0);
// Mean phase difference (b - a, wrapped, degrees) over band, coherence-weighted, and implied delay.
struct PhaseOffset { double meanDeg = 0; double centerHz = 0; double impliedMs = 0; };
PhaseOffset phaseOffset(const TFResult& a, const TFResult& b, double lo, double hi);
// W2 sub delay from the crossover-band phase offset (QA D7). Positive = sub later than mains.
// primaryMs = wrapped phase-offset delay at xoHz, moved by whole periods to the candidate
// nearest the phase-slope (group-delay) estimate over [lo, hi]. alternativesMs = primary
// -/+ one period of xoHz (the guide shows all three; nothing is auto-picked). The raw
// impulse/xcorr peak is carried only as a hint (it includes filter group delay and can sit
// half a cycle away with flipped polarity, e.g. 20.5 ms "inverted" for a 12.5 ms sub).
struct SubDelayEstimate {
    bool valid = false;
    double xoHz = 0, periodMs = 0;     // crossover frequency used for the alternatives (N4: recorded)
    double phaseDeg = 0;               // sub - mains, wrapped, coherence weighted, at xoHz
    double wrappedMs = 0;              // -phase/360/xo, in (-period/2, period/2]
    double slopeMs = 0;                // group delay from the phase slope over the band
    double primaryMs = 0;
    double alternativesMs[2] = {0, 0}; // primary - period, primary + period
    double coherence = 0;              // mean min(coh_a, coh_b) over the band
    bool hasHint = false; double hintMs = 0; bool hintInverted = false; // raw peak, display only
};
SubDelayEstimate subDelayFromPhase(const TFResult& mains, const TFResult& sub, double lo, double hi,
                                   double xoHz = 0, const DelayResult* rawPeakHint = nullptr);
// Virtual (complex) sum of two traces.
TFResult virtualSum(const TFResult& a, const TFResult& b);
double wrapDeg(double d);
} // namespace foh
