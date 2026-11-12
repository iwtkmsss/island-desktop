#pragma once

#include <algorithm>
#include <cstdint>

// The caller supplies monotonic milliseconds. No dependency on wall-clock changes
// or the frequency with which the UI happens to repaint.
class Countdown {
public:
    enum class State { Idle, Running, Paused, Finished };
    using Milliseconds = std::int64_t;

    State state() const { return state_; }
    Milliseconds duration() const { return duration_; }
    Milliseconds remaining(Milliseconds now) const {
        return state_ == State::Running ? std::max<Milliseconds>(0, deadline_ - now) : remaining_;
    }
    bool configure(Milliseconds duration) {
        if (duration < 1000 || duration > 180 * 60 * 1000 || state_ == State::Running)
            return false;
        duration_ = duration;
        reset();
        return true;
    }
    void start(Milliseconds now) {
        if (state_ == State::Running)
            return;
        if (state_ == State::Finished)
            remaining_ = duration_;
        deadline_ = now + remaining_;
        state_ = State::Running;
    }
    void pause(Milliseconds now) {
        if (state_ != State::Running)
            return;
        remaining_ = remaining(now);
        state_ = remaining_ == 0 ? State::Finished : State::Paused;
    }
    void reset() {
        remaining_ = duration_;
        state_ = State::Idle;
    }
    bool advance(Milliseconds now) {
        if (state_ != State::Running || remaining(now) != 0)
            return false;
        remaining_ = 0;
        state_ = State::Finished;
        return true;
    }

private:
    State state_ = State::Idle;
    Milliseconds duration_ = 25 * 60 * 1000;
    Milliseconds remaining_ = duration_;
    Milliseconds deadline_ = 0;
};

