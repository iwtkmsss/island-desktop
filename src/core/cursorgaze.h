#pragma once
#include <QPointF>
#include <QSizeF>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

// Pure sampling state: no input hooks, clocks, or window-system dependencies.
class CursorGaze {
public:
    qreal x() const { return x_; }
    qreal y() const { return y_; }
    quint64 activity() const { return activity_; }

    bool sample(QPointF global, QPointF center, QSizeF screenSize, bool observedMovement = false) {
        // Count raw movement, including movement beyond the clamped gaze range.
        // The first sample after inactivity establishes a baseline, not activity.
        const bool moved = hasPosition_ && (global != lastPosition_ || observedMovement);
        lastPosition_ = global;
        hasPosition_ = true;
        if (moved) ++activity_;
        const qreal dx = (global.x() - center.x()) / std::max(qreal(1), screenSize.width() / 4);
        const qreal dy = (global.y() - center.y()) / std::max(qreal(1), screenSize.height() / 4);
        // Radial limiting retains direction far from the island; independently
        // clamping both axes would freeze a whole screen corner at (1, 1).
        const qreal distance = std::max(qreal(1), std::hypot(dx, dy));
        const qreal x = dx / distance;
        const qreal y = dy / distance;
        const bool changed = moved || x != x_ || y != y_;
        x_ = x;
        y_ = y;
        return changed;
    }

    bool reset() {
        // Keep the serial monotonic. Hidden movement must not become a wake-up
        // when tracking resumes; geometry changes never increment it either.
        hasPosition_ = false;
        const bool changed = x_ != 0 || y_ != 0;
        x_ = y_ = 0;
        return changed;
    }

private:
    QPointF lastPosition_;
    qreal x_ = 0;
    qreal y_ = 0;
    quint64 activity_ = 0;
    bool hasPosition_ = false;
};
