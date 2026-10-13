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
TEST_CASE("Mic cal: Will's ECM8000 CSV") {
    auto c = loadMicCal(std::string(FOH_TEST_DATA) + "/ecm8000-cal.csv");
    REQUIRE(c.ok()); REQUIRE(c.hz.size()==124); REQUIRE(c.hasPhase);
    REQUIRE(c.dbAt(20)==Approx(-0.07)); REQUIRE(c.dbAt(21999)==Approx(-4.92)); REQUIRE(c.dbAt(5)==Approx(-0.07)); REQUIRE(c.dbAt(30000)==Approx(-4.92));
    double mid = c.dbAt(std::sqrt(20.0*25.1785)); REQUIRE(mid==Approx((-0.07+0.137)/2).margin(1e-6));
}
TEST_CASE("Mic cal: tolerant parsing of .txt / .frd / headers / comments") {
    auto c = parseMicCal("\xEF\xBB\xBF\"Sens Factor =-1.5dB, SERNO: 123\"\n* comment\n# another\nFreq(Hz)\tSPL(dB)\tPhase\n\n20\t0.5\n100  0.0\r\n1000;1.0\n50,0.2,10\n");
    REQUIRE(c.ok()); REQUIRE(c.hz.size()==4); REQUIRE(!c.hasPhase); REQUIRE(c.hz[1]==50); REQUIRE(c.sensitivityOffsetDb==Approx(-1.5));
    auto frd = parseMicCal("; FRD file\n10.0 -1.0 5.0\n20000 2.0 -5.0\n"); REQUIRE(frd.ok()); REQUIRE(frd.hasPhase);
    REQUIRE(!parseMicCal("hello\nworld\n").ok());
}
TEST_CASE("Mic cal applied to TF removes the mic response") {
    auto c = loadMicCal(std::string(FOH_TEST_DATA) + "/ecm8000-cal.csv");
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
