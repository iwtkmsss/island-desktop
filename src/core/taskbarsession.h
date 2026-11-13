#pragma once
#include <cstdint>
#include <map>

namespace IslandDesktop {
// Deterministic guard for secure-desktop transitions and Explorer feedback.
class TaskbarSessionControl {
public:
    void observe(bool interactive, std::uint64_t geometry, std::uint64_t now) {
        if (!interactive) {
            interactive_ = seen_ = settled_ = false;
            hides_.clear(); writes_.clear(); conflicts_ = 0; cooldown_ = lastWrite_ = 0;
            return;
        }
        interactive_ = true;
        if (!seen_ || geometry != geometry_) { geometry_ = geometry; stableSince_ = now; seen_ = true; }
        if (!settled_ && now >= cooldown_ && now - stableSince_ >= 1500) settled_ = true;
    }
    bool active(std::uint64_t now) const { return interactive_ && settled_ && now >= cooldown_ && !failed_; }
    bool cleanupReady(std::uint64_t now) const {
        return interactive_ && settled_ && now >= cooldown_ && now - stableSince_ >= 1500;
    }
    bool allowHide(std::uint64_t window, std::uint64_t now) {
        if (!active(now)) return false;
        if (hides_.size() > 64) { failed_ = true; return false; }
        auto &budget = hides_[window];
        if (budget.attempted && now - budget.last < 100) return false; // Already queued.
        if (!budget.attempted || now - budget.start >= 1000) { budget.start = now; budget.count = 0; }
        if (budget.count >= 3) {
            if (++conflicts_ >= 2) failed_ = true;
            cooldown_ = now + 2000; stableSince_ = now; settled_ = false; hides_.clear();
            return false;
        }
        budget.attempted = true; budget.last = now; ++budget.count;
        return true;
    }
    bool allowWork(std::uint64_t geometry, std::uint64_t now) {
        if (!active(now) || now - stableSince_ < 1500 || (lastWrite_ && now - lastWrite_ < 2000)) return false;
        auto &count = writes_[geometry];
        if (count >= 3 || writes_.size() > 64) { failed_ = true; return false; }
        ++count; lastWrite_ = now; return true;
    }
    bool failed() const { return failed_; }
private:
    struct HideBudget { std::uint64_t start = 0, last = 0; unsigned count = 0; bool attempted = false; };
    bool interactive_ = false, seen_ = false, settled_ = false, failed_ = false;
    unsigned conflicts_ = 0;
    std::uint64_t geometry_ = 0, stableSince_ = 0, cooldown_ = 0, lastWrite_ = 0;
    std::map<std::uint64_t, HideBudget> hides_;
    std::map<std::uint64_t, unsigned> writes_;
};
}
