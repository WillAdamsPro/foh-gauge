#include <catch2/catch_test_macros.hpp>
#include "Widgets.h"
#include <limits>

namespace {
constexpr double qnan = std::numeric_limits<double>::quiet_NaN();
constexpr double inf = std::numeric_limits<double>::infinity();
juce::Image render(PlotComponent& plot) {
    juce::Image image(juce::Image::ARGB, 640, 320, true);
    juce::Graphics graphics(image); plot.setSize(640, 320); plot.paint(graphics); return image;
}
bool samePixels(const juce::Image& a, const juce::Image& b) {
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x)
        if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) return false;
    return true;
}
}

TEST_CASE("TF view decimation rejects mismatched FFT grids and gaps corrupt bins", "[app][edges]") {
    foh::TFResult tf; tf.fftSize = 1024; tf.sampleRate = 48000;
    std::vector<double> values(513, 0), f, v; std::vector<uint8_t> gaps;
    logDecimate(tf, values, f, v, &gaps); REQUIRE_FALSE(f.empty());
    for (double bad : {qnan, inf, -inf, std::numeric_limits<double>::denorm_min()}) {
        std::fill(values.begin(), values.end(), bad);
        logDecimate(tf, values, f, v, &gaps); REQUIRE_FALSE(gaps.empty());
        for (auto gap : gaps) REQUIRE(gap == 1);
        for (auto value : v) REQUIRE(std::isfinite(value));
        logDecimate(tf, values, f, v); REQUIRE(f.empty());
    }
    for (int size : {0, -1, 3, 512, 2048}) {
        tf.fftSize = size; logDecimate(tf, values, f, v, &gaps);
        REQUIRE(f.empty()); REQUIRE(v.empty()); REQUIRE(gaps.empty());
    }
    tf.fftSize = 1024;
    for (double rate : {0., -1., qnan, inf}) {
        tf.sampleRate = rate; logDecimate(tf, values, f, v, &gaps); REQUIRE(f.empty());
    }
}

TEST_CASE("Magnitude phase RTA and target painting skips invalid or incomplete points", "[app][edges]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    for (bool bars : {false, true}) {
        PlotComponent plot; plot.bars = bars; plot.wrapPhase = !bars;
        auto empty = render(plot);
        PlotComponent::Series series; series.c = juce::Colours::red;
        series.f = {100, 200, 1000}; series.v = {qnan, inf}; plot.series = {series};
        plot.barLo = {80, 180}; plot.barHi = {120};
        PlotComponent::ToleranceBand band; band.f = {20, 20000}; band.lower = {}; band.upper = {3};
        plot.toleranceBands = {band};
        REQUIRE(samePixels(empty, render(plot)));
        band.lower = {-3, -3}; band.upper = {3, qnan}; plot.toleranceBands = {band};
        REQUIRE(samePixels(empty, render(plot)));
        series.v = {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::denorm_min()};
        plot.series = {series}; REQUIRE(samePixels(empty, render(plot)));
        series.f = {qnan, inf, 0}; series.v = {0, 0, 0}; plot.series = {series};
        REQUIRE(samePixels(empty, render(plot)));
        plot.series.clear(); band.upper = {3, 3}; plot.toleranceBands = {band};
        REQUIRE_FALSE(samePixels(empty, render(plot)));
    }
}

TEST_CASE("Plot paint bounds malformed axis ticks instead of hanging", "[app][edges]") {
    juce::ScopedJuceInitialiser_GUI initialise;
    PlotComponent plot;
    for (double step : {0., -1., qnan, inf, std::numeric_limits<double>::denorm_min()}) {
        plot.yStep = step; REQUIRE_FALSE(render(plot).isNull());
    }
    plot.yStep = 6; plot.yMax = plot.yMin; REQUIRE_FALSE(render(plot).isNull());
    plot.yMax = qnan; REQUIRE_FALSE(render(plot).isNull());
    plot.yMax = 12; plot.fMin = 0; REQUIRE_FALSE(render(plot).isNull());
    plot.fMin = 20; plot.fMax = inf; REQUIRE_FALSE(render(plot).isNull());
    plot.fMax = std::nextafter(plot.fMin, inf); REQUIRE_FALSE(render(plot).isNull());
}
