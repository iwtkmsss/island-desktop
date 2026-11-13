#pragma once
#include <cstdint>

// Physical screen coordinates. With no reserved taskbar area, a normal maximized
// app's client can fill the monitor too; its decorated resize frame extends out.
struct ShellFullscreenCandidate {
    struct Rect { long left = 0, top = 0, right = 0, bottom = 0; };
    Rect client, monitor, outer;
    bool visible = false;
    bool minimized = false;
    bool belongsToUs = false;
    bool desktopSurface = false;
    bool sameMonitor = false;
    bool maximized = false;
    bool decoratedResizable = false;
    bool normalMaximized() const {
        return maximized && decoratedResizable
            && outer.right > outer.left && outer.bottom > outer.top
            && (outer.left < monitor.left || outer.top < monitor.top
                || outer.right > monitor.right || outer.bottom > monitor.bottom);
    }
    bool qualifies() const {
        constexpr long tolerance = 1;
        return visible && !minimized && !belongsToUs && !desktopSurface && sameMonitor && !normalMaximized()
            && client.right > client.left && client.bottom > client.top
            && monitor.right > monitor.left && monitor.bottom > monitor.top
            && client.left <= monitor.left + tolerance && client.top <= monitor.top + tolerance
            && client.right >= monitor.right - tolerance && client.bottom >= monitor.bottom - tolerance;
    }
};

// Hover intent and close grace are independent of rendering and wall-clock time.
class ShellInteraction {
public:
    enum class Action { None, Open, Close };
    struct Input { bool inside; bool expanded; bool pinned; bool pressed; bool visible = true; };
    void suppressUntilLeave() { suppressed_ = true; enteredAt_ = leftAt_ = -1; }
    // Keep a manual-close guard across hiding/fullscreen, but discard elapsed
    // hover/leave time: a hidden interval must never count as hover intent.
    void suspend() { enteredAt_ = leftAt_ = -1; }
    Action update(Input input, std::int64_t now) {
        if (!input.visible) { suspend(); return Action::None; }
        if (input.inside) {
            leftAt_ = -1;
            if (!input.expanded && !suppressed_ && !input.pressed) {
                if (enteredAt_ < 0) enteredAt_ = now;
                if (now - enteredAt_ >= 260) { enteredAt_ = -1; return Action::Open; }
            } else enteredAt_ = -1;
        } else {
            enteredAt_ = -1;
            suppressed_ = false;
            if (input.expanded && !input.pinned && !input.pressed) {
                if (leftAt_ < 0) leftAt_ = now;
                if (now - leftAt_ >= 620) { leftAt_ = -1; return Action::Close; }
            } else leftAt_ = -1;
        }
        return Action::None;
    }
private:
    bool suppressed_ = false;
    std::int64_t enteredAt_ = -1;
    std::int64_t leftAt_ = -1;
};
