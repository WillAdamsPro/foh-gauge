#include "SqController.h"
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
namespace foh::sq {
namespace {
std::string valueText(Target t, std::uint16_t value) {
    std::ostringstream out; out << std::fixed << std::setprecision(1);
    switch(t.parameter) {
        case Parameter::Frequency: out << SqCodec::freqHz(value) / 1000 << " kHz"; break;
        case Parameter::Gain: out << std::showpos << SqCodec::gainDb(value) << " dB"; break;
        case Parameter::DelayTime: out << SqCodec::delayMs(value) << " ms"; break;
        default: out << (value ? "in" : "out"); break;
    }
    return out.str();
}
foh::Json bytesJson(const Bytes& bytes) { auto j = foh::Json::array(); for (auto b : bytes) j.push(int(b)); return j; }
std::optional<Bytes> jsonBytes(const foh::Json& j) {
    if (!j.isArray() || j.items().size() != 8) return {};
    Bytes out;
    for (const auto& b : j.items()) {
        const auto n = b.asNumber(-1);
        if (n < 0 || n > 255 || n != std::floor(n)) return {};
        out.push_back(std::uint8_t(n));
    }
    return out;
}
}
SqController::SqController(Sink s) : sink(std::move(s)) {}
unsigned SqController::key(Address a) { return (unsigned(a.cat) << 24) | (unsigned(a.reg) << 16) | (unsigned(a.b3) << 8) | a.mod; }
void SqController::startConnection() { disconnect(); connected = true; }
void SqController::disconnect() { connected = firmwareAllowed = false; version.clear(); cache.clear(); pending.clear(); cancel(); }
void SqController::firmware(const Bytes& bytes) { version = bytes; firmwareAllowed = SqAddressMap::firmwareVerified(bytes); cancel(); }
bool SqController::echo(const Bytes& bytes) {
    if (!connected) return false;
    const auto f = SqCodec::parse(bytes); if (!f) return false;
    const auto k = key({f->cat,f->reg,f->b3,f->mod});
    auto found = cache.find(k);
    if (found == cache.end() || found->second != bytes) cancel();
    cache[k] = bytes;
    const auto expected = pending.find(k);
    if (expected != pending.end() && expected->second == f->value) pending.erase(expected);
    return true;
}
std::optional<Frame> SqController::current(Target t) const {
    auto a = SqAddressMap::address(t); if (!a) return {};
    auto it = cache.find(key(*a)); if (it == cache.end()) return {};
    return SqCodec::parse(it->second);
}
bool SqController::fail(const std::string& reason) { cancel(); status = reason; return false; }
bool SqController::addChange(Target t, const Bytes& after) {
    if (!SqAddressMap::writable(t)) return fail("Unverified strip or read-only parameter");
    auto a = SqAddressMap::address(t); auto it = cache.find(key(*a));
    if (it == cache.end()) return fail("No current read-back for target parameter");
    const auto beforeFrame = *SqCodec::parse(it->second), afterFrame = *SqCodec::parse(after);
    if (beforeFrame.value == afterFrame.value) return true;
    proposal.push_back({t,it->second,after,SqAddressMap::describe(t) + ": " + valueText(t,beforeFrame.value) + " -> " + valueText(t,afterFrame.value)});
    return true;
}
bool SqController::propose(const std::vector<Request>& requests) {
    cancel(); undoProposal = false;
    if (!connected) return fail("Disconnected");
    if (!pending.empty()) return fail("Waiting for prior batch echoes; reconnect if the desk rejected it");
    if (requests.empty() || requests.size() > 64) return fail("Invalid batch size");
    std::set<unsigned> seen;
    for (const auto& r : requests) {
        const auto a = SqAddressMap::address(r.target);
        if (!a || !SqAddressMap::writable(r.target)) return fail("Unverified strip or read-only parameter");
        if (!seen.insert(key(*a)).second) return fail("Duplicate parameter in batch");
        if (!std::isfinite(r.value)) return fail("Non-finite parameter");
        std::uint16_t wire{};
        switch(r.target.parameter) {
            case Parameter::Gain: wire = SqCodec::gainWire(r.value); break;
            case Parameter::Frequency: wire = SqCodec::freqWire(r.value); break;
            case Parameter::DelayTime: wire = SqCodec::delayWire(r.value); break;
            case Parameter::DelayIn:
                if (r.value != 0 && r.value != 1) return fail("Delay in must be 0 or 1");
                wire = SqCodec::boolWire(r.value == 1); break;
            default: return fail("Read-only parameter");
        }
        if (!addChange(r.target, SqCodec::encode({a->cat,a->cat,a->reg,a->b3,a->mod,wire}))) return false;
    }
    status = proposal.empty() ? "No changes after clamping" : "Review changes and confirm";
    return !proposal.empty();
}
bool SqController::proposeUndo() {
    cancel(); undoProposal = true;
    if (!pending.empty()) return fail("Waiting for prior batch echoes");
    if (!connected || undo.empty()) return fail("No undo snapshot or disconnected");
    for (const auto& bytes : undo) {
        const auto frame = SqCodec::parse(bytes);
        const auto target = frame ? SqAddressMap::writableTarget(*frame) : std::optional<Target>{};
        if (!target || !addChange(*target, bytes)) return fail("Undo requires current read-back for verified parameters");
    }
    status = "Review exact prior bytes and confirm restore";
    return !proposal.empty();
}
bool SqController::apply(bool confirmed, bool dryRun) {
    dryLog.clear();
    if (!confirmed) return fail("Explicit confirmation required");
    if (proposal.empty()) return fail("No reviewed changes");
    if (!connected) return fail("Disconnected");
    // Recheck all evidence at the dispatch boundary, including dry-runs.
    if (!firmwareAllowed) return fail("Unknown firmware: read-only (capture 3 required)");
    Batch before, after;
    for (const auto& c : proposal) {
        const auto a = SqAddressMap::address(c.target);
        if (!a || !SqAddressMap::writable(c.target) || cache[key(*a)] != c.before) return fail("Read-back changed: review again");
        before.push_back(c.before); after.push_back(c.after);
    }
    if (dryRun) { dryLog = after; status = "Dry-run: zero bytes sent"; cancel(); return true; }
    if (!sink || !sink(after)) return fail("Transport refused batch");
    // Snapshot accepted batches before any echoes; never pretend an enqueue is read-back.
    if (undoProposal) {
        if (undoHistory.empty()) undo.clear();
        else { undo = std::move(undoHistory.back()); undoHistory.pop_back(); }
    } else {
        if (!undo.empty()) undoHistory.push_back(undo);
        undo = before;
    }
    for (const auto& bytes : after) {
        const auto f = *SqCodec::parse(bytes);
        pending[key({f.cat,f.reg,f.b3,f.mod})] = f.value;
    }
    status = "Batch queued; waiting for desk-side echoes"; cancel(); return true;
}
foh::Json SqController::sessionState() const {
    auto j = foh::Json::object(); j.set("schema", 1);
    auto frames = foh::Json::array(); for (const auto& b : undo) frames.push(bytesJson(b));
    j.set("undo",frames);
    auto history = foh::Json::array();
    for (const auto& batch : undoHistory) {
        auto entries = foh::Json::array();
        for (const auto& b : batch) entries.push(bytesJson(b));
        history.push(entries);
    }
    j.set("history",history); return j;
}
bool SqController::restoreSession(const foh::Json& j) {
    disconnect(); undo.clear(); undoHistory.clear(); dryLog.clear();
    if (!j.isObject() || j["schema"].asInt() != 1 || !j["undo"].isArray()) return false;
    auto parseBatch = [](const foh::Json& entries) -> std::optional<Batch> {
        if (!entries.isArray() || entries.items().size() > 64) return {};
        Batch restored; std::set<unsigned> seen;
        for (const auto& entry : entries.items()) {
            auto bytes = jsonBytes(entry); auto frame = bytes ? SqCodec::parse(*bytes) : std::optional<Frame>{};
            if (!frame || !SqAddressMap::writableTarget(*frame) || !seen.insert(key({frame->cat,frame->reg,frame->b3,frame->mod})).second) return {};
            restored.push_back(*bytes);
        }
        return restored;
    };
    auto restored = parseBatch(j["undo"]); if (!restored) return false;
    std::vector<Batch> history;
    if (j.has("history")) {
        if (!j["history"].isArray() || j["history"].items().size() > 4096) return false;
        for (const auto& entry : j["history"].items()) {
            auto batch = parseBatch(entry); if (!batch || batch->empty()) return false;
            history.push_back(*batch);
        }
    }
    if (restored->empty() && !history.empty()) return false;
    undo = std::move(*restored); undoHistory = std::move(history); return true;
}
}
