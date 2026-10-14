#pragma once
#include <chrono>
namespace foh::sq {
// Steady-clock pacing, without bursts across batch boundaries. Half-open
// rolling one-second windows contain at most 25 parameter frames.
class SqRateLimiter {
public:
    using Clock = std::chrono::steady_clock;
    bool ready(Clock::time_point now) const { return now >= next; }
    void sent(Clock::time_point now) { next = now + std::chrono::milliseconds(40); }
private:
    Clock::time_point next{};
};
}
