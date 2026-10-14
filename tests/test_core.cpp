#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "fohcore/fohcore.h"
#include <cmath>
#include <random>
using namespace foh; using Catch::Approx;

static std::vector<float> pink(int n, uint32_t seed = 1) { PinkNoise p(seed); std::vector<float> v((size_t) n); for (auto& x : v) x = p.next(); return v; }
static std::vector<float> delayed(const std::vector<float>& x, int d, float g = 1.f) { std::vector<float> y(x.size(), 0.f); for (size_t i = (size_t) d; i < x.size(); ++i) y[i] = g * x[i - (size_t) d]; return y; }
static size_t binOf(const TFResult& r, double f) { return (size_t) std::lround(f / (r.sampleRate / r.fftSize)); }
// one-pole lowpass biquad-ish (RBJ LPF)
static std::vector<float> lowpass(const std::vector<float>& x, double fc, double fs) {
    double w = 2*M_PI*fc/fs, al = std::sin(w)/(2*0.7071), c = std::cos(w);
    double b0=(1-c)/2, b1=1-c, b2=(1-c)/2, a0=1+al, a1=-2*c, a2=1-al; double x1=0,x2=0,y1=0,y2=0;
    std::vector<float> y(x.size()); for (size_t i=0;i<x.size();++i){ double o=(b0*x[i]+b1*x1+b2*x2-a1*y1-a2*y2)/a0; x2=x1;x1=x[i];y2=y1;y1=o;y[i]=(float)o;} return y; }

TEST_CASE("FFT round trip and sine bin") {
    FFT f(1024); std::vector<cpx> d(1024); for (int i=0;i<1024;++i) d[(size_t)i]=std::sin(2*M_PI*10*i/1024.0);
    auto o=d; f.forward(d); REQUIRE(std::abs(d[10]) == Approx(512).margin(1e-6)); f.inverse(d);
    for (int i=0;i<1024;++i) REQUIRE(d[(size_t)i].real()==Approx(o[(size_t)i].real()).margin(1e-9));
    REQUIRE_THROWS(FFT(1000));
}
TEST_CASE("Pink noise slope is about -3 dB/oct") {
    auto x = pink(48000*20); RTA r(16384,48000,3); r.avgAlpha=0.02; r.process(x.data(),(int)x.size()); auto b=r.bandsDb(); auto& bands=r.bands();
    double a=0,c=0; for (size_t i=0;i<bands.size();++i){ if (std::fabs(bands[i].fCenter-250)<5) a=b[i]; if (std::fabs(bands[i].fCenter-4000)<50) c=b[i]; }
    // 1/3-oct bands of pink noise: equal energy per band => flat; RTA of pink should be flat within 1.5 dB
    REQUIRE(std::fabs(a-c) < 1.5);
}
TEST_CASE("Log sweep stays bounded and has expected length") {
    LogSweep s; s.prepare(48000,20,20000,2.0,0.1); s.setGainDb(0); REQUIRE(s.lengthSamples()==96000);
    float mx=0; for(int i=0;i<100800;++i) mx=std::max(mx,std::fabs(s.next())); REQUIRE(mx<=0.2501f); REQUIRE(mx>0.24f);
}
TEST_CASE("Ring buffer SPSC basic") {
    RingBuffer rb(1024); std::vector<float> a(600,1.f),b(600); rb.push(a.data(),600); rb.push(a.data(),600); // second dropped
    REQUIRE(rb.available()==600); REQUIRE(rb.pop(b.data(),1000)==600); REQUIRE(rb.available()==0);
}
TEST_CASE("Delay finder: integer and fractional known delays") {
    auto x = pink(48000); for (int d : {0, 37, 599, 2400}) {
        auto y = delayed(x, d, 0.5f); auto r = findDelay(x, y, 48000);
        REQUIRE(!r.arrivals.empty()); REQUIRE(r.arrivals[0].delaySamples == Approx(d).margin(0.1)); REQUIRE(r.confidence > 0.8); }
    auto y = delayed(x, 599); auto r = findDelay(x, y, 48000, false);
    REQUIRE(r.arrivals[0].delaySamples == Approx(599).margin(0.5));
    REQUIRE(r.arrivals[0].delayMs == Approx(12.479).margin(0.02));
}
TEST_CASE("Delay finder: top-3 arrivals (direct + 2 reflections) in noise") {
    auto x = pink(48000, 3); auto y = delayed(x, 480); auto r1 = delayed(x, 720, 0.5f), r2 = delayed(x, 1200, 0.3f);
    std::mt19937 g(7); std::normal_distribution<float> n(0, 0.01f);
    for (size_t i=0;i<y.size();++i) y[i]+=r1[i]+r2[i]+n(g);
    auto r = findDelay(x, y, 48000); REQUIRE(r.arrivals.size()==3);
    REQUIRE(r.arrivals[0].delaySamples==Approx(480).margin(0.5)); REQUIRE(r.arrivals[1].delaySamples==Approx(720).margin(0.5));
    REQUIRE(r.arrivals[2].delaySamples==Approx(1200).margin(0.5));
    // relative levels: GCC-PHAT distorts them, plain cross-correlation preserves them
    // (with white noise; pink's broad autocorrelation biases levels too)
    std::vector<float> w(48000); for (auto& v : w) v = n(g) * 30.f; auto wy = delayed(w, 480), wr = delayed(w, 720, 0.5f);
    for (size_t i=0;i<wy.size();++i) wy[i]+=wr[i];
    auto lin = findDelay(w, wy, 48000, false); REQUIRE(lin.arrivals.size()>=2); REQUIRE(lin.arrivals[1].relLevelDb==Approx(-6.02).margin(1.5));
}
TEST_CASE("Transfer function: pure gain + compensated delay -> flat, zero phase, coherence 1") {
    auto x = pink(48000*6); auto y = delayed(x, 300, 0.5f);
    TransferFunction tf(16384, 48000); tf.setReferenceDelaySamples(300); tf.setAverages(0); tf.process(x.data(), y.data(), (int)x.size());
    auto r = tf.result(); REQUIRE(r.averages >= 30);
    for (double f : {50.0, 100.0, 1000.0, 10000.0}) { auto k=binOf(r,f);
        REQUIRE(r.magDb[k]==Approx(-6.02).margin(0.1)); REQUIRE(std::fabs(r.phaseDeg[k])<1.0); REQUIRE(r.coherence[k]>0.99); }
}
TEST_CASE("Transfer function: uncompensated delay shows linear phase slope") {
    auto x = pink(48000*6); auto y = delayed(x, 48); // 1 ms
    TransferFunction tf(16384, 48000); tf.setAverages(0); tf.process(x.data(), y.data(), (int)x.size()); auto r = tf.result();
    REQUIRE(r.phaseDeg[binOf(r,250)]==Approx(-90).margin(3)); // -360*f*t
    REQUIRE(wrapDeg(r.phaseDeg[binOf(r,600)])==Approx(wrapDeg(-216)).margin(3));
}
TEST_CASE("Transfer function: lowpass filter recovered (-3 dB @ fc, -90 deg)") {
    auto x = pink(48000*8); auto y = lowpass(x, 1000, 48000);
    TransferFunction tf(16384, 48000); tf.setAverages(0); tf.process(x.data(), y.data(), (int)x.size()); auto r = tf.result();
    REQUIRE(r.magDb[binOf(r,100)]==Approx(0).margin(0.2)); REQUIRE(r.magDb[binOf(r,1000)]==Approx(-3.01).margin(0.3));
    REQUIRE(r.phaseDeg[binOf(r,1000)]==Approx(-90).margin(3)); REQUIRE(r.magDb[binOf(r,4000)]==Approx(-24.1).margin(1.0));
}
TEST_CASE("Coherence drops with uncorrelated noise (SNR 0 dB -> ~0.5)") {
    auto x = pink(48000*10, 5); auto nz = pink(48000*10, 99); std::vector<float> y(x.size()); for (size_t i=0;i<y.size();++i) y[i]=x[i]+nz[i];
    TransferFunction tf(16384, 48000); tf.setAverages(0); tf.process(x.data(), y.data(), (int)x.size()); auto r = tf.result(3);
    for (double f : {200.0, 2000.0}) REQUIRE(r.coherence[binOf(r,f)]==Approx(0.5).margin(0.12));
    // clean case for contrast
    TransferFunction t2(16384,48000); t2.setAverages(0); t2.process(x.data(), x.data(), (int)x.size()); REQUIRE(t2.result().coherence[binOf(r,2000)]>0.999);
}
TEST_CASE("Smoothing preserves flat and reduces ripple") {
    std::vector<double> p(8193); for (size_t i=0;i<p.size();++i) p[i]= 1.0 + 0.5*((i%2)?1:-1);
    auto s = smoothPower(p, 48000.0/16384, 6); REQUIRE(s[4000]==Approx(1.0).margin(0.01)); REQUIRE(smoothPower(p,1,0)==p);
}
TEST_CASE("RTA: 1 kHz full-scale sine reads ~0 dBFS in 1 kHz band (1/3 and 1/6)") {
    for (int frac : {3, 6}) { RTA r(16384,48000,frac); std::vector<float> s(48000*2); for (size_t i=0;i<s.size();++i) s[i]=(float)std::sin(2*M_PI*1000*i/48000.0);
        r.process(s.data(),(int)s.size()); auto b=r.bandsDb(); auto& bands=r.bands(); size_t k=0; for(size_t i=0;i<bands.size();++i) if(std::fabs(bands[i].fCenter-1000)<1) k=i;
        REQUIRE(b[k]==Approx(0).margin(0.5)); REQUIRE(b[k+2] < -40); }
    REQUIRE(octaveBands(3).size()==31); REQUIRE(octaveBands(6).size()==61);
}
TEST_CASE("Mic cal: synthetic ECM8000-style CSV (mixed separators, CRLF, comments, out-of-order row)") {
    auto c = loadMicCal(std::string(FOH_TEST_DATA) + "/synthetic-mic-cal.csv");
    REQUIRE(c.ok()); REQUIRE(c.hz.size()==77); REQUIRE(c.hasPhase); REQUIRE(c.sensitivityOffsetDb==Approx(-2.10));
    REQUIRE(std::is_sorted(c.hz.begin(), c.hz.end())); REQUIRE(c.hz.front()==5.0); REQUIRE(c.hz.back()==30000.0);
    REQUIRE(c.dbAt(5)==Approx(-2.234)); REQUIRE(c.dbAt(30000)==Approx(-2.468)); // ends
    REQUIRE(c.dbAt(2)==Approx(-2.234)); REQUIRE(c.dbAt(40000)==Approx(-2.468)); // clamped outside 5 Hz-30 kHz
    double mid = c.dbAt(std::sqrt(5.0*5.6123)); REQUIRE(mid==Approx((-2.234-2.191)/2).margin(1e-6));
}
TEST_CASE("Mic cal: tolerant parsing of .txt / .frd / headers / comments") {
    auto c = parseMicCal("\xEF\xBB\xBF\"Sens Factor =-1.5dB, SERNO: 123\"\n* comment\n# another\nFreq(Hz)\tSPL(dB)\tPhase\n\n20\t0.5\n100  0.0\r\n1000;1.0\n50,0.2,10\n");
    REQUIRE(c.ok()); REQUIRE(c.hz.size()==4); REQUIRE(!c.hasPhase); REQUIRE(c.hz[1]==50); REQUIRE(c.sensitivityOffsetDb==Approx(-1.5));
    auto frd = parseMicCal("; FRD file\n10.0 -1.0 5.0\n20000 2.0 -5.0\n"); REQUIRE(frd.ok()); REQUIRE(frd.hasPhase);
    REQUIRE(!parseMicCal("hello\nworld\n").ok());
}
TEST_CASE("Mic cal applied to TF removes the mic response") {
    auto c = loadMicCal(std::string(FOH_TEST_DATA) + "/synthetic-mic-cal.csv");
    // synthetic TF equal to the mic response
    TFResult tf; tf.sampleRate=48000; tf.fftSize=16384; size_t M=8193; tf.freq.resize(M); tf.H.resize(M); tf.magDb.resize(M); tf.phaseDeg.resize(M); tf.coherence.assign(M,1);
    for (size_t k=0;k<M;++k){ tf.freq[k]=k*48000.0/16384; tf.H[k]=std::polar(std::pow(10.0,c.dbAt(tf.freq[k])/20), c.phaseAt(tf.freq[k])*M_PI/180); }
    c.apply(tf); for (double f : {30.0, 1000.0, 15000.0}) { REQUIRE(tf.magDb[binOf(tf,f)]==Approx(0).margin(1e-6)); REQUIRE(tf.phaseDeg[binOf(tf,f)]==Approx(0).margin(1e-6)); }
    auto corr = c.correctionDb(8193, 48000.0/16384); REQUIRE(corr[binOf(tf,15000)]==Approx(-c.dbAt(binOf(tf,15000)*48000.0/16384)));
}
TEST_CASE("Trace store caps at 8") {
    TraceStore s; TFResult t; for (int i=0;i<8;++i) REQUIRE(s.store("T"+std::to_string(i),t,0)); REQUIRE(!s.store("X",t,0));
    REQUIRE(s.remove(0)); REQUIRE(s.count()==7); REQUIRE(s.at(0).name=="T1");
}
TEST_CASE("Guide: sub-to-mains phase offset and virtual sum") {
    // main = 1 (flat), sub = main delayed 2 ms; offset at 100 Hz = -72 deg -> implied +2 ms
    TFResult a; a.sampleRate=48000; a.fftSize=16384; size_t M=8193; a.freq.resize(M); a.H.resize(M); a.magDb.assign(M,0); a.phaseDeg.resize(M); a.coherence.assign(M,1);
    for (size_t k=0;k<M;++k){ a.freq[k]=k*48000.0/16384; a.H[k]=1; } TFResult b=a;
    for (size_t k=0;k<M;++k) b.H[k]=std::polar(1.0,-2*M_PI*a.freq[k]*0.002);
    auto p = phaseOffset(a,b,90,110); REQUIRE(p.impliedMs==Approx(2.0).margin(0.05));
    auto xo = acousticCrossover(a,b); REQUIRE(xo.found);
    auto s = virtualSum(a,a); REQUIRE(s.magDb[100]==Approx(6.02).margin(0.01));
    auto t = impulseFromTF(b); size_t pk=0; for(size_t i=0;i<t.size();++i) if(std::fabs(t[i])>std::fabs(t[pk])) pk=i; REQUIRE(pk==96);
}
TEST_CASE("Coherence blanking flags noisy bins only") {
    // DUT passes 0..~1.2 kHz cleanly (lowpass), heavy uncorrelated noise everywhere => HF coherence collapses
    auto x = pink(48000*8, 11); auto y = lowpass(x, 300, 48000); auto nz = pink(48000*8, 77);
    for (size_t i=0;i<y.size();++i) y[i] += 0.05f*nz[i];
    TransferFunction tf(16384, 48000); tf.setAverages(0);
    tf.process(x.data(), y.data(), (int)x.size());
    auto off = tf.result(6); REQUIRE(off.blanked.size()==off.freq.size());
    for (auto b : off.blanked) REQUIRE(b==0);
    tf.setCoherenceBlankThreshold(0.5); REQUIRE(tf.coherenceBlankThreshold()==0.5);
    auto r = tf.result(6); REQUIRE(r.blankThreshold==0.5);
    REQUIRE(!r.isBlanked(binOf(r,100))); REQUIRE(r.coherence[binOf(r,100)]>0.9);
    REQUIRE(r.isBlanked(binOf(r,8000))); REQUIRE(r.coherence[binOf(r,8000)]<0.5);
    for (size_t k=1;k<r.freq.size();++k) REQUIRE(r.isBlanked(k)==(r.coherence[k]<0.5));
    tf.setCoherenceBlankThreshold(2.0); REQUIRE(tf.coherenceBlankThreshold()==1.0);
}
#include <thread>
TEST_CASE("TripleBuffer: latest() returns newest published, never torn") {
    TripleBuffer<std::vector<int>> tb; REQUIRE(!tb.hasNew());
    tb.writeBuffer().assign(4, 1); tb.publish(); REQUIRE(tb.hasNew());
    tb.writeBuffer().assign(4, 2); tb.publish();
    REQUIRE(tb.latest()[0]==2); REQUIRE(!tb.hasNew()); REQUIRE(tb.latest()[0]==2);
    // concurrent stress: writer fills every element with i; reader must see uniform, nondecreasing values
    TripleBuffer<std::vector<int>> s; for (int k=0;k<3;++k){ s.writeBuffer().assign(256,0); s.publish(); }
    std::atomic<bool> done{false};
    std::thread w([&]{ for (int i=1;i<=20000;++i){ auto& b=s.writeBuffer(); b.assign(256,i); s.publish(); } done=true; });
    int last=0; bool ok=true;
    while(!done.load()){ auto& b=s.latest(); int v=b[0]; for(int x:b) if(x!=v) ok=false; if(v<last) ok=false; last=v; }
    w.join(); REQUIRE(ok); REQUIRE(s.latest()[0]==20000);
}
TEST_CASE("SnapshotPublisher carries TF/RTA and sequence") {
    SnapshotPublisher p; auto& a=p.beginWrite(); a.rtaDb={-10,-20}; a.tf.fftSize=16384; p.publish();
    auto& r=p.latest(); REQUIRE(r.seq==1); REQUIRE(r.rtaDb.size()==2); REQUIRE(r.tf.fftSize==16384);
    p.beginWrite().rtaDb={-1}; p.publish(); REQUIRE(p.latest().seq==2); REQUIRE(p.latest().rtaDb.size()==1);
}
TEST_CASE("Polarity flip: TF phase 180, coherence 1; delay finder flags inverted") {
    auto x = pink(48000*6, 21); auto y = delayed(x, 240, -1.f);
    TransferFunction tf(16384,48000); tf.setAverages(0); tf.setReferenceDelaySamples(240); tf.process(x.data(), y.data(), (int)x.size());
    auto r = tf.result();
    for (double f : {80.0, 1000.0, 8000.0}) { auto k=binOf(r,f);
        REQUIRE(std::fabs(r.phaseDeg[k]) == Approx(180).margin(1.0)); REQUIRE(r.magDb[k]==Approx(0).margin(0.1)); REQUIRE(r.coherence[k]>0.99); }
    for (bool phat : {true, false}) { auto d = findDelay(x, y, 48000, phat);
        REQUIRE(d.arrivals[0].delaySamples==Approx(240).margin(0.5)); REQUIRE(d.arrivals[0].inverted); }
    auto d2 = findDelay(x, delayed(x,240), 48000); REQUIRE(!d2.arrivals[0].inverted);
    // biquad DUT with flipped polarity: phase = LPF phase + 180
    auto lp = lowpass(x, 1000, 48000); for (auto& v : lp) v = -v;
    TransferFunction t2(16384,48000); t2.setAverages(0); t2.process(x.data(), lp.data(), (int)x.size());
    REQUIRE(wrapDeg(t2.result().phaseDeg[binOf(r,1000)])==Approx(90).margin(3));
}
TEST_CASE("Averaging: infinite = cumulative mean, exponential tracks changes") {
    // DUT gain steps 1.0 -> 0.5 halfway. Infinite: Sxy/Sxx -> mean gain 0.75. Exponential(4): -> 0.5.
    auto x = pink(48000*12, 4); std::vector<float> y(x.size()); for (size_t i=0;i<y.size();++i) y[i] = x[i] * (i < y.size()/2 ? 1.f : 0.5f);
    TransferFunction inf(16384,48000); inf.setAverages(0); inf.process(x.data(), y.data(), (int)x.size());
    TransferFunction ex(16384,48000); ex.setAverages(4); ex.process(x.data(), y.data(), (int)x.size());
    auto ri = inf.result(3), re = ex.result(3); auto k = binOf(ri, 1000);
    REQUIRE(ri.magDb[k]==Approx(20*std::log10(0.75)).margin(0.4));
    REQUIRE(re.magDb[k]==Approx(-6.02).margin(0.3));
    REQUIRE(ri.coherence[k] < re.coherence[k]); // non-stationary DUT costs coherence in the long average
    // both modes count every frame; only the weighting differs
    REQUIRE(ri.averages == ex.result().averages); REQUIRE(ri.averages > 60);
}
TEST_CASE("Smoothing 1/12 and 1/24: flat preserved, ripple reduced, resolution ordered") {
    double bin = 48000.0/16384; std::vector<double> p(8193);
    for (size_t i=0;i<p.size();++i) p[i] = 1.0 + 0.5*((i%2)?1:-1);
    for (int f : {12, 24}) { auto s = smoothPower(p, bin, f);
        REQUIRE(s[4000]==Approx(1.0).margin(0.01)); REQUIRE(s[1000]==Approx(1.0).margin(0.02)); }
    // narrow resonance at 1 kHz (Q~20): finer smoothing keeps more of the peak
    std::vector<double> q(8193); for (size_t i=1;i<q.size();++i){ double f=i*bin, d=(f/1000-1000/f)*20; q[i]=1+15/(1+d*d); }
    auto pk=[&](int fr){ auto s=smoothPower(q,bin,fr); return s[(size_t)std::lround(1000/bin)]; };
    REQUIRE(pk(24) > pk(12)); REQUIRE(pk(12) > pk(6)); REQUIRE(pk(6) > pk(3));
    REQUIRE(pk(24) < q[(size_t)std::lround(1000/bin)]);
    // complex smoothing at 1/24 keeps a flat TF exact
    std::vector<cpx> h(8193, cpx(0.5, 0.0)); auto hs = smoothComplex(h, bin, 24); REQUIRE(std::abs(hs[3000])==Approx(0.5).margin(1e-12));
}
TEST_CASE("Delay finder: plain cross-correlation (no PHAT) on white noise and a biquad DUT") {
    std::mt19937 g(42); std::normal_distribution<float> n(0, 0.3f); std::vector<float> w(48000); for (auto& v : w) v = n(g);
    for (int d : {0, 11, 480, 4800}) { auto r = findDelay(w, delayed(w, d, 0.7f), 48000, false);
        REQUIRE(r.arrivals[0].delaySamples==Approx(d).margin(0.3)); REQUIRE(r.confidence > 0.8); REQUIRE(!r.arrivals[0].inverted); }
    // biquad DUT adds group delay at LF, peak stays near the pure delay
    auto y = delayed(lowpass(w, 4000, 48000), 960); auto r = findDelay(w, y, 48000, false);
    REQUIRE(r.arrivals[0].delayMs==Approx(20.0).margin(0.15));
    // in heavy noise, confidence falls
    std::vector<float> noisy = delayed(w, 480, 0.05f); for (auto& v : noisy) v += n(g);
    REQUIRE(findDelay(w, noisy, 48000, false).confidence < r.confidence);
}
// ---- RT-safety: count heap allocations made while a flag is set on this thread
#include <new>
#include <cstdlib>
static thread_local bool g_trackAlloc = false; static thread_local int g_allocs = 0;
void* operator new(std::size_t n) { if (g_trackAlloc) ++g_allocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
struct AllocGuard { AllocGuard(){ g_allocs=0; g_trackAlloc=true; } ~AllocGuard(){ g_trackAlloc=false; } };
TEST_CASE("RT safety: audio-thread paths do not allocate (block sizes 16..2048)") {
    PinkNoise pn; LogSweep sw; sw.prepare(48000, 20, 20000, 1.0, 0.1); RingBuffer rb(1 << 16);
    TripleBuffer<std::vector<float>> tb; for (int k=0;k<3;++k){ tb.writeBuffer().assign(64,0.f); tb.publish(); }
    std::vector<float> blk(2048), out(2048);
    int allocs;
    { AllocGuard g; ScopedFlushDenormals fz;
      for (int bs : {16, 32, 64, 128, 256, 512, 1024, 2048}) for (int rep=0; rep<8; ++rep) {
          pn.process(blk.data(), bs); sw.process(blk.data(), bs); pn.setGainDb(-6); sw.setGainDb(-6); sw.reset();
          rb.push(blk.data(), bs); rb.pop(out.data(), bs); tb.publish(); (void) tb.latest(); }
      allocs = g_allocs; }
    REQUIRE(allocs == 0);
    // sanity: the counter works
    { AllocGuard g; std::vector<int> v(10); allocs = g_allocs; } REQUIRE(allocs >= 1);
}
TEST_CASE("RT safety: TF/RTA frames reuse preallocated scratch") {
    auto x = pink(16384*4); TransferFunction tf(16384,48000); RTA rta(16384,48000,3);
    tf.process(x.data(), x.data(), 16384); rta.process(x.data(), 16384); // warm up
    int allocs; { AllocGuard g; tf.process(x.data()+16384, x.data()+16384, 16384*2); rta.process(x.data()+16384, 16384*2); allocs = g_allocs; }
    REQUIRE(allocs == 0); REQUIRE(tf.frames() >= 4);
}
TEST_CASE("RingBuffer: wraparound, whole-block drop, pow2 rounding, drop counter") {
    RingBuffer rb(1000); REQUIRE(rb.capacity()==1024);
    std::vector<float> a(700), b(700); for (int i=0;i<700;++i) a[(size_t)i]=(float)i;
    REQUIRE(rb.push(a.data(),700)); REQUIRE(rb.pop(b.data(),700)==700);
    REQUIRE(rb.push(a.data(),700)); // wraps at 1024
    REQUIRE(!rb.push(a.data(),700)); REQUIRE(rb.dropped()==1); REQUIRE(rb.available()==700);
    std::fill(b.begin(), b.end(), -1.f); REQUIRE(rb.pop(b.data(),700)==700);
    for (int i=0;i<700;++i) REQUIRE(b[(size_t)i]==(float)i);
    REQUIRE(rb.push(a.data(),0)); REQUIRE(rb.pop(b.data(),0)==0);
    // concurrent SPSC stress: ramp survives intact
    RingBuffer s(4096); std::atomic<bool> done{false}; int expect=0; bool ok=true;
    std::thread prod([&]{ std::vector<float> blk(97); int v=0; while (v < 200000) { for (auto& x: blk) x=(float)(v++); while(!s.push(blk.data(),97)) std::this_thread::yield(); } done=true; });
    std::vector<float> o(500);
    while (!done.load() || s.available()) { int n=s.pop(o.data(),500); for (int i=0;i<n;++i){ if (o[(size_t)i]!=(float)expect) ok=false; ++expect; } }
    prod.join(); REQUIRE(ok); REQUIRE(expect >= 200000);
}
TEST_CASE("Denormals are flushed inside ScopedFlushDenormals") {
    volatile float tiny = 1e-38f; volatile float scale = 1e-3f;
    float before = tiny * scale; REQUIRE(before != 0.0f); // subnormal without FTZ
    float inside; { ScopedFlushDenormals fz; inside = tiny * scale; }
    REQUIRE(inside == 0.0f);
    float after = tiny * scale; REQUIRE(after != 0.0f); // restored
}
TEST_CASE("Generators: gain/reset are thread-safe requests honoured on next sample") {
    LogSweep sw; sw.prepare(48000, 20, 20000, 1.0, 0.0); std::vector<float> a(1000), b(1000);
    sw.process(a.data(), 1000); sw.reset(); sw.process(b.data(), 1000); REQUIRE(a == b);
    PinkNoise p1(9), p2(9); p2.setGainDb(-20); float x1=p1.next(), x2=p2.next(); REQUIRE(x2==Approx(x1*0.1f).epsilon(1e-4));
}
TEST_CASE("Convergence: clean DUT settles fast, noisy DUT later, timeout honoured") {
    auto x = pink(48000*30, 31); auto nz = pink(48000*30, 32);
    auto runUntil = [&](TransferFunction& tf, const std::vector<float>& y) {
        const int blk = 512; for (size_t i=0; i+blk<=x.size(); i+=blk) { tf.process(x.data()+i, y.data()+i, blk);
            auto c = tf.convergence(); if (c.settled || c.timedOut) return c; } return tf.convergence(); };
    ConvergenceSettings cs; cs.toleranceDb = 0.1; cs.minFrames = 8; cs.holdFrames = 6;
    auto clean = lowpass(x, 2000, 48000);
    TransferFunction a(16384,48000); a.setAverages(0); a.setConvergence(cs); auto ca = runUntil(a, clean);
    REQUIRE(ca.settled); REQUIRE(ca.frames < 20); REQUIRE(ca.progress==1.0); REQUIRE(ca.meanCoherence > 0.95);
    REQUIRE(a.frameSeconds()==Approx(0.17067).margin(1e-4));
    std::vector<float> noisy(x.size()); for (size_t i=0;i<x.size();++i) noisy[i]=clean[i]+nz[i];
    TransferFunction b(16384,48000); b.setAverages(0); b.setConvergence(cs); auto cb = runUntil(b, noisy);
    REQUIRE(cb.settled); REQUIRE(cb.frames > ca.frames); REQUIRE(cb.meanCoherence < ca.meanCoherence);
    // settled estimate is actually close to truth
    auto r = b.result(6); REQUIRE(r.magDb[binOf(r,500)]==Approx(0).margin(0.6));
    // timeout: impossible tolerance
    ConvergenceSettings tight = cs; tight.toleranceDb = 1e-9; tight.maxFrames = 12;
    TransferFunction c(16384,48000); c.setAverages(0); c.setConvergence(tight); auto cc = runUntil(c, noisy);
    REQUIRE(!cc.settled); REQUIRE(cc.timedOut); REQUIRE(cc.frames==12);
    // reset clears state; setting a reference delay resets too
    c.reset(); REQUIRE(c.convergence().frames==0); REQUIRE(!c.settled());
    // a DUT change mid-run breaks the stable streak
    std::vector<float> step(x.size()); for (size_t i=0;i<x.size();++i) step[i] = i < 48000*4 ? clean[i] : 0.3f*clean[i];
    TransferFunction e(16384,48000); e.setAverages(4); ConvergenceSettings ce=cs; ce.minFrames=1000; e.setConvergence(ce);
    e.process(x.data(), step.data(), 48000*4); int before = e.convergence().stableFrames;
    e.process(x.data()+48000*4, step.data()+48000*4, 16384); REQUIRE(before > 0); REQUIRE(e.convergence().stableFrames < before);
}
TEST_CASE("Delay finder for subs: 3 s LF-weighted window recovers sub delay under HF noise") {
    const double fs = 48000; int L = recommendedDelayWindowSamples(fs, true);
    REQUIRE(L == 144000); REQUIRE(recommendedDelayWindowSamples(fs, true, 10) == 192000);
    REQUIRE(recommendedDelayWindowSamples(fs, true, 1) == 96000); REQUIRE(recommendedDelayWindowSamples(fs, false) == 48000);
    auto x = pink(L, 51); auto nz = pink(L, 52);
    // sub = band-limited 25..100 Hz (two biquads), true delay 35 ms, equal-level uncorrelated broadband pink noise (mains/room)
    std::vector<float> hp(x.size()); { // 1st-order HP at 25 Hz
        double a = std::exp(-2*M_PI*25/fs), px=0, py=0; for (size_t i=0;i<x.size();++i){ double o=a*(py+x[i]-px); px=x[i]; py=o; hp[i]=(float)o; } }
    auto sub = delayed(lowpass(hp, 100, fs), 1680);
    std::vector<float> m(x.size()); for (size_t i=0;i<m.size();++i) m[i] = sub[i] + nz[i];
    auto lfOpt = DelayOptions::forSubs();
    auto r = findDelay(x, m, fs, lfOpt);
    REQUIRE(!r.arrivals.empty());
    // expect true delay + LPF group delay (2nd-order Butterworth at 100 Hz: ~2.2 ms at DC, less in-band)
    REQUIRE(r.arrivals[0].delayMs > 35.0); REQUIRE(r.arrivals[0].delayMs < 38.5);
    REQUIRE(!r.arrivals[0].inverted);
    for (auto& a : r.arrivals) REQUIRE(a.delayMs <= 500.0 + 0.1);
    // broadband search on the same capture is dominated by the noise
    auto bb = findDelay(x, m, fs, true);
    REQUIRE(r.confidence > 0.3); REQUIRE(r.confidence > bb.confidence);
    REQUIRE(std::fabs(bb.arrivals[0].delayMs - 35.0) > 5.0); // broadband locks onto noise here
    // the old 1 s window is still usable but less confident than 3 s
    std::vector<float> x1(x.begin(), x.begin()+48000), m1(m.begin(), m.begin()+48000);
    REQUIRE(findDelay(x1, m1, fs, lfOpt).confidence < r.confidence);
    // polarity-flipped sub is reported inverted
    for (auto& v : m) v = -v; auto ri = findDelay(x, m, fs, lfOpt); REQUIRE(ri.arrivals[0].inverted);
    REQUIRE(ri.arrivals[0].delayMs == Approx(r.arrivals[0].delayMs).margin(0.2));
}
TEST_CASE("UI bridge: AnalysisSnapshot carries RTA bands, cal, ref delay, convergence") {
    SnapshotPublisher p; RTA rta(16384,48000,6); TransferFunction tf(16384,48000); tf.setReferenceDelaySamples(480);
    auto x = pink(48000*3); tf.process(x.data(), x.data(), (int)x.size()); rta.process(x.data(), (int)x.size());
    { auto& s = p.beginWrite(); s.tf = tf.result(6); s.smoothingFraction = 6; s.rtaDb = rta.bandsDb(); s.rtaBands = rta.bands(); s.rtaFraction = 6;
      s.refDelaySamples = tf.referenceDelaySamples(); s.refDelayMs = s.refDelaySamples / 48.0; s.calApplied = true; s.setCalName("ECM8000 #12345");
      s.convergence = tf.convergence(); }
    p.publish(); const AnalysisSnapshot& r = p.latest();
    REQUIRE(r.rtaBands.size()==r.rtaDb.size()); REQUIRE(r.rtaBands.size() > 50);
    for (auto& b : r.rtaBands) { REQUIRE(b.fLo < b.fCenter); REQUIRE(b.fCenter < b.fHi); }
    REQUIRE(r.refDelayMs==Approx(10.0)); REQUIRE(r.calApplied); REQUIRE(std::string(r.calName)=="ECM8000 #12345");
    REQUIRE(r.convergence.frames==tf.frames()); REQUIRE(r.convergence.progress >= 0); REQUIRE(r.convergence.progress <= 1);
    AnalysisSnapshot t; t.setCalName(std::string(200,'a').c_str()); REQUIRE(std::string(t.calName).size()==63);
    t.setCalName(nullptr); REQUIRE(t.calName[0]==0);
}
TEST_CASE("UI bridge: SPSC command queue is POD, bounded, FIFO, allocation-free") {
    STATIC_REQUIRE(std::is_trivially_copyable<Command>::value);
    CommandQueue q; Command c; REQUIRE(!q.pop(c)); REQUIRE(q.size()==0);
    int allocs; { AllocGuard g;
        REQUIRE(q.push(Command::smoothing(12))); REQUIRE(q.push(Command::findDelay(true, 7)));
        REQUIRE(q.push(Command::blankThreshold(0.4))); allocs = g_allocs; }
    REQUIRE(allocs==0); REQUIRE(q.size()==3);
    REQUIRE(q.pop(c)); REQUIRE(c.type==CommandType::SetSmoothing); REQUIRE(c.i==12);
    REQUIRE(q.pop(c)); REQUIRE(c.type==CommandType::FindDelay); REQUIRE(c.sub); REQUIRE(c.id==7);
    REQUIRE(q.pop(c)); REQUIRE(c.type==CommandType::SetBlankThreshold); REQUIRE(c.d==0.4);
    for (int i=0;i<CommandQueue::capacity();++i) REQUIRE(q.push(Command::averages(i)));
    REQUIRE(!q.push(Command::resetAverages())); // full
    for (int i=0;i<CommandQueue::capacity();++i){ REQUIRE(q.pop(c)); REQUIRE(c.i==i); } REQUIRE(!q.pop(c));
    // two-thread stress (run under TSan in CI)
    CommandQueue s; const int N = 100000; std::thread prod([&]{ for (int i=0;i<N;){ if (s.push(Command::averages(i))) ++i; else std::this_thread::yield(); } });
    int expect=0; bool ok=true; while (expect<N){ Command o; if (s.pop(o)) { if (o.i!=expect) ok=false; ++expect; } }
    prod.join(); REQUIRE(ok);
}
TEST_CASE("UI bridge: commands drive the analysis objects end to end") {
    CommandQueue q; SnapshotPublisher p; TransferFunction tf(16384,48000); int smooth=0; uint64_t lastDelayId=0;
    auto x = pink(48000*4, 61); auto y = delayed(x, 480);
    q.push(Command::smoothing(24)); q.push(Command::blankThreshold(0.8)); q.push(Command::findDelay(false, 42));
    Command c; while (q.pop(c)) { // analysis-thread loop body
        switch (c.type) {
            case CommandType::SetSmoothing: smooth = c.i; break;
            case CommandType::SetBlankThreshold: tf.setCoherenceBlankThreshold(c.d); break;
            case CommandType::FindDelay: { auto& s = p.beginWrite();
                std::vector<float> r(x.begin(), x.begin()+recommendedDelayWindowSamples(48000, c.sub)), m(y.begin(), y.begin()+(long)r.size());
                s.delay = c.sub ? findDelay(r, m, 48000, DelayOptions::forSubs()) : findDelay(r, m, 48000, true);
                s.delaySeq = c.id; s.delayWasSub = c.sub; lastDelayId = c.id; break; }
            default: break; } }
    tf.process(x.data(), y.data(), (int)x.size());
    auto& s = p.beginWrite(); s.tf = tf.result(smooth); s.smoothingFraction = smooth; s.delaySeq = lastDelayId; p.publish();
    auto& r = p.latest(); REQUIRE(r.smoothingFraction==24); REQUIRE(r.tf.blankThreshold==0.8); REQUIRE(r.delaySeq==42);
    REQUIRE(!r.delayWasSub); REQUIRE(r.delay.arrivals[0].delaySamples==Approx(480).margin(0.5));
}
