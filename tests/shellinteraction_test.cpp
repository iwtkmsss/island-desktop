#include "shellinteraction.h"
#include <iostream>
#include <stdexcept>

int main() {
    int scenarios = 0;
    const auto expect = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    using Action = ShellInteraction::Action;
    try {
        {
            ShellFullscreenCandidate fullscreen;
            fullscreen.client = {0, 0, 1920, 1080};
            fullscreen.monitor = {0, 0, 1920, 1080};
            fullscreen.visible = fullscreen.sameMonitor = true;
            expect(fullscreen.qualifies(), "A foreign fullscreen client on the island monitor should suppress.");
            fullscreen.client = {0, 32, 1920, 1040};
            expect(!fullscreen.qualifies(), "A normal maximized client must not suppress.");
            fullscreen.client = {1, 0, 1919, 1080};
            expect(fullscreen.qualifies(), "One physical pixel of rounding should be tolerated.");
            ++scenarios;
        }
        {
            ShellFullscreenCandidate fullscreen;
            fullscreen.client = {-1920, -1080, 0, 0};
            fullscreen.monitor = {-1920, -1080, 0, 0};
            fullscreen.visible = fullscreen.sameMonitor = true;
            expect(fullscreen.qualifies(), "Negative monitor coordinates are valid fullscreen bounds.");
            fullscreen.sameMonitor = false;
            expect(!fullscreen.qualifies(), "A fullscreen app on another/offscreen monitor must not suppress.");
            ++scenarios;
        }
        {
            ShellFullscreenCandidate candidate;
            candidate.client = candidate.monitor = {0, 0, 1920, 1080};
            candidate.visible = candidate.sameMonitor = true;
            candidate.minimized = true;
            expect(!candidate.qualifies(), "The restored rectangle of a minimized window is not fullscreen.");
            candidate.minimized = false; candidate.belongsToUs = true;
            expect(!candidate.qualifies(), "Our own fullscreen-sized window must not suppress itself.");
            candidate.belongsToUs = false; candidate.desktopSurface = true;
            expect(!candidate.qualifies(), "Desktop/taskbar surfaces must not count as fullscreen.");
            candidate.desktopSurface = false; candidate.visible = false;
            expect(!candidate.qualifies(), "Invisible windows must not count as fullscreen.");
            ++scenarios;
        }
        {
            ShellFullscreenCandidate candidate;
            candidate.client = candidate.monitor = {1920, 0, 3840, 1080};
            candidate.outer = {1912, -8, 3848, 1088};
            candidate.visible = candidate.sameMonitor = true;
            candidate.maximized = candidate.decoratedResizable = true;
            expect(candidate.normalMaximized() && !candidate.qualifies(),
                "A decorated maximized client filling a monitor without a reserved taskbar is not fullscreen.");
            candidate.decoratedResizable = false;
            expect(candidate.qualifies(), "Borderless fullscreen remains fullscreen even if Windows reports it maximized.");
            candidate.decoratedResizable = true; candidate.maximized = false;
            expect(candidate.qualifies(), "A fullscreen presentation retaining decoration styles is not a normal maximized window.");
            candidate.maximized = true; candidate.outer = candidate.monitor;
            expect(candidate.qualifies(), "Fullscreen with exact monitor outer bounds remains fullscreen even when maximized and decorated.");
            candidate.knownWindowedMaximized = true;
            expect(!candidate.qualifies(), "A previously windowed maximized app expanded by taskbar removal is not fullscreen.");
            candidate.knownWindowedMaximized = false;
            expect(candidate.qualifies(), "A fullscreen transition that loses windowed recognition still suppresses normally.");
            ++scenarios;
        }
        {
            ShellInteraction shell;
            shell.update({true, false, false, false}, 0);
            shell.update({true, false, false, false, false}, 200);
            expect(shell.update({true, false, false, false}, 10000) == Action::None,
                "Time while hidden/fullscreen must not count toward hover opening.");
            expect(shell.update({true, false, false, false}, 10259) == Action::None, "A fresh dwell is required after revealing.");
            expect(shell.update({true, false, false, false}, 10260) == Action::Open, "Fresh hover should work after revealing.");
            ++scenarios;
        }
        {
            ShellInteraction shell;
            shell.suppressUntilLeave();
            shell.update({true, false, false, false, false}, 0);
            expect(shell.update({true, false, false, false}, 10000) == Action::None,
                "Fullscreen exit must not remove the Escape-under-cursor guard.");
            expect(shell.update({true, false, false, false}, 20000) == Action::None, "A stationary pointer must remain guarded.");
            shell.update({false, false, false, false}, 20100);
            shell.update({true, false, false, false}, 20200);
            expect(shell.update({true, false, false, false}, 20460) == Action::Open, "Leaving and deliberately returning should release the guard.");
            ++scenarios;
        }
        {
            ShellInteraction shell;
            shell.update({false, true, false, false}, 0);
            shell.suspend();
            expect(shell.update({false, true, false, false}, 10000) == Action::None,
                "Fullscreen exit must not immediately close the restored panel from an old leave timer.");
            expect(shell.update({false, true, false, false}, 10619) == Action::None, "Restored panel receives a fresh leave grace.");
            expect(shell.update({false, true, false, false}, 10620) == Action::Close, "An unpinned panel still closes after its fresh grace.");
            ++scenarios;
        }
        {
            ShellInteraction shell;
            shell.update({false, true, false, false}, 0);
            shell.suppressUntilLeave();
            shell.update({false, false, false, false}, 100);
            shell.update({true, false, false, false}, 200);
            expect(shell.update({true, false, false, false}, 460) == Action::Open, "A new hover cycle should open normally.");
            expect(shell.update({false, true, false, false}, 500) == Action::None, "An old leave timer must not close a new hover-open immediately.");
            expect(shell.update({false, true, false, false}, 1120) == Action::Close, "The new hover-open uses its own leave grace.");
            ++scenarios;
        }
        {
            ShellInteraction shell;
            for (int i = 0; i < 10; ++i) {
                shell.update({true, false, false, false}, i * 200);
                expect(shell.update({false, false, false, false}, i * 200 + 120) == Action::None,
                    "Repeated short passes must never accumulate into an open.");
            }
            ++scenarios;
        }
        std::cout << scenarios << " shell behavior scenarios passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Shell behavior failure: " << error.what() << '\n';
        return 1;
    }
}
