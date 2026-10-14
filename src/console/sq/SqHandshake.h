#pragma once
#include "SqCodec.h"
namespace foh::sq {
// Provisional read-only handshake. Exact firmware/version bytes still await
// capture 3 (docs/console/sq-protocol.md §6); no handshake enables writes.
class SqHandshake {
public:
    Bytes begin(std::uint16_t udpPort);
    std::vector<Bytes> receive(const Envelope&);
    bool ready() const { return step == 4; }
    const Bytes& version() const { return versionPayload; }
private:
    int step = 0;
    Bytes versionPayload;
};
}
