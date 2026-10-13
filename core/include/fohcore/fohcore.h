// FOH Gauge DSP core. JUCE-free, allocation-free on the audio-thread paths
// (generators, RingBuffer::push). Analysis runs on a worker/UI thread.
#pragma once
#include <atomic>
#include <complex>
#include <cstdint>
#include <string>
#include <vector>

namespace foh {
using cpx = std::complex<double>;

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
class PinkNoise {   // Paul Kellet refined filter on white noise, ~-3 dB/oct
public:
    explicit PinkNoise(uint32_t seed = 12345) : state(seed ? seed : 1) {}
    float next();
    void setGainDb(float db);
private:
    float white();
    uint32_t state;
    float b0=0,b1=0,b2=0,b3=0,b4=0,b5=0,b6=0;
    float gain = 0.1f;
};

class LogSweep {    // exponential sine sweep (Farina), loops with silence gap
public:
    void prepare(double sampleRate, double f1, double f2, double seconds, double gapSeconds = 0.5);
    float next();
    void reset() { pos = 0; }
    void setGainDb(float db);
    long lengthSamples() const { return sweepLen; }
private:
    double fs=48000, f1=20, f2=20000, T=5; long sweepLen=0, gapLen=0, pos=0; float gain=0.25f;
};

// ---------- Lock-free SPSC ring buffer (audio thread -> analysis thread) ----------
class RingBuffer {
public:
    explicit RingBuffer(int capacityPow2 = 1 << 18);
    void push(const float* data, int n);     // audio thread; drops on overflow
    int available() const;
    int pop(float* dest, int n);             // analysis thread
private:
    std::vector<float> buf; int mask;
    std::atomic<uint64_t> w{0}, r{0};
};

// ---------- Fractional-octave smoothing (log-frequency, power domain) ----------
// fraction: 3 => 1/3 oct, 6, 12, 24, 48; 0 => none.
std::vector<double> smoothPower(const std::vector<double>& power, double binHz, int fraction);
std::vector<cpx> smoothComplex(const std::vector<cpx>& h, double binHz, int fraction);

// ---------- Dual-channel transfer function, Welch averaged ----------
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
private:
    void frame();
    FFT fft; int N; double fs; int avgN = 16; int refDelay = 0; double blankThr = 0.0;
    std::vector<double> win; std::vector<float> xbuf, ybuf; int fill = 0;
    std::vector<float> refHistory; int histPos = 0;
    std::vector<double> Sxx, Syy; std::vector<cpx> Sxy; int frameCount = 0;
};

// ---------- Delay finder ----------
struct Arrival { double delaySamples; double delayMs; double distanceM; double relLevelDb; };
struct DelayResult { std::vector<Arrival> arrivals; double confidence = 0; std::vector<double> impulse; };
// Cross-correlation of meas vs ref (meas = ref delayed). Returns top 3 peaks with
// sub-sample (parabolic) interpolation. phat = GCC-PHAT weighting.
DelayResult findDelay(const std::vector<float>& ref, const std::vector<float>& meas, double sampleRate,
                      bool phat = true, int maxArrivals = 3, double minSeparationMs = 0.5,
                      double speedOfSound = 343.0);
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
    std::vector<double> rtaDb;  // per-band dBFS (matches RTA::bands())
    DelayResult delay;          // last delay-finder run (may be empty)
    double inputPeakDbRef = -120, inputPeakDbMeas = -120;
};
class SnapshotPublisher {
public:
    AnalysisSnapshot& beginWrite() { return tb.writeBuffer(); }   // analysis thread
    void publish() { tb.writeBuffer().seq = ++seq; tb.publish(); } // analysis thread
    const AnalysisSnapshot& latest() { return tb.latest(); }        // UI thread
    bool hasNew() const { return tb.hasNew(); }
private:
    TripleBuffer<AnalysisSnapshot> tb; uint64_t seq = 0;
};

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
