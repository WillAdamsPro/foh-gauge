#pragma once
#include "Protocol.h"
#include <functional>

namespace foh::console {
// Single control-thread owner. No dependency on fohcore or an audio engine.
// Transport is supplied by a worker; no raw send API is exposed by this controller.
class Control {
public:
    using Send = std::function<bool(const Bytes&)>;
    explicit Control(Send sender) : send(std::move(sender)) {}
    void select(Target target); // Cancels pending confirmation and discards routing state.
    bool read();
    void receive(const Bytes&); // X32 routing read-back and parameter reply.
    std::optional<Value> readValue(Parameter, int band) const;
    std::optional<std::uint64_t> propose(Value); // Does not send anything.
    std::string preview() const;
    bool confirm(std::uint64_t proposal, bool accepted); // One-shot, exact proposal only.
    void cancel();
private:
    Send send;
    Target selected;
    std::optional<Value> pending;
    std::vector<Value> values;
    std::optional<int> outputSource;
    std::uint64_t serial = 0;
};
}
