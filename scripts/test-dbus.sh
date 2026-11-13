#!/usr/bin/env bash
set -euo pipefail
# Invoked inside dbus-run-session. Both bus roles point only to this test bus.
export DBUS_SYSTEM_BUS_ADDRESS="${DBUS_SESSION_BUS_ADDRESS:?Private test bus is required}"
exec "$@"
