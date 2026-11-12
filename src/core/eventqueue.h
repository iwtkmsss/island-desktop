#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>
#include <utility>

struct IslandEvent {
    QString kind;           // "timer", "notice", "volume", later "device", "notification", "clipboard"
    QString title;
    QString icon;           // LineIcon name for QML, e.g. "check", "speaker", "speaker-off"
    int priority = 50;
    int durationMs = 4000;
    QString coalesceKey;    // empty = never coalesce
    bool transient = false; // if it cannot be shown immediately it is dropped, never queued
    int level = -1;         // optional 0..100 value (volume), -1 = none
    bool muted = false;
};

// Priority queue of island events. No timers: the caller supplies monotonic
// milliseconds and re-arms its own timer from deadline().
class EventQueue {
public:
    static constexpr int capacity = 8; // queued events, not counting current

    // Returns true if current() changed.
    bool post(IslandEvent event, qint64 nowMs) {
        event.durationMs = qMax(1, event.durationMs);
        if (!hasCurrent_) {
            makeCurrent(std::move(event), nowMs);
            return true;
        }
        if (!event.coalesceKey.isEmpty()) {
            if (event.coalesceKey == current_.coalesceKey) {
                makeCurrent(std::move(event), nowMs);
                return true;
            }
            for (qsizetype i = 0; i < queue_.size(); ++i) {
                if (queue_.at(i).coalesceKey == event.coalesceKey) {
                    queue_.removeAt(i);
                    break;
                }
            }
        }
        if (event.priority > current_.priority) {
            IslandEvent old = std::move(current_);
            makeCurrent(std::move(event), nowMs);
            if (!old.transient) {
                qsizetype at = 0;
                while (at < queue_.size() && queue_.at(at).priority > old.priority)
                    ++at;
                queue_.insert(at, std::move(old));
            }
            trim();
            return true;
        }
        if (event.transient)
            return false;
        qsizetype at = queue_.size();
        while (at > 0 && queue_.at(at - 1).priority < event.priority)
            --at;
        queue_.insert(at, std::move(event));
        trim();
        return false;
    }

    // Expires the current event once its deadline has been reached.
    bool advance(qint64 nowMs) {
        if (!hasCurrent_ || nowMs < deadline_)
            return false;
        promote(nowMs);
        return true;
    }

    bool dismiss(qint64 nowMs) {
        if (!hasCurrent_)
            return false;
        promote(nowMs);
        return true;
    }

    bool removeKind(const QString &kind, qint64 nowMs) {
        for (qsizetype i = queue_.size(); i > 0; --i) {
            if (queue_.at(i - 1).kind == kind)
                queue_.removeAt(i - 1);
        }
        if (hasCurrent_ && current_.kind == kind) {
            promote(nowMs);
            return true;
        }
        return false;
    }

    void clear() {
        queue_.clear();
        current_ = IslandEvent();
        hasCurrent_ = false;
        deadline_ = -1;
    }

    const IslandEvent *current() const { return hasCurrent_ ? &current_ : nullptr; }
    qint64 deadline() const { return hasCurrent_ ? deadline_ : -1; }
    int pending() const { return static_cast<int>(queue_.size()); }

private:
    void makeCurrent(IslandEvent event, qint64 nowMs) {
        current_ = std::move(event);
        hasCurrent_ = true;
        deadline_ = nowMs + current_.durationMs;
    }

    void promote(qint64 nowMs) {
        if (queue_.isEmpty()) {
            current_ = IslandEvent();
            hasCurrent_ = false;
            deadline_ = -1;
            return;
        }
        makeCurrent(queue_.takeFirst(), nowMs);
    }

    // Drops the oldest event among those with the lowest priority.
    void trim() {
        while (queue_.size() > capacity) {
            int lowest = queue_.constLast().priority;
            for (const auto &e : std::as_const(queue_))
                lowest = qMin(lowest, e.priority);
            for (qsizetype i = 0; i < queue_.size(); ++i) {
                if (queue_.at(i).priority == lowest) {
                    queue_.removeAt(i);
                    break;
                }
            }
        }
    }

    IslandEvent current_;
    bool hasCurrent_ = false;
    qint64 deadline_ = -1;
    QList<IslandEvent> queue_;
};
