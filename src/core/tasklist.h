#pragma once
// Platform-independent row model of the Home panel task bar on Windows.
//
// Rows are applications. Window membership, the stable cycle order and the
// click decision live here so they can be tested on any OS; the platform layer
// only supplies window snapshots and performs the resulting action.
//
// Click rules reproduce src/platform/linux/taskactivation.h (KDE TaskTools'
// cycle mode): no window list opens on a left click.
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <algorithm>
#include <iterator>
#include <limits>

namespace IslandDesktop {

struct TaskWindow {
    quintptr id = 0;
    QString appKey;
    QString title;
    bool minimized = false;
};

class TaskList {
public:
    // Notified around every mutation so a QAbstractItemModel can emit exact
    // begin/end signals. All callbacks are optional.
    struct Observer {
        virtual ~Observer() = default;
        virtual void beginReset() {}
        virtual void endReset() {}
        virtual void beginInsert(int, int) {}  // first, last
        virtual void endInsert() {}
        virtual void beginRemove(int) {}
        virtual void endRemove() {}
        virtual void beginMove(int, int) {}    // from, final index
        virtual void endMove() {}
        virtual void rowChanged(int) {}
    };
    struct Row {
        QString appKey;
        bool pinned = false;
        QList<quintptr> windows;  // first-seen order: the stable cycle order
    };
    struct State {
        int count = 0;   // windows in the row (0 for a pure launcher)
        int active = -1; // index of the active window within the row
    };
    enum class ActionKind { None, Launch, Activate, Minimize, Restore };
    struct Action {
        ActionKind kind = ActionKind::None;
        QString appKey;
        quintptr window = 0;
    };

    void setObserver(Observer *observer) { observer_ = observer; }

    int rowCount() const { return int(rows_.size()); }
    const Row *row(int index) const { return index >= 0 && index < rows_.size() ? &rows_[index] : nullptr; }
    int rowOf(const QString &appKey) const {
        for (int r = 0; r < rows_.size(); ++r)
            if (rows_[r].appKey == appKey) return r;
        return -1;
    }
    bool contains(quintptr window) const { return windows_.contains(window); }
    TaskWindow window(quintptr id) const { return windows_.value(id); }

    // Applies a snapshot of all task windows in z-order (top first). Existing
    // rows never move: an unpinned row disappears with its last window, a
    // pinned one becomes a launcher in place, new applications are appended.
    // Returns true if anything observable changed.
    bool update(const QList<TaskWindow> &snapshot) {
        QHash<quintptr, TaskWindow> next;
        QHash<quintptr, int> zOrder;
        QList<quintptr> ordered;
        for (const auto &w : snapshot) {
            if (!w.id || w.appKey.isEmpty() || next.contains(w.id)) continue;
            zOrder.insert(w.id, int(ordered.size()));
            ordered.append(w.id);
            next.insert(w.id, w);
            if (!firstSeen_.contains(w.id)) firstSeen_.insert(w.id, ++sequence_);
        }
        for (auto it = firstSeen_.begin(); it != firstSeen_.end();)
            it = next.contains(it.key()) ? std::next(it) : firstSeen_.erase(it);
        for (auto it = lastActivated_.begin(); it != lastActivated_.end();)
            it = next.contains(it.key()) ? std::next(it) : lastActivated_.erase(it);
        std::stable_sort(ordered.begin(), ordered.end(),
            [this](quintptr a, quintptr b) { return firstSeen_.value(a) < firstSeen_.value(b); });
        QHash<QString, QList<quintptr>> byApp;
        QStringList appOrder;
        for (const auto id : ordered) {
            const auto &key = next[id].appKey;
            if (!byApp.contains(key)) appOrder.append(key);
            byApp[key].append(id);
        }
        const auto previous = windows_;
        windows_ = next;
        zOrder_ = zOrder;

        bool changed = false;
        for (int r = int(rows_.size()) - 1; r >= 0; --r) {
            if (rows_[r].pinned || byApp.contains(rows_[r].appKey)) continue;
            obs().beginRemove(r);
            rows_.removeAt(r);
            obs().endRemove();
            changed = true;
        }
        for (int r = 0; r < rows_.size(); ++r) {
            auto &row = rows_[r];
            const auto windows = byApp.value(row.appKey);
            bool differs = windows != row.windows;
            for (int n = 0; !differs && n < windows.size(); ++n) {
                const auto &before = previous.value(windows[n]);
                const auto &after = windows_[windows[n]];
                differs = before.title != after.title || before.minimized != after.minimized;
            }
            if (!differs) continue;
            row.windows = windows;
            obs().rowChanged(r);
            changed = true;
        }
        QStringList fresh;
        for (const auto &key : appOrder)
            if (rowOf(key) < 0) fresh.append(key);
        if (!fresh.isEmpty()) {
            const int first = int(rows_.size());
            obs().beginInsert(first, first + int(fresh.size()) - 1);
            for (const auto &key : fresh) rows_.append({key, false, byApp.value(key)});
            obs().endInsert();
            changed = true;
        }
        return changed;
    }

    // A known window became the foreground window. Only affects which window
    // of a group a click picks first; never reorders the cycle.
    void noteActivated(quintptr window) {
        if (window && windows_.contains(window)) lastActivated_[window] = ++clock_;
    }

    // `effectiveActive` is the window to treat as active (0 for none).
    State state(int index, quintptr effectiveActive) const {
        const auto *r = row(index);
        if (!r) return {};
        return {int(r->windows.size()), effectiveActive ? int(r->windows.indexOf(effectiveActive)) : -1};
    }

    Action click(int index, quintptr effectiveActive) const {
        const auto *r = row(index);
        if (!r) return {};
        const auto &windows = r->windows;
        if (windows.isEmpty()) return {ActionKind::Launch, r->appKey, 0};
        if (windows.size() == 1) {
            const auto id = windows.first();
            if (windows_.value(id).minimized) return {ActionKind::Restore, r->appKey, id};
            if (effectiveActive && id == effectiveActive) return {ActionKind::Minimize, r->appKey, id};
            return {ActionKind::Activate, r->appKey, id};
        }
        const int current = effectiveActive ? int(windows.indexOf(effectiveActive)) : -1;
        if (current >= 0)
            return {ActionKind::Activate, r->appKey, windows[(current + 1) % int(windows.size())]};
        quintptr target = 0;
        quint64 latest = 0;
        for (const auto id : windows) {
            const auto stamp = lastActivated_.value(id);
            if (stamp > latest) { latest = stamp; target = id; }
        }
        if (!target) {
            int top = std::numeric_limits<int>::max();
            for (const auto id : windows) {
                const int z = zOrder_.value(id, std::numeric_limits<int>::max());
                if (z < top) { top = z; target = id; }
            }
        }
        if (!target) target = windows.first();
        return {ActionKind::Activate, r->appKey, target};
    }

    bool move(int from, int to) {
        if (from < 0 || to < 0 || from >= rows_.size() || to >= rows_.size() || from == to) return false;
        obs().beginMove(from, to);
        rows_.move(from, to);
        obs().endMove();
        return true;
    }
    bool pin(int index) {
        if (!row(index) || rows_[index].pinned) return false;
        rows_[index].pinned = true;
        obs().rowChanged(index);
        return true;
    }
    bool unpin(int index) {
        if (!row(index) || !rows_[index].pinned) return false;
        if (rows_[index].windows.isEmpty()) {
            obs().beginRemove(index);
            rows_.removeAt(index);
            obs().endRemove();
        } else {
            rows_[index].pinned = false;
            obs().rowChanged(index);
        }
        return true;
    }
    // Pinned application keys in row order: what gets persisted.
    QStringList launchers() const {
        QStringList keys;
        for (const auto &r : rows_)
            if (r.pinned) keys.append(r.appKey);
        return keys;
    }
    // Seeds pinned rows (normally at startup). Rows with windows that are not
    // in `keys` keep their windows and follow the launchers.
    void setLaunchers(const QStringList &keys) {
        obs().beginReset();
        QList<Row> next;
        QStringList seen;
        for (const auto &key : keys) {
            if (key.isEmpty() || seen.contains(key)) continue;
            seen.append(key);
            const int existing = rowOf(key);
            next.append({key, true, existing >= 0 ? rows_[existing].windows : QList<quintptr>()});
        }
        for (const auto &r : rows_)
            if (!seen.contains(r.appKey) && !r.windows.isEmpty()) next.append({r.appKey, false, r.windows});
        rows_ = next;
        obs().endReset();
    }

private:
    Observer &obs() { return observer_ ? *observer_ : silent_; }
    QList<Row> rows_;
    QHash<quintptr, TaskWindow> windows_;
    QHash<quintptr, quint64> firstSeen_;
    QHash<quintptr, quint64> lastActivated_;
    QHash<quintptr, int> zOrder_;
    quint64 sequence_ = 0;
    quint64 clock_ = 0;
    Observer *observer_ = nullptr;
    Observer silent_;
};

}  // namespace IslandDesktop
