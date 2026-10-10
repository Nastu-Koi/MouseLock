#pragma once

#include <cstdint>

// A continuous press is one gesture. Repeats never restart its clock, and
// changing shortcuts cannot turn a key already held during capture into an exit.
class HoldToExit
{
public:
    bool Update(bool down, std::uint64_t now, unsigned durationMs)
    {
        if (!down)
        {
            Reset();
            blocked_ = false;
            return false;
        }
        if (blocked_ || fired_) return false;
        if (!holding_ || now < started_)
        {
            started_ = now;
            holding_ = true;
        }
        if (now - started_ < durationMs) return false;
        fired_ = true;
        return true;
    }

    void RequireRelease()
    {
        Reset();
        blocked_ = true;
    }

    unsigned Elapsed(std::uint64_t now, unsigned durationMs) const
    {
        if (!holding_ || blocked_ || now < started_) return 0;
        auto elapsed = now - started_;
        return elapsed >= durationMs ? durationMs : static_cast<unsigned>(elapsed);
    }

    bool Holding() const { return holding_ && !blocked_; }

    // A hold is counting, has fired, or a release is still required. While this
    // is false the caller has nothing to time and may stop polling.
    bool Pending() const { return holding_ || fired_ || blocked_; }

private:
    void Reset() { holding_ = fired_ = false; started_ = 0; }
    bool holding_ = false;
    bool fired_ = false;
    bool blocked_ = false;
    std::uint64_t started_ = 0;
};
