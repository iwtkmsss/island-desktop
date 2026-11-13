#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>

namespace IslandDesktop::WorkArea {
struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const Rect &) const = default;
    bool valid() const { return right > left && bottom > top; }
};
enum class Edge : std::int32_t { Left, Top, Right, Bottom };
inline std::int32_t edge(Rect rect, Edge side) {
    switch (side) {
    case Edge::Left: return rect.left;
    case Edge::Top: return rect.top;
    case Edge::Right: return rect.right;
    case Edge::Bottom: return rect.bottom;
    }
    return 0;
}
inline Rect withEdge(Rect rect, Edge side, std::int32_t value) {
    switch (side) {
    case Edge::Left: rect.left = value; break;
    case Edge::Top: rect.top = value; break;
    case Edge::Right: rect.right = value; break;
    case Edge::Bottom: rect.bottom = value; break;
    }
    return rect;
}
inline bool within(Rect rect, Rect monitor) {
    return rect.valid() && monitor.valid() && rect.left >= monitor.left && rect.top >= monitor.top
        && rect.right <= monitor.right && rect.bottom <= monitor.bottom;
}
struct Change { Edge side; std::int32_t before, after; };
inline bool windowedBeforeRelease(Rect monitor, Rect oldWork, Rect frame) {
    return oldWork != monitor && frame != monitor;
}

// Release only a strip touching the current work area. Other appbars can occupy
// the remaining edges or the outer portion of the same edge. A disjoint strip
// cannot be represented by rcWork, so ambiguous/non-adjacent reservations stay.
inline std::optional<Change> taskbarStrip(Rect monitor, Rect work, Rect bar) {
    if (!within(work, monitor)) return {};
    bar = {std::max(bar.left, monitor.left), std::max(bar.top, monitor.top),
        std::min(bar.right, monitor.right), std::min(bar.bottom, monitor.bottom)};
    if (!bar.valid()) return {};
    const bool horizontal = bar.right - std::int64_t(bar.left) > bar.bottom - std::int64_t(bar.top);
    if (horizontal && bar.left <= work.left && bar.right >= work.right) {
        if (bar.top == work.bottom && bar.bottom > work.bottom) return Change{Edge::Bottom, work.bottom, bar.bottom};
        if (bar.bottom == work.top && bar.top < work.top) return Change{Edge::Top, work.top, bar.top};
    }
    if (!horizontal && bar.top <= work.top && bar.bottom >= work.bottom) {
        if (bar.right == work.left && bar.left < work.left) return Change{Edge::Left, work.left, bar.left};
        if (bar.left == work.right && bar.right > work.right) return Change{Edge::Right, work.right, bar.right};
    }
    return {};
}
inline std::optional<Rect> restore(Rect monitor, Rect current, Change owned) {
    if (edge(current, owned.side) != owned.after) return {};
    const auto desired = withEdge(current, owned.side, owned.before);
    if (!within(desired, monitor)) return {};
    return desired;
}
inline std::optional<Rect> reflow(Rect oldWork, Rect newWork, Rect visibleFrame, Rect outer) {
    const auto closeEnough = [](std::int32_t a, std::int32_t b) { return std::abs(std::int64_t(a) - b) <= 2; };
    if (!oldWork.valid() || !newWork.valid() || !outer.valid()
        || !closeEnough(visibleFrame.left, oldWork.left) || !closeEnough(visibleFrame.top, oldWork.top)
        || !closeEnough(visibleFrame.right, oldWork.right) || !closeEnough(visibleFrame.bottom, oldWork.bottom)) return {};
    const auto moved = [](std::int32_t value, std::int32_t from, std::int32_t to) -> std::optional<std::int32_t> {
        const auto next = std::int64_t(value) + to - from;
        if (next < std::numeric_limits<std::int32_t>::min() || next > std::numeric_limits<std::int32_t>::max()) return {};
        return static_cast<std::int32_t>(next);
    };
    const auto left = moved(outer.left, oldWork.left, newWork.left), top = moved(outer.top, oldWork.top, newWork.top);
    const auto right = moved(outer.right, oldWork.right, newWork.right), bottom = moved(outer.bottom, oldWork.bottom, newWork.bottom);
    if (!left || !top || !right || !bottom) return {};
    return Rect{*left, *top, *right, *bottom};
}
}
