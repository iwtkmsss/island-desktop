#pragma once

#include <QString>
#include <algorithm>

// Initial readings and a newly selected default endpoint establish a baseline.
// They are not user volume changes and must not produce a flyout.
class VolumeChangeTracker {
public:
    bool observe(const QString &deviceId, int percent, bool muted) {
        if (deviceId.isEmpty()) { reset(); return false; }
        percent = std::clamp(percent, 0, 100);
        const bool event = hasBaseline_ && deviceId == deviceId_
            && (percent != percent_ || muted != muted_);
        deviceId_ = deviceId;
        percent_ = percent;
        muted_ = muted;
        hasBaseline_ = true;
        return event;
    }
    void reset() { hasBaseline_ = false; deviceId_.clear(); }
private:
    QString deviceId_;
    int percent_ = 0;
    bool muted_ = false;
    bool hasBaseline_ = false;
};
