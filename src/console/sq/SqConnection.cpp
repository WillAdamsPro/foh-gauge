#include "SqConnection.h"
#include "SqRateLimiter.h"
#include <chrono>
namespace foh::sq {
SqConnection::SqConnection(juce::String h) : Thread("SQ console TCP"), host(std::move(h)) { startThread(); }
SqConnection::~SqConnection() {
    signalThreadShouldExit(); notify();
    // Socket lifetime belongs to the worker; bounded waits allow a normal join.
    stopThread(-1);
}
void SqConnection::setStatus(std::string s) { std::lock_guard<std::mutex> lock(mutex); update.status = std::move(s); }
bool SqConnection::enqueue(const SqController::Batch& batch) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!update.ready || !SqAddressMap::firmwareVerified(update.version) || batch.empty() || outgoing.size() + batch.size() > 64) return false;
    for (const auto& bytes : batch) {
        const auto f = SqCodec::parse(bytes);
        if (!f || !SqAddressMap::writableTarget(*f)) return false;
    }
    for (const auto& b : batch) outgoing.push_back(b);
    notify(); return true;
}
SqConnection::Update SqConnection::poll() {
    std::lock_guard<std::mutex> lock(mutex);
    auto result = update; update.echoes.clear(); return result;
}
bool SqConnection::send(const Bytes& bytes) {
    std::size_t offset = 0;
    while (offset < bytes.size() && !threadShouldExit()) {
        if (tcp.waitUntilReady(false, 100) <= 0) return false;
        const int n = tcp.write(bytes.data() + offset, int(bytes.size() - offset));
        if (n <= 0) return false;
        offset += std::size_t(n);
    }
    return offset == bytes.size();
}
void SqConnection::run() {
    if (!udp.bindToPort(0) || !tcp.connect(host,51326,2000)) { setStatus("Connection failed"); return; }
    { std::lock_guard<std::mutex> lock(mutex); update.connected = true; }
    SqHandshake handshake; StreamDecoder decoder;
    bool ok = send(handshake.begin(std::uint16_t(udp.getBoundPort())));
    using Clock = std::chrono::steady_clock;
    auto keepalive = Clock::now();
    SqRateLimiter limiter;
    const auto handshakeDeadline = Clock::now() + std::chrono::seconds(10);
    setStatus("Connected; negotiating read-only subscriptions");
    while (ok && !threadShouldExit()) {
        auto now = Clock::now();
        if (!handshake.ready() && now > handshakeDeadline) { setStatus("Handshake timeout: capture 3 required"); break; }
        if (now >= keepalive) {
            const auto bytes = SqCodec::envelope(0x05);
            ok = udp.write(host,51324,bytes.data(),int(bytes.size())) == int(bytes.size());
            keepalive = now + std::chrono::seconds(1);
        }
        Bytes packet;
        if (limiter.ready(now)) {
            std::lock_guard<std::mutex> lock(mutex);
            // Second firmware gate immediately before dispatch. No arbitrary frames.
            if (handshake.ready() && SqAddressMap::firmwareVerified(update.version) && !outgoing.empty()) {
                packet = std::move(outgoing.front()); outgoing.pop_front();
            }
        }
        if (!packet.empty()) {
            ok = send(packet);
            // No bursts: at least 40ms between parameter frames, including batches.
            limiter.sent(Clock::now());
        }
        int ready = tcp.waitUntilReady(true,20);
        if (ready < 0) { ok = false; break; }
        if (ready == 0) continue;
        std::uint8_t input[16384]; const int n = tcp.read(input,sizeof(input),false);
        if (n <= 0) { ok = false; break; }
        const auto frames = decoder.feed(Bytes(input,input + n));
        if (decoder.hasError()) { ok = false; break; }
        for (const auto& bytes : frames) {
            if (SqCodec::parse(bytes)) {
                std::lock_guard<std::mutex> lock(mutex);
                if (update.echoes.size() >= 4096) { ok = false; break; }
                update.echoes.push_back(bytes);
            } else if (auto envelope = SqCodec::parseEnvelope(bytes)) {
                if (handshake.ready() && envelope->sub == 0x02 && envelope->payload != handshake.version()) {
                    ok = false; break; // Firmware evidence changed: disconnect and drop queued writes.
                }
                for (const auto& reply : handshake.receive(*envelope)) {
                    if (!send(reply)) { ok = false; break; }
                    wait(40);
                }
                std::lock_guard<std::mutex> lock(mutex);
                update.version = handshake.version(); update.ready = handshake.ready();
                if (update.ready) update.status = SqAddressMap::firmwareVerified(update.version)
                    ? "Verified firmware" : "Unknown firmware: read-only; capture 3 pending";
                // TODO docs/console/sq-protocol.md §6: init-dump offsets unverified.
                // Only standalone f7 frames establish read-back in this version.
            }
        }
    }
    tcp.close(); udp.shutdown();
    std::lock_guard<std::mutex> lock(mutex);
    update.connected = update.ready = false; outgoing.clear();
    if (!ok) update.status = "Disconnected: socket error or receive overflow";
}
}
