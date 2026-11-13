// Runtime-only KWin observer. No input injection, shortcuts or persistent settings.
(function () {
    const destination = @SERVICE@;
    const objectPath = @PATH@;
    const plugin = @PLUGIN@;
    let dirty = true;
    let movement = 0;
    let busy = false;
    let heartbeatBusy = false;
    let alive = true;
    const timer = new QTimer();
    timer.interval = 33;
    const watchdog = new QTimer();
    watchdog.interval = 2000;
    function changed() { dirty = true; }
    function moved() { ++movement; dirty = true; }
    function stop() {
        alive = false;
        timer.stop(); watchdog.stop();
        workspace.cursorPosChanged.disconnect(moved);
        workspace.screensChanged.disconnect(changed);
        callDBus("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", "unloadScript", plugin);
    }
    timer.timeout.connect(function () {
        if (!alive || busy || !dirty) return;
        dirty = false; busy = true;
        const p = workspace.cursorPos;
        let output = null;
        for (const screen of workspace.screens) {
            const g = screen.geometry;
            if (p.x >= g.x && p.x < g.x + g.width && p.y >= g.y && p.y < g.y + g.height) { output = screen; break; }
        }
        const g = output ? output.geometry : {x: 0, y: 0, width: 0, height: 0};
        callDBus(destination, objectPath, "io.github.iwtkmsss.IslandDesktop.Pointer", "Update",
            JSON.stringify({movement: movement, x: p.x, y: p.y, screen: output ? output.name : "", left: g.x, top: g.y, width: g.width, height: g.height}),
            function () { busy = false; });
    });
    watchdog.timeout.connect(function () {
        if (heartbeatBusy || !alive) return;
        heartbeatBusy = true;
        callDBus("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner", destination,
            function (present) { heartbeatBusy = false; if (!present) stop(); });
    });
    workspace.cursorPosChanged.connect(moved);
    workspace.screensChanged.connect(changed);
    timer.start(); watchdog.start();
})();
