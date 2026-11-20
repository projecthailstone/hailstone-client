#include "game/position_tracker.h"


namespace hailstone::game {

void PositionTracker::enable() {
    if (!enabled_) flush();
    enabled_ = true;
}

bool PositionTracker::record(const std::array<float, 3>& p, std::uint32_t reason, std::uint32_t now, const Gate& g) {
    if (!enabled_ || !g.in_game || !g.zone_connected || !g.controlled) return false;
    if (!g.main_loop) {
        if (reason == kPositionMove) return false;
    } else if (reason == kPositionMove) {
        // 0x56e250: compared per component, a NaN never equal.
        bool same = has_last_ && p[0] == last_[0] && p[1] == last_[1] && p[2] == last_[2];
        if (same && now - last_time_ < 1000) return false;
        last_ = p;
        has_last_ = true;
    }
    samples_.push_back({p, reason, now});
    ++sequence_;
    last_time_ = now;
    return reason == 3 || reason == 4 || reason == 5 || reason == 8; // 0x56e290
}

std::vector<PositionTracker::HistoryEntry> PositionTracker::take(bool force, std::uint32_t now) {
    std::vector<HistoryEntry> result;
    if (!enabled_) return result;
    while (!samples_.empty() && result.size() < 10000) {
        const Sample& s = samples_.front();
        if (!force && now - s.time <= 2000) break;
        result.push_back({s, entries_++});
        samples_.pop_front();
    }
    return result;
}

void PositionTracker::flush() {
    samples_.clear();
    sequence_ = entries_;
}

} // namespace hailstone::game
