#include <catch2/catch_test_macros.hpp>
#include "fohcore/fohcore.h"
#include "fohcore/json.h"

namespace {
foh::TFResult syntheticInterface() {
    foh::TFResult tf; tf.sampleRate = 48000; tf.fftSize = 2048; tf.averages = 12;
    for (int k = 0; k <= tf.fftSize / 2; ++k) {
        double w = 2 * M_PI * k / tf.fftSize;
        auto h = (0.7 + 0.2 * std::exp(foh::cpx(0, -w))) * std::exp(foh::cpx(0, -w * 37.25));
        tf.freq.push_back(k * tf.sampleRate / tf.fftSize); tf.H.push_back(h);
        tf.magDb.push_back(20 * std::log10(std::abs(h))); tf.phaseDeg.push_back(std::arg(h) * 180 / M_PI);
        tf.coherence.push_back(1); tf.blanked.push_back(0);
    }
    return tf;
}
}
TEST_CASE("Stored interface response and fractional latency are removed", "[loopback]") {
    auto raw = syntheticInterface();
    auto c = foh::LoopbackCorrection::measure(raw); REQUIRE(c.valid());
    c = foh::LoopbackCorrection::fromJson(foh::Json::parse(c.toJson().dump())); REQUIRE(c.valid());
    auto tf = raw;
    for (size_t k = 0; k < tf.H.size(); ++k)
        tf.H[k] *= std::polar(std::pow(10., 3. / 20), -2 * M_PI * k * 81.5 / tf.fftSize);
    REQUIRE(c.apply(tf));
    for (size_t k = 1; k < tf.H.size(); ++k) {
        CHECK(std::abs(tf.magDb[k] - 3.) <= 0.1);
        CHECK(std::abs(tf.H[k] - std::polar(std::pow(10., 3. / 20), -2 * M_PI * k * 81.5 / tf.fftSize)) < 1e-9);
        CHECK(tf.coherence[k] == 1);
    }
    auto onlyInterface = raw; REQUIRE(c.apply(onlyInterface));
    for (size_t k = 1; k < tf.H.size(); ++k) {
        CHECK(std::abs(onlyInterface.magDb[k]) <= 0.1);
        CHECK(std::abs(onlyInterface.phaseDeg[k]) < 1e-9);
    }
}
TEST_CASE("Loopback rejects unusable captures and incompatible grids", "[loopback][edges]") {
    auto raw = syntheticInterface(); auto c = foh::LoopbackCorrection::measure(raw);
    auto changed = raw; changed.sampleRate = 96000; REQUIRE_FALSE(c.apply(changed)); CHECK(changed.H == raw.H);
    raw.coherence[10] = 0.5; CHECK_FALSE(foh::LoopbackCorrection::measure(raw).valid());
    raw = syntheticInterface(); raw.H[4] = 0; CHECK_FALSE(foh::LoopbackCorrection::measure(raw).valid());
    raw = syntheticInterface(); raw.H[4] = {NAN, 0}; CHECK_FALSE(foh::LoopbackCorrection::measure(raw).valid());
    CHECK_FALSE(foh::LoopbackCorrection::fromJson(foh::Json::object()).valid());
}
TEST_CASE("TF removes loopback before smoothing", "[loopback]") {
    foh::TransferFunction analyzer(2048, 48000); analyzer.setAverages(0);
    foh::PinkNoise noise(12); std::vector<float> ref(65536), mic(ref.size());
    for (size_t i = 0; i < ref.size(); ++i) { ref[i] = noise.next(); mic[i] = .7f * ref[i] + (i ? .2f * ref[i-1] : 0); }
    analyzer.process(ref.data(), mic.data(), (int)ref.size());
    auto c = foh::LoopbackCorrection::measure(analyzer.result()); REQUIRE(c.valid());
    auto corrected = analyzer.result(12, &c);
    for (size_t k = 1; k < corrected.H.size(); ++k) {
        CHECK(std::abs(corrected.magDb[k]) <= .1);
        CHECK(std::abs(corrected.phaseDeg[k]) < 1e-8);
        CHECK(corrected.coherence[k] > .99);
    }
}
