#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "SqController.h"
#include "SqHandshake.h"
#include "SqRateLimiter.h"
#include <fohcore/session.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iterator>
using namespace foh::sq;
namespace foh::sq {
struct SqControllerTestAccess {
    // This bypass exists only in offline tests to exercise the safety/undo path
    // before capture 3 supplies the real allow-list. Never linked into the app.
    static void verified(SqController& c) { c.startConnection(); c.firmwareAllowed = true; }
};
}
namespace {
Bytes fromHex(const std::string& s) {
    REQUIRE(s.size() == 16); Bytes out;
    for (std::size_t i=0;i<s.size();i+=2) out.push_back(std::uint8_t(std::stoul(s.substr(i,2),nullptr,16)));
    return out;
}
Target gain{{StripKind::Aux,1},Parameter::Gain,Band::HM};
Bytes prior{0xf7,0x11,0x11,0x13,0x73,0x00,0x00,0x80};
Bytes changed{0xf7,0x11,0x11,0x13,0x73,0x00,0x00,0x7d};
}
TEST_CASE("SQ capture fixtures round-trip with independent byte field expectations", "[sq][codec]") {
    std::ifstream in(FOH_SQ_FIXTURES); REQUIRE(in.good());
    std::string error;
    const auto json = foh::Json::parse(std::string(std::istreambuf_iterator<char>(in),{}), &error);
    REQUIRE(error.empty()); REQUIRE(json.isArray()); REQUIRE(json.items().size() == 87);
    bool different = false;
    for (const auto& entry : json.items()) {
        auto bytes = fromHex(entry.asString()); auto frame = SqCodec::parse(bytes); REQUIRE(frame);
        CHECK(frame->prefixCategory == std::stoul(entry.asString().substr(2,2),nullptr,16));
        CHECK(frame->cat == std::stoul(entry.asString().substr(4,2),nullptr,16));
        CHECK(frame->reg == std::stoul(entry.asString().substr(6,2),nullptr,16));
        CHECK(frame->b3 == std::stoul(entry.asString().substr(8,2),nullptr,16));
        CHECK(frame->mod == std::stoul(entry.asString().substr(10,2),nullptr,16));
        const auto expected = std::stoul(entry.asString().substr(14,2) + entry.asString().substr(12,2),nullptr,16);
        CHECK(frame->value == expected); CHECK(SqCodec::encode(*frame) == bytes);
        different |= frame->cat != frame->prefixCategory;
    }
    CHECK(different);
}
TEST_CASE("SQ encoding and clamping use confirmed scales", "[sq][codec]") {
    CHECK(SqCodec::gainWire(0) == 0x8000);
    CHECK(SqCodec::gainDb(0x7248) == Catch::Approx(-13.71875));
    CHECK(SqCodec::freqHz(0xac35) == Catch::Approx(3030).margin(5));
    CHECK(SqCodec::delayMs(0x0b40) == 30.0);
    CHECK(SqCodec::delayWire(30) == 0x0b40);
    CHECK(SqCodec::gainWire(16) == SqCodec::gainWire(15));
    CHECK(SqCodec::gainWire(-16) == SqCodec::gainWire(-15));
    CHECK(SqCodec::freqWire(10) == SqCodec::freqWire(20));
    CHECK(SqCodec::freqWire(22000) == SqCodec::freqWire(20000));
    CHECK(SqCodec::delayWire(900) == 65472);
    CHECK(SqCodec::delayWire(-1) == 0);
    CHECK(SqCodec::delayWire(900,false) == 32736);
    CHECK_THROWS(SqCodec::gainWire(std::numeric_limits<double>::infinity()));
    CHECK_THROWS(SqCodec::freqWire(std::numeric_limits<double>::quiet_NaN()));
    CHECK_FALSE(SqCodec::qWire(1)); CHECK_FALSE(SqCodec::shapeWire(6));
    CHECK(SqCodec::boolean({0x11,0x11,0x1a,0x73,0,1}) == true);
    CHECK(SqCodec::boolean({0x14,0x14,0x0c,0x58,0,0}) == false);
    CHECK_FALSE(SqCodec::boolean({0x14,0x14,0x0c,0x58,0,2}));
}
TEST_CASE("SQ stream decoder handles fragmentation and opaque envelopes", "[sq][codec]") {
    Bytes payload(260,0xf7); auto e = SqCodec::envelope(0x04,payload);
    CHECK(e[2] == 4); CHECK(e[3] == 1);
    auto parsed = SqCodec::parseEnvelope(e); REQUIRE(parsed); CHECK(parsed->payload == payload);
    StreamDecoder decoder;
    Bytes stream = e; stream.insert(stream.end(),prior.begin(),prior.end());
    std::vector<Bytes> output;
    for (auto b : stream) { auto next = decoder.feed({b}); output.insert(output.end(),next.begin(),next.end()); }
    REQUIRE(output.size() == 2); CHECK(output[0] == e); CHECK(output[1] == prior);
    CHECK_FALSE(SqCodec::parse({0xf7})); CHECK_FALSE(SqCodec::parseEnvelope({0x7f}));
    CHECK(decoder.feed({0x7f,0,0xff,0xff,0xff,0xff}).empty());
    CHECK(decoder.hasError()); decoder.reset(); CHECK_FALSE(decoder.hasError());
    CHECK(decoder.feed(prior) == std::vector<Bytes>{prior});
    StreamDecoder together; CHECK(together.feed(stream) == output);
}
TEST_CASE("SQ verified address map limits writable parameters", "[sq][safety]") {
    CHECK(SqAddressMap::stripAddress({StripKind::Input,1}) == 0);
    CHECK(SqAddressMap::stripAddress({StripKind::Input,48}) == 47);
    CHECK(SqAddressMap::stripAddress({StripKind::Aux,1}) == 0x73);
    CHECK(SqAddressMap::stripAddress({StripKind::Matrix,1}) == 0x58);
    CHECK_FALSE(SqAddressMap::stripAddress({StripKind::Aux,2}));
    CHECK_FALSE(SqAddressMap::stripAddress({StripKind::Matrix,2}));
    CHECK_FALSE(SqAddressMap::writable({{StripKind::Input,1},Parameter::DelayIn,Band::LF}));
    for (auto p : {Parameter::PeqIn,Parameter::Q,Parameter::Shape}) CHECK_FALSE(SqAddressMap::writable({{StripKind::Aux,1},p,Band::LF}));
    CHECK_FALSE(SqAddressMap::writableTarget({7,7,0x0e,0x73,0x20,0x8000}));
    CHECK_FALSE(SqAddressMap::writableTarget({0x11,0x11,0x13,0x73,1,0x8000}));
    CHECK_FALSE(SqAddressMap::firmwareVerified({}));
    CHECK_FALSE(SqAddressMap::firmwareVerified({1,6,3}));
}
TEST_CASE("SQ safety refuses missing read-back, unverified targets and unknown firmware", "[sq][safety]") {
    SqController::Batch sent;
    SqController c([&](const auto& b) { sent.insert(sent.end(),b.begin(),b.end()); return true; });
    c.startConnection();
    CHECK_FALSE(c.propose({{gain,-3}}));
    REQUIRE(c.echo(prior)); REQUIRE(c.propose({{gain,-3}}));
    CHECK_FALSE(c.apply(true,false)); CHECK(sent.empty());
    c.firmware({1,6,3}); REQUIRE(c.propose({{gain,-3}})); CHECK_FALSE(c.apply(true,false));
    auto bad = gain; bad.strip.number = 2;
    CHECK_FALSE(c.propose({{bad,-3}})); CHECK(sent.empty());
    c.disconnect(); CHECK_FALSE(c.echo(prior)); CHECK_FALSE(c.current(gain));
}
TEST_CASE("SQ fixture echoes update cache without optimistic write evidence", "[sq][echo]") {
    SqController c({}); c.startConnection();
    const auto bytes = fromHex("f711111373005e7c");
    REQUIRE(c.echo(bytes)); auto f = c.current(gain); REQUIRE(f); CHECK(f->value == 0x7c5e);
    REQUIRE(c.propose({{gain,-3}})); REQUIRE(c.echo(changed)); CHECK(c.changes().empty());
}
TEST_CASE("SQ confirmation, dry-run and exact persisted undo", "[sq][safety]") {
    SqController::Batch sent;
    SqController c([&](const auto& b) { sent = b; return true; });
    SqControllerTestAccess::verified(c); REQUIRE(c.echo(prior));
    REQUIRE(c.propose({{gain,-3}})); CHECK_FALSE(c.apply(false,false)); CHECK(sent.empty());
    REQUIRE(c.propose({{gain,-3}})); REQUIRE(c.apply(true,true));
    CHECK(sent.empty()); CHECK(c.dryRunFrames() == SqController::Batch{changed}); CHECK_FALSE(c.hasUndo());
    // Preserve the nonmatching first category byte from an actual echo in undo.
    auto aliased = prior; aliased[1] = 0x38; REQUIRE(c.echo(aliased));
    REQUIRE(c.propose({{gain,-3}})); REQUIRE(c.apply(true,false)); CHECK(sent == SqController::Batch{changed});
    CHECK(c.current(gain)->value == 0x8000); // enqueue is not read-back
    const auto saved = c.sessionState();
    const auto folder = std::filesystem::temp_directory_path() / ("foh-sq-synthetic-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".fohsession");
    struct Cleanup { std::filesystem::path folder; ~Cleanup() { std::filesystem::remove_all(folder); } } cleanup{folder};
    std::filesystem::remove_all(folder);
    foh::Session session; session.measurement.set("sqConsole",saved);
    std::string error; REQUIRE(foh::saveSession(folder.string(),session,error));
    foh::Session loaded; REQUIRE(foh::loadSession(folder.string(),loaded,error));
    CHECK(loaded.measurement["sqConsole"] == saved);
    SqController restored([&](const auto& b) { sent = b; return true; });
    REQUIRE(restored.restoreSession(foh::Json::parse(saved.dump())));
    CHECK_FALSE(restored.proposeUndo()); // saved cache/firmware never authorizes writes
    SqControllerTestAccess::verified(restored); REQUIRE(restored.echo(changed));
    REQUIRE(restored.proposeUndo()); REQUIRE(restored.apply(true,false));
    CHECK(sent == SqController::Batch{aliased}); CHECK_FALSE(restored.hasUndo());
}
TEST_CASE("SQ batch validation clamps then diffs and fails atomically", "[sq][safety]") {
    int calls = 0;
    SqController c([&](const auto&) { ++calls; return false; }); SqControllerTestAccess::verified(c); REQUIRE(c.echo(prior));
    CHECK_FALSE(c.propose({{gain,0}}));
    CHECK_FALSE(c.propose({{gain,-3},{gain,-4}})); CHECK(c.changes().empty());
    CHECK_FALSE(c.propose({{gain,std::numeric_limits<double>::quiet_NaN()}}));
    auto missing = gain; missing.band = Band::LF;
    CHECK_FALSE(c.propose({{gain,-3},{missing,-2}})); CHECK(c.changes().empty());
    REQUIRE(c.propose({{gain,16}})); CHECK(SqCodec::parse(c.changes()[0].after)->value == SqCodec::gainWire(15));
    CHECK_FALSE(c.apply(true,false)); CHECK(calls == 1); CHECK_FALSE(c.hasUndo());
    auto bad = c.sessionState(); auto frames = foh::Json::array(); frames.push(foh::Json::array()); bad.set("undo",frames);
    CHECK_FALSE(c.restoreSession(bad));
}
TEST_CASE("SQ provisional handshake emits only ordered subscription envelopes", "[sq][handshake]") {
    SqHandshake h; CHECK(h.begin(0x1234) == Bytes{0x7f,0,2,0,0,0,0x34,0x12});
    CHECK(h.receive({0x02,Bytes(12)}).empty());
    CHECK(h.receive({0x00,{0x20,0xc8}}) == std::vector<Bytes>{SqCodec::envelope(1)});
    auto version = Bytes(12,0x42);
    CHECK(h.receive({0x02,version}) == std::vector<Bytes>{SqCodec::envelope(0x14)});
    CHECK(h.version() == version); CHECK_FALSE(h.ready());
    CHECK(h.receive({0x15,{0}}) == std::vector<Bytes>{SqCodec::envelope(0x0b,{2,0})});
    auto subscribe = h.receive({0x0c,{1,0}}); REQUIRE(subscribe.size() == 4);
    CHECK(subscribe[0] == SqCodec::envelope(0x0a,Bytes(8192,0xff)));
    CHECK(subscribe[1] == SqCodec::envelope(0x0d)); CHECK(subscribe[2] == SqCodec::envelope(0x11)); CHECK(subscribe[3] == SqCodec::envelope(0x0f));
    CHECK(h.ready()); CHECK_FALSE(SqAddressMap::firmwareVerified(h.version()));
    CHECK(SqCodec::envelope(0x05) == Bytes{0x7f,5,0,0,0,0});
}

TEST_CASE("SQ frame pacing never bursts and persists across batches", "[sq][safety]") {
    SqRateLimiter limiter;
    const auto epoch = SqRateLimiter::Clock::time_point{};
    REQUIRE(limiter.ready(epoch)); limiter.sent(epoch);
    CHECK_FALSE(limiter.ready(epoch + std::chrono::milliseconds(39)));
    CHECK(limiter.ready(epoch + std::chrono::milliseconds(40)));
    int sent = 1;
    for (int ms = 1; ms < 1000; ++ms) {
        const auto now = epoch + std::chrono::milliseconds(ms);
        if (limiter.ready(now)) { limiter.sent(now); ++sent; }
    }
    CHECK(sent == 25);
    CHECK_FALSE(limiter.ready(epoch + std::chrono::milliseconds(999)));
    CHECK(limiter.ready(epoch + std::chrono::milliseconds(1000)));
}

TEST_CASE("SQ undo keeps every applied batch and waits for acknowledgements", "[sq][safety]") {
    SqController::Batch sent;
    SqController c([&](const auto& b) { sent = b; return true; });
    SqControllerTestAccess::verified(c); REQUIRE(c.echo(prior));
    REQUIRE(c.propose({{gain,-3}})); REQUIRE(c.apply(true,false));
    CHECK_FALSE(c.propose({{gain,-4}})); CHECK_FALSE(c.proposeUndo());
    REQUIRE(c.echo(changed)); REQUIRE(c.propose({{gain,-4}})); REQUIRE(c.apply(true,false));
    const auto second = sent; REQUIRE(c.echo(second[0]));
    const auto saved = c.sessionState(); CHECK(saved["history"].items().size() == 1);
    SqController restored([&](const auto& b) { sent = b; return true; });
    REQUIRE(restored.restoreSession(saved)); SqControllerTestAccess::verified(restored); REQUIRE(restored.echo(second[0]));
    REQUIRE(restored.proposeUndo()); REQUIRE(restored.apply(true,false)); CHECK(sent == SqController::Batch{changed});
    REQUIRE(restored.echo(changed)); REQUIRE(restored.proposeUndo()); REQUIRE(restored.apply(true,false));
    CHECK(sent == SqController::Batch{prior}); CHECK_FALSE(restored.hasUndo());
}
