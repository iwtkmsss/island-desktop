#pragma once
// Adapted from the supplied mod's two-poll seek confirmation in MediaThreadProc.
// Copyright (c) 2026 devcode90, MIT; third_party/dynamic-island/LICENSE.
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>

class MediaTimeline {
public:
    void reset() { track_.clear(); pending_ = -1; playing_ = false; }
    std::int64_t accept(std::int64_t reported, std::int64_t duration, bool playing,
                        double rate, const QString &track, std::int64_t now) {
        if (playing && playing_ && track == track_ && duration == duration_) {
            const auto predicted = position_ + static_cast<std::int64_t>((now - updated_) * rate);
            if (reported < predicted - 1500) {
                const auto expectedSeek = pending_ + static_cast<std::int64_t>((now - pendingAt_) * rate);
                if (pending_ >= 0 && std::abs(reported - expectedSeek) < 500) pending_ = -1;
                else { pending_ = reported; pendingAt_ = now; reported = predicted; }
            } else pending_ = -1;
        } else pending_ = -1;
        track_ = track; playing_ = playing; duration_ = duration; updated_ = now;
        position_ = std::clamp(reported, std::int64_t(0), duration);
        return position_;
    }
private:
    QString track_;
    bool playing_ = false;
    std::int64_t position_ = 0, updated_ = 0, duration_ = 0, pending_ = -1, pendingAt_ = 0;
};
