#pragma once
#include "SqController.h"
#include "SqHandshake.h"
#include <juce_core/juce_core.h>
#include <deque>
#include <mutex>
namespace foh::sq {
class SqConnection : private juce::Thread {
public:
    struct Update { bool connected{}; bool ready{}; Bytes version; std::vector<Bytes> echoes; std::string status; };
    explicit SqConnection(juce::String host);
    ~SqConnection() override;
    bool enqueue(const SqController::Batch&);
    Update poll();
private:
    void run() override;
    bool send(const Bytes&);
    void setStatus(std::string);
    const juce::String host;
    juce::StreamingSocket tcp;
    juce::DatagramSocket udp{false};
    std::mutex mutex;
    std::deque<Bytes> outgoing;
    Update update;
};
}
