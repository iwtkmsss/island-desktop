#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$project_dir" -B "$project_dir/build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build "$project_dir/build" --parallel "${ISLAND_BUILD_JOBS:-4}"
QT_QPA_PLATFORM=offscreen ctest --test-dir "$project_dir/build" --output-on-failure
XDG_DATA_DIRS="$project_dir/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}" QT_QPA_PLATFORM=offscreen "$project_dir/build/IslandDesktop" --settings "$project_dir/artifacts/linux/smoke.ini" --smoke-test
