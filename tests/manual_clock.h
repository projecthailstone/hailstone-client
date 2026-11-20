#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace hailstone::test {

// A session clock (soe::Options::clock) that follows the steady clock until hold(), then moves only by advance(),
// so a timeout test asserts on the session's own time, not on how promptly a loaded machine runs the test thread
// (a 300 ms idle limit fed by real-time keepalives died whenever the test was descheduled that long). Copies share
// the time; the session's service thread reads it too.
class ManualClock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    std::function<time_point()> source() const {
        return [s = state_] { return s->now(); };
    }
    void hold() {
        std::lock_guard lock(state_->mutex);
        state_->held = true;
        state_->at = std::chrono::steady_clock::now();
    }
    void advance(std::chrono::milliseconds d) {
        std::lock_guard lock(state_->mutex);
        state_->at += d;
    }

private:
    struct State {
        std::mutex mutex;
        bool held = false;
        time_point at;
        time_point now() {
            std::lock_guard lock(mutex);
            return held ? at : std::chrono::steady_clock::now();
        }
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
};

} // namespace hailstone::test
