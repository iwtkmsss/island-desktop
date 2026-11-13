#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ ! -x "$project_dir/build/IslandDesktop" ]]; then
    printf '%s\n' 'Спочатку виконайте ./scripts/build.sh' >&2
    exit 1
fi
export XDG_DATA_DIRS="$project_dir/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
# XWayland allows desktop-wide cursor tracking. Native Wayland is opt-in until
# gaze and fullscreen parity are verified for the target compositor.
if [[ "${1:-}" == --wayland ]]; then
    export QT_QPA_PLATFORM=wayland
    shift
elif [[ -z "${QT_QPA_PLATFORM:-}" && -n "${DISPLAY:-}" ]]; then
    export QT_QPA_PLATFORM=xcb
fi
exec "$project_dir/build/IslandDesktop" "$@"
