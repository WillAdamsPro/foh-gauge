#include "fohcore/leq.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
namespace foh {
double LeqMeter::Pole::tick(double v) noexcept { double out=b0*v+b1*x-a1*y; x=v; y=out; return out; }
LeqMeter::LeqMeter(double rate) : fs(rate), bucketSamples(1) {
    if (!std::isfinite(rate) || rate < 8000 || rate > 384000) throw std::invalid_argument("Invalid Leq sample rate");
    bucketSamples = int(std::round(fs / 10));
    // Bilinear transform of the standard analog weighting poles; normalize at 1 kHz.
    for (int w=0; w<2; ++w) {
        const double poles[6] = {20.598997,20.598997,107.65265,737.86223,12194.217,12194.217};
        std::complex<double> response=1, z=std::polar(1.0, -2*3.141592653589793*1000/fs);
        for (int i=0; i<6; ++i) {
            auto& p=filters[w][i];
            if (w==1 && (i==2 || i==3)) { p.b0=1; continue; }
            double k=2*fs, a=2*3.141592653589793*poles[i];
            p.a1=(a-k)/(a+k); p.b0=i<4 ? k/(a+k) : a/(a+k); p.b1=i<4 ? -p.b0 : p.b0;
            response *= (p.b0+p.b1*z)/(1.0+p.a1*z);
        }
        gain[w]=1/std::abs(response);
    }
}
void LeqMeter::reset() noexcept {
    history={}; partial={}; total={}; recent={}; fill=head=count=recentSamples=0; samples=0;
    for (auto& f:filters) for (auto& p:f) p.x=p.y=0;
}
void LeqMeter::process(const float* data, int n) noexcept {
    if (!data || n<=0) return;
    for (int i=0; i<n; ++i) {
        double x=std::isfinite(data[i]) ? std::clamp(double(data[i]), -1.0, 1.0) : 0;
        Energy values {{x,x,x}};
        for (int w=0; w<2; ++w) { for (auto& p:filters[w]) values[w]=p.tick(values[w]); values[w]*=gain[w]; }
        for (int w=0; w<3; ++w) { double e=2*values[w]*values[w]; partial[w]+=e; total[w]+=e; }
        ++samples;
        if (++fill==bucketSamples) { recent=partial; recentSamples=bucketSamples; history[head]=partial; head=(head+1)%9000; count=std::min(count+1,9000); partial={}; fill=0; }
    }
}
LeqMeter::Energy LeqMeter::window(int buckets) const noexcept {
    Energy energy=partial;
    int available=std::min(count,buckets);
    for (int i=0; i<available; ++i) {
        double fraction = i==buckets-1 ? 1.0-double(fill)/bucketSamples : 1;
        const auto& e=history[(head-1-i+9000)%9000];
        for (int w=0; w<3; ++w) energy[w]+=e[w]*fraction;
    }
    double denom=double(std::min<uint64_t>(samples,uint64_t(buckets)*bucketSamples));
    for (auto& e:energy) e=denom>0 ? 10*std::log10(std::max(e/denom,1e-12)) : -120;
    return energy;
}
LeqReading LeqMeter::reading() const noexcept {
    LeqReading r; r.seconds=double(samples)/fs; r.minute=window(600); r.fifteenMinutes=window(9000);
    for (int w=0; w<3; ++w) {
        r.session[w]=samples ? 10*std::log10(std::max(total[w]/samples,1e-12)) : -120;
        r.level[w]=recentSamples ? 10*std::log10(std::max(recent[w]/recentSamples,1e-12)) : -120;
    }
    return r;
}
ExposureWarning exposureWarning(ExposurePreset p, double fifteen, double session, double seconds) noexcept {
    if (!std::isfinite(fifteen) || !std::isfinite(session) || !std::isfinite(seconds) || seconds<=0) return {};
    // NIOSH: 85 dBA / 8 h with a 3 dB exchange rate; WHO: 100 dBA LAeq,15min.
    double dose=100*seconds/(8*3600)*std::pow(2.0,(session-85)/3);
    return {p==ExposurePreset::WHO ? fifteen>=100 : dose>=100, dose};
}
}
