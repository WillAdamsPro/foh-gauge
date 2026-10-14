// FOH Gauge DSP core. JUCE-free, allocation-free on the audio-thread paths
// (generators, RingBuffer::push). Analysis runs on a worker/UI thread.
#pragma once
#include <atomic>
#include <complex>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

// Mark functions callable from the audio thread. With clang >= 20 and
// -fsanitize=realtime, RealtimeSanitizer verifies them; elsewhere it is a no-op.
#if defined(__has_cpp_attribute)
#  if __has_cpp_attribute(clang::nonblocking)
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
    FOH_RT float next() noexcept;
    FOH_RT void process(float* out, int n) noexcept { for (int i = 0; i < n; ++i) out[i] = next(); }
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
    void prepare(double sampleRate, double f1, double f2, double seconds, double gapSeconds = 0.5);
    FOH_RT float next() noexcept;
    FOH_RT void process(float* out, int n) noexcept { for (int i = 0; i < n; ++i) out[i] = next(); }
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
    FOH_RT bool push(const float* data, int n) noexcept;   // audio thread; false = dropped
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
// fraction: 3 => 1/3 oct, 6, 12, 24, 48; 0 => none.
std::vector<double> smoothPower(const std::vector<double>& power, double binHz, int fraction);
std::vector<cpx> smoothComplex(const std::vector<cpx>& h, double binHz, int fraction);

// ---------- Dual-channel transfer function, Welch averaged ----------
// ---------- Coherence traffic light ----------
// Green: coherence >= 0.9. Amber: 0.6 <= coherence < 0.9. Red: < 0.6 (also NaN).
enum class TrafficLight : uint8_t { Red = 0, Amber = 1, Green = 2 };
constexpr double kCoherenceGreen = 0.9, kCoherenceAmber = 0.6;
constexpr TrafficLight classifyCoherence(double c) {
    return c >= kCoherenceGreen ? TrafficLight::Green : (c >= kCoherenceAmber ? TrafficLight::Amber : TrafficLight::Red);
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

// "Wait until the average settles": after every Welch frame the TF is summarised on a
// 1/3-oct grid (fLo..fHi); delta = coherence-weighted mean |change in dB| vs the previous
// frame. Settled when delta < toleranceDb for holdFrames consecutive frames (and at least
// minFrames frames). maxFrames > 0 is a timeout (settled=false, timedOut=true).
// At 16k/48k with 50% overlap one frame = 170.7 ms.
struct ConvergenceSettings {
    double toleranceDb = 0.1; int minFrames = 8; int holdFrames = 6; int maxFrames = 0;
    double fLo = 40.0, fHi = 16000.0;
};
struct ConvergenceState {
    bool settled = false, timedOut = false;
    double deltaDb = 1e9;      // last frame-to-frame change
    double meanCoherence = 0;  // over the grid
    int frames = 0, stableFrames = 0;
    double progress = 0;       // 0..1 for a UI bar: stableFrames/holdFrames gated by minFrames
};

class TransferFunction {
public:
    TransferFunction(int fftSize = 16384, double sampleRate = 48000.0);
    // Feed blocks of reference (x) and measurement (y) samples; 50% overlap Welch.
    void process(const float* ref, const float* meas, int n);
    // Delay applied to the reference (positive = ref delayed, compensating acoustic delay).
    void setReferenceDelaySamples(int d);
    int referenceDelaySamples() const { return refDelay; }
    // averages: 0 = infinite (cumulative); otherwise exponential with ~N frame memory
    void setAverages(int n) { avgN = n; }
    // Bins with coherence below t (0..1) are flagged in TFResult::blanked. 0 disables.
    void setCoherenceBlankThreshold(double t) { blankThr = t < 0 ? 0 : (t > 1 ? 1 : t); }
    double coherenceBlankThreshold() const { return blankThr; }
    void reset();
    TFResult result(int smoothingFraction = 0) const;
    int frames() const { return frameCount; }
    void setConvergence(const ConvergenceSettings& c) { conv = c; resetConvergence(); }
    const ConvergenceSettings& convergenceSettings() const { return conv; }
    ConvergenceState convergence() const { return cstate; }
    bool settled() const { return cstate.settled; }
    double frameSeconds() const { return (N / 2) / fs; }
private:
    void frame();
    void updateConvergence();
    void resetConvergence();
    ConvergenceSettings conv; ConvergenceState cstate;
    std::vector<std::pair<int,int>> gridBins; std::vector<double> gridPrevDb; bool gridHavePrev = false;
    FFT fft; int N; double fs; int avgN = 16; int refDelay = 0; double blankThr = 0.0;
    std::vector<double> win; std::vector<float> xbuf, ybuf; int fill = 0;
    std::vector<float> refHistory; int histPos = 0;
    std::vector<double> Sxx, Syy; std::vector<cpx> Sxy; int frameCount = 0;
    std::vector<cpx> X, Y; // preallocated FFT scratch
};

// ---------- Delay finder ----------
struct Arrival { double delaySamples; double delayMs; double distanceM; double relLevelDb; bool inverted = false; }; // inverted: correlation peak negative (polarity flip)
struct DelayResult { std::vector<Arrival> arrivals; double confidence = 0; std::vector<double> impulse; };
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
    static DelayOptions forSubs() { DelayOptions o; o.hpHz = 20; o.lpHz = 150; o.minSeparationMs = 10; o.maxDelayMs = 500; return o; }
};
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double sampleRate,
                      const DelayOptions& opt);
// Capture length the caller should use: 1 s for mains, 3 s for subs (LF needs
// many periods for a stable estimate; clamp to 2..4 s for sub work).
inline int recommendedDelayWindowSamples(double sampleRate, bool sub, double seconds = 0) {
    double s = seconds > 0 ? seconds : (sub ? 3.0 : 1.0);
    if (sub) s = s < 2.0 ? 2.0 : (s > 4.0 ? 4.0 : s);
    return (int) (s * sampleRate);
}
// Impulse response from an averaged TF (IFFT of H).
std::vector<double> impulseFromTF(const TFResult& tf);

// ---------- RTA (FFT-banded) ----------
struct Band { double fCenter, fLo, fHi; };
std::vector<Band> octaveBands(int fraction, double fmin = 20.0, double fmax = 20000.0); // base-10 IEC centres
class RTA {
public:
    RTA(int fftSize = 16384, double sampleRate = 48000.0, int fraction = 3);
    void setFraction(int f);
    void process(const float* x, int n);
    void reset();
    // dBFS per band (sine at full scale ~ 0 dB). Optional cal correction applied (dB per bin added).
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

// ---------- Mic calibration ----------
struct MicCal {
    std::vector<double> hz, db, phaseDeg; bool hasPhase = false;
    double sensitivityOffsetDb = 0; // e.g. from "Sens Factor" header line
    std::string error;
    bool ok() const { return hz.size() >= 2 && error.empty(); }
    double dbAt(double f) const;     // log-freq interpolation, clamped at ends
    double phaseAt(double f) const;
    // Per-bin correction for a spectrum (dB to ADD = -cal response).
    std::vector<double> correctionDb(int numBins, double binHz) const;
    // Apply to a TF result in place (divides by mic response).
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
    ConvergenceState convergence; // progress (0..1), settled, deltaDb, frames...
    double inputPeakDbRef = -120, inputPeakDbMeas = -120;
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
    uint64_t id = 0;         // caller tag, echoed in AnalysisSnapshot::delaySeq for FindDelay
    static Command smoothing(int frac) { Command c; c.type = CommandType::SetSmoothing; c.i = frac; return c; }
    static Command rtaFraction(int frac) { Command c; c.type = CommandType::SetRtaFraction; c.i = frac; return c; }
    static Command findDelay(bool sub, uint64_t id = 0) { Command c; c.type = CommandType::FindDelay; c.sub = sub; c.id = id; return c; }
    static Command blankThreshold(double t) { Command c; c.type = CommandType::SetBlankThreshold; c.d = t; return c; }
    static Command averages(int n) { Command c; c.type = CommandType::SetAverages; c.i = n; return c; }
    static Command resetAverages() { Command c; c.type = CommandType::ResetAverages; return c; }
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
// Virtual (complex) sum of two traces.
TFResult virtualSum(const TFResult& a, const TFResult& b);
double wrapDeg(double d);
} // namespace foh
