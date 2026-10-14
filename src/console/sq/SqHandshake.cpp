#include "SqHandshake.h"
namespace foh::sq {
Bytes SqHandshake::begin(std::uint16_t udpPort) {
    step = 0; versionPayload.clear();
    return SqCodec::envelope(0x00,{std::uint8_t(udpPort),std::uint8_t(udpPort >> 8)});
}
std::vector<Bytes> SqHandshake::receive(const Envelope& e) {
    if (step == 0 && e.sub == 0x00 && e.payload.size() == 2) {
        step = 1; return {SqCodec::envelope(0x01)};
    }
    if (step == 1 && e.sub == 0x02 && e.payload.size() == 12) {
        versionPayload = e.payload; step = 2; return {SqCodec::envelope(0x14)};
    }
    if (step == 2 && e.sub == 0x15 && !e.payload.empty()) {
        step = 3; return {SqCodec::envelope(0x0b,{0x02,0x00})};
    }
    if (step == 3 && e.sub == 0x0c && e.payload == Bytes{0x01,0x00}) {
        step = 4;
        return {SqCodec::envelope(0x0a,Bytes(8192,0xff)), SqCodec::envelope(0x0d),
                SqCodec::envelope(0x11), SqCodec::envelope(0x0f)};
    }
    return {};
}
}
