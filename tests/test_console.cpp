#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "console/Control.h"
#include <cmath>
#include <limits>
using namespace foh::console;
using Catch::Approx;
namespace {
Bytes hex(const std::string& text) {
    Bytes out;
    for (std::size_t i = 0; i < text.size(); i += 2) out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}
// Synthetic wire recordings, fixed independently of the encoder under test.
const Bytes busGain = hex("2f6275732f30312f65712f322f6700002c6600003f000000"); // 0 dB
const Bytes matrixGain = hex("2f6d74782f30362f65712f362f6700002c6600003e800000"); // -7.5 dB
const Bytes outputDelay = hex("2f6f7574707574732f6d61696e2f30342f64656c61792f74696d65002c6600003f000000"); // 250.15 ms
const Bytes outputSource = hex("2f6f7574707574732f6d61696e2f30342f737263000000002c69000000000004"); // mix 1
const Bytes sqGain = hex("f738110c58000080"); // mix 1 LF gain, 0 dB
const Bytes sqDelay = hex("f738140d5800b004"); // mix 1, 12.5 ms (1200 samples)
}
TEST_CASE("X32 synthetic recorded OSC replies and write bytes", "[console][protocol]") {
    Target t {Desk::x32, Bus::mix, 1, 4};
    auto v = decodeX32(t, busGain); REQUIRE(v); CHECK(v->band == 2); CHECK(v->value == 0);
    CHECK(encodeWrite(*v) == busGain);
    auto d = decodeX32(t, outputDelay); REQUIRE(d); CHECK(d->value == Approx(250.15)); CHECK(encodeWrite(*d) == outputDelay);
    t.bus = Bus::matrix; t.number = 6;
    v = decodeX32(t, matrixGain); REQUIRE(v); CHECK(v->value == Approx(-7.5)); CHECK(v->band == 6);
    CHECK(encodeWrite(*v) == matrixGain);
    CHECK_FALSE(decodeX32(t, busGain));
    CHECK(oscQuery("/bus/01/eq/2/g") == hex("2f6275732f30312f65712f322f670000"));
}
TEST_CASE("X32 EQ transforms and every mix and matrix address", "[console][protocol]") {
    for (auto bus : {Bus::mix, Bus::matrix}) for (int n = 1; n <= (bus == Bus::mix ? 16 : 6); ++n) {
        Target t {Desk::x32, bus, n, 1};
        for (int b = 1; b <= 6; ++b) for (auto p : {Parameter::frequency, Parameter::gain, Parameter::q, Parameter::shape}) {
            for (double x : (p == Parameter::frequency ? std::vector<double>{20, 1000, 20000} : p == Parameter::gain ? std::vector<double>{-15, 0, 15} : p == Parameter::q ? std::vector<double>{0.3, 1, 10} : std::vector<double>{0, 2, 5})) {
                Value v {t, p, b, x}; auto result = decodeX32(t, encodeWrite(v)); REQUIRE(result); CHECK(result->value == Approx(x).epsilon(1e-5).margin(1e-5));
            }
        }
        for (auto p : {Parameter::eqOn, Parameter::delayOn}) for (double x : {0.0, 1.0}) {
            Value v {t, p, 0, x}; auto result = decodeX32(t, encodeWrite(v)); REQUIRE(result); CHECK(result->value == x);
        }
    }
}
TEST_CASE("OSC malformed and unrelated recordings fail closed", "[console][protocol]") {
    Target t;
    for (std::size_t n = 0; n < busGain.size(); ++n) CHECK_FALSE(parseOsc(Bytes(busGain.begin(), busGain.begin() + static_cast<std::ptrdiff_t>(n))));
    auto b = busGain; b.push_back(0); CHECK_FALSE(parseOsc(b));
    b = busGain; b[15] = 1; CHECK_FALSE(parseOsc(b)); // non-zero padding
    b = busGain; b[17] = 'd'; CHECK_FALSE(parseOsc(b));
    b = busGain; b[20] = 0x7f; b[21] = 0x80; b[22] = 0; b[23] = 0; CHECK_FALSE(parseOsc(b)); // inf
    b = busGain; b[20] = 0x40; CHECK_FALSE(decodeX32(t, b)); // normalised value > 1
    CHECK_FALSE(decodeX32(t, hex("2362756e646c65000000000000000000")));
    for (auto x : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -16.0, 16.0})
        CHECK_THROWS(encodeWrite({t, Parameter::gain, 1, x}));
    CHECK_THROWS(encodeWrite({t, Parameter::delayMs, 0, 10})); // no selected output
    CHECK_THROWS(encodeWrite({t, Parameter::frequency, 7, 1000}));
    CHECK_THROWS(encodeWrite({t, Parameter::eqOn, 0, 2}));
}
TEST_CASE("SQ recorded MixPad frames and fragmented stream", "[console][protocol]") {
    Target t {Desk::sq, Bus::mix, 1, 0};
    SqFramer framer;
    for (std::size_t split = 0; split <= sqGain.size(); ++split) {
        framer.reset(); auto frames = framer.push(Bytes(sqGain.begin(), sqGain.begin() + static_cast<std::ptrdiff_t>(split)));
        auto tail = framer.push(Bytes(sqGain.begin() + static_cast<std::ptrdiff_t>(split), sqGain.end()));
        frames.insert(frames.end(), tail.begin(), tail.end()); REQUIRE(frames.size() == 1);
        auto v = decodeSq(t, frames[0]); REQUIRE(v); CHECK(v->value == 0); CHECK(v->band == 1); CHECK(encodeWrite(*v) == sqGain);
    }
    auto frames = framer.push(sqDelay); REQUIRE(frames.size() == 1);
    auto v = decodeSq(t, frames[0]); REQUIRE(v); CHECK(v->value == 12.5); CHECK(encodeWrite(*v) == sqDelay);
    Bytes burst = sqFrame(3); burst.insert(burst.end(), sqGain.begin(), sqGain.end());
    frames = framer.push(burst); REQUIRE(frames.size() == 2); CHECK(frames[0].type == 3);
    CHECK(sqFrame(0x0b, {2,0}) == hex("7f0b020000000200"));
    CHECK_THROWS(framer.push(hex("7f040000ffffffff")));
    CHECK_THROWS(framer.push(Bytes(131073, 0)));
    framer.reset(); CHECK_THROWS(framer.push({0}));
    CHECK_FALSE(validTarget({Desk::sq, Bus::matrix, 1, 0})); // No guessed addresses.
}
TEST_CASE("Console transport cannot write before explicit exact one-shot confirmation", "[console][confirmation]") {
    std::vector<Bytes> sent;
    Control c([&](const Bytes& b) { sent.push_back(b); return true; });
    Target t {Desk::x32, Bus::mix, 1, 4}; c.select(t);
    CHECK(c.read()); REQUIRE(sent.size() == 28); // 25 EQ, 2 delay, source query
    for (const auto& b : sent) CHECK_FALSE(parseOsc(b)); // All address-only reads.
    sent.clear();
    auto id = c.propose({t, Parameter::gain, 2, 0}); REQUIRE(id); CHECK(sent.empty());
    CHECK_FALSE(c.confirm(*id, false)); CHECK(sent.empty()); CHECK_FALSE(c.confirm(*id, true));
    id = c.propose({t, Parameter::gain, 2, 0}); REQUIRE(id);
    CHECK_FALSE(c.confirm(*id + 1, true)); CHECK(sent.empty());
    CHECK(c.confirm(*id, true)); REQUIRE(sent.size() == 1); CHECK(sent[0] == busGain);
    CHECK_FALSE(c.confirm(*id, true)); CHECK(sent.size() == 1);
    id = c.propose({t, Parameter::gain, 2, 3}); REQUIRE(id); c.cancel(); CHECK_FALSE(c.confirm(*id, true));
    id = c.propose({t, Parameter::gain, 2, 3}); REQUIRE(id); c.select(t); CHECK_FALSE(c.confirm(*id, true));
    CHECK_FALSE(c.propose({{Desk::x32, Bus::matrix, 1, 4}, Parameter::gain, 2, 0}));
}
TEST_CASE("X32 delay confirmation requires matching output source read-back", "[console][confirmation]") {
    std::vector<Bytes> sent;
    Control c([&](const Bytes& b) { sent.push_back(b); return true; });
    Target t {Desk::x32, Bus::mix, 1, 4}; c.select(t);
    CHECK_FALSE(c.propose({t, Parameter::delayMs, 0, 250.15}));
    c.receive(outputSource); auto id = c.propose({t, Parameter::delayMs, 0, 250.15}); REQUIRE(id); CHECK(sent.empty());
    auto changed = outputSource; changed.back() = 20; c.receive(changed); CHECK_FALSE(c.confirm(*id, true)); CHECK(sent.empty());
    c.receive(outputSource); id = c.propose({t, Parameter::delayMs, 0, 250.15}); REQUIRE(id); CHECK(c.confirm(*id, true)); REQUIRE(sent.size() == 1); CHECK(sent[0] == outputDelay);
    t.bus = Bus::matrix; t.number = 1; c.select(t); c.receive(outputSource);
    CHECK_FALSE(c.propose({t, Parameter::delayMs, 0, 10}));
    c.receive(changed); id = c.propose({t, Parameter::delayOn, 0, 1}); REQUIRE(id); CHECK(c.confirm(*id, true));
}
TEST_CASE("SQ writes and invalid proposals remain disabled without verified firmware captures", "[console][confirmation]") {
    int calls = 0; Control c([&](const Bytes&) { ++calls; return true; });
    Target t {Desk::sq, Bus::mix, 1, 0}; c.select(t);
    CHECK_FALSE(c.propose({t, Parameter::gain, 1, 0})); CHECK_FALSE(c.confirm(0, true)); CHECK(calls == 0);
    t = {Desk::x32, Bus::mix, 1, 0}; c.select(t);
    auto id = c.propose({t, Parameter::gain, 1, 0}); REQUIRE(id);
    CHECK_FALSE(c.propose({t, Parameter::gain, 1, 99})); CHECK_FALSE(c.confirm(*id, true)); CHECK(calls == 0);
}
TEST_CASE("Transport failure consumes confirmation without retry", "[console][confirmation]") {
    int calls = 0; Control c([&](const Bytes&) { ++calls; return false; }); Target t; c.select(t);
    auto id = c.propose({t, Parameter::gain, 1, 0}); REQUIRE(id);
    CHECK_FALSE(c.confirm(*id, true)); CHECK_FALSE(c.confirm(*id, true)); CHECK(calls == 1);
}
TEST_CASE("Fresh reads discard old routing, proposals and parameter snapshots", "[console][confirmation]") {
    std::vector<Bytes> sent; Control c([&](const Bytes& b) { sent.push_back(b); return true; });
    Target t {Desk::x32, Bus::mix, 1, 4}; c.select(t);
    c.receive(busGain); auto v = c.readValue(Parameter::gain, 2); REQUIRE(v); CHECK(v->value == 0);
    CHECK_FALSE(c.readValue(Parameter::gain, 1));
    c.receive(outputSource); auto id = c.propose({t, Parameter::delayMs, 0, 10}); REQUIRE(id);
    CHECK(c.read()); CHECK_FALSE(c.confirm(*id, true)); CHECK_FALSE(c.readValue(Parameter::gain, 2));
    CHECK_FALSE(c.propose({t, Parameter::delayMs, 0, 10}));
    c.receive(outputSource); id = c.propose({t, Parameter::delayMs, 0, 10}); REQUIRE(id);
    auto newId = c.propose({t, Parameter::gain, 2, 0}); REQUIRE(newId);
    CHECK_FALSE(c.confirm(*id, true)); auto count = sent.size(); CHECK(c.confirm(*newId, true)); CHECK(sent.size() == count + 1);
}
