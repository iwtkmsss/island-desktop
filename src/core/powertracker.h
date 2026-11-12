#pragma once

struct PowerSnapshot {
    bool present = false;
    bool onAc = true;
    int percent = -1;
};

enum class PowerEvent { None, Plugged, Unplugged, Low, Critical };

// Turns a stream of power snapshots into one-shot events. No OS calls, no timers.
class PowerTracker {
public:
    PowerEvent update(const PowerSnapshot &now) {
        if (!now.present) {
            hasBaseline_ = false;
            return PowerEvent::None;
        }
        if (!hasBaseline_) {
            hasBaseline_ = true;
            previous_ = now;
            lowArmed_ = criticalArmed_ = true;
            // Already below a threshold at start: do not announce it.
            if (now.percent >= 0) {
                if (now.percent <= lowThreshold) lowArmed_ = false;
                if (now.percent <= criticalThreshold) criticalArmed_ = false;
            }
            return PowerEvent::None;
        }
        const PowerSnapshot before = previous_;
        previous_ = now;

        // Re-arm.
        if (now.onAc) {
            lowArmed_ = criticalArmed_ = true;
        } else if (now.percent >= 0) {
            if (now.percent > lowThreshold + hysteresis) lowArmed_ = true;
            if (now.percent > criticalThreshold + hysteresis) criticalArmed_ = true;
            if (now.percent > lowThreshold) lowMissed_ = false;
            if (now.percent > criticalThreshold) criticalMissed_ = false;
        } else {
            lowMissed_ = criticalMissed_ = false;
        }

        if (!before.onAc && now.onAc) {
            return PowerEvent::Plugged;
        }
        if (before.onAc && !now.onAc) {
            // The AC change wins. A threshold crossed by this same update stays pending and
            // fires on a later update if still applicable; a level that was already low is not new.
            if (now.percent >= 0) {
                lowMissed_ = now.percent <= lowThreshold && before.percent > lowThreshold;
                criticalMissed_ = now.percent <= criticalThreshold && before.percent > criticalThreshold;
                if (now.percent <= lowThreshold && !lowMissed_) lowArmed_ = false;
                if (now.percent <= criticalThreshold && !criticalMissed_) criticalArmed_ = false;
            }
            return PowerEvent::Unplugged;
        }
        if (now.onAc || now.percent < 0) return PowerEvent::None;

        const int was = before.percent;
        if (was < 0) {
            // No previous level to compare with: the current level becomes the reference.
            return PowerEvent::None;
        }
        if (criticalArmed_ && now.percent <= criticalThreshold && (was > criticalThreshold || criticalMissed_)) {
            criticalArmed_ = false;
            lowArmed_ = false;
            lowMissed_ = criticalMissed_ = false;
            return PowerEvent::Critical;
        }
        if (lowArmed_ && now.percent <= lowThreshold && (was > lowThreshold || lowMissed_)) {
            lowArmed_ = false;
            lowMissed_ = false;
            return PowerEvent::Low;
        }
        return PowerEvent::None;
    }

private:
    static constexpr int lowThreshold = 20;
    static constexpr int criticalThreshold = 10;
    static constexpr int hysteresis = 5;
    bool hasBaseline_ = false;
    PowerSnapshot previous_;
    bool lowArmed_ = true;
    bool criticalArmed_ = true;
    bool lowMissed_ = false;
    bool criticalMissed_ = false;
};
