#!/usr/bin/env bash
set -euo pipefail
runtime_dir="$(mktemp -d /tmp/island-pulse-test.XXXXXXXX)"
chmod 700 "$runtime_dir"
export XDG_RUNTIME_DIR="$runtime_dir"
export PIPEWIRE_RUNTIME_DIR="$runtime_dir"
export PULSE_SERVER="unix:$runtime_dir/pulse/native"
export XDG_CONFIG_HOME="$runtime_dir/config"
export XDG_STATE_HOME="$runtime_dir/state"
export XDG_CACHE_HOME="$runtime_dir/cache"
pipewire_pid=''
pulse_pid=''
policy_pid=''
cleanup() {
    for process in "$policy_pid" "$pulse_pid" "$pipewire_pid"; do
        [[ -z "$process" ]] || kill "$process" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    # Only this test's mktemp directory is removed.
    rm -rf -- "$runtime_dir"
}
trap cleanup EXIT
# The stock server creates only dummy drivers. Disable desktop/bus helpers.
# The policy-only WirePlumber profile does not load hardware monitors.
pipewire -c /usr/share/pipewire/pipewire.conf -P '{ module.x11.bell = false module.jackdbus-detect = false }' > "$runtime_dir/pipewire.log" 2>&1 &
pipewire_pid=$!
pipewire-pulse -c /usr/share/pipewire/pipewire-pulse.conf > "$runtime_dir/pulse.log" 2>&1 &
pulse_pid=$!
ready=false
for ((attempt=0; attempt<100; ++attempt)); do
    if pactl info > /dev/null 2>&1; then ready=true; break; fi
    sleep 0.05
done
if [[ "$ready" != true ]]; then
    cat "$runtime_dir/pipewire.log" "$runtime_dir/pulse.log" >&2
    exit 1
fi
cp /usr/share/wireplumber/wireplumber.conf "$runtime_dir/wireplumber.conf"
WIREPLUMBER_CONFIG_DIR="$runtime_dir" wireplumber -c wireplumber.conf -p policy > "$runtime_dir/policy.log" 2>&1 &
policy_pid=$!
if ! pactl load-module module-null-sink sink_name=island-test 'sink_properties="device.description=island-test node.always-process=true"' > /dev/null; then
    cat "$runtime_dir/pipewire.log" "$runtime_dir/pulse.log" "$runtime_dir/policy.log" >&2
    exit 1
fi
for ((attempt=0; attempt<100; ++attempt)); do
    if pactl set-default-sink island-test > /dev/null 2>&1 && [[ "$(pactl get-default-sink 2>/dev/null)" == island-test ]]; then break; fi
    sleep 0.05
done
if ! "$@"; then
    pactl info >&2
    pactl list short sinks >&2
    tail -70 "$runtime_dir/policy.log" >&2
    cat "$runtime_dir/pipewire.log" "$runtime_dir/pulse.log" >&2
    exit 1
fi
