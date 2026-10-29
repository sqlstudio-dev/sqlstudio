#!/usr/bin/env bash
#
# Smoke test for an installed SqlStudio: starts it on a virtual X display with
# a fresh user profile, waits for the main window and checks that the
# application is still running a while later without Python errors and that it
# recognises the operating system as supported.
#
# Needs: xvfb, xdotool, x11-utils, dbus-x11, imagemagick.
#
# Usage: smoke_test.sh [output-dir]
#
# The output directory receives a screenshot, the list of open windows, the
# application's console output and its own log (wb.log), for inspection when
# the test fails.

set -euo pipefail

OUT_DIR=${1:-smoke-test}
WINDOW_TIMEOUT=${SMOKE_WINDOW_TIMEOUT:-120}   # seconds to wait for the main window
SETTLE_TIME=${SMOKE_SETTLE_TIME:-20}          # seconds it must keep running after that

mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)

fail() {
    echo "::error::Smoke test: $*"
    exit 1
}

# A fresh profile, so the test always covers the first start.
export HOME
HOME=$(mktemp -d)
export DISPLAY=:99

app_pid=
xvfb_pid=
cleanup() {
    # Collected on every exit, so a failure can be inspected too.
    import -window root "$OUT_DIR/screenshot.png" 2>/dev/null || true
    xdotool search --onlyvisible --name . 2>/dev/null |
        while read -r w; do xdotool getwindowname "$w"; done >"$OUT_DIR/windows.txt" || true
    cp "$HOME/.mysql/studio/log/wb.log" "$OUT_DIR/" 2>/dev/null || true

    # The launcher runs sql-studio-bin as a child, so stop that one too.
    pkill -x sql-studio-bin 2>/dev/null || true
    [[ -n $app_pid ]] && kill "$app_pid" 2>/dev/null || true
    [[ -n ${DBUS_SESSION_BUS_PID:-} ]] && kill "$DBUS_SESSION_BUS_PID" 2>/dev/null || true
    [[ -n $xvfb_pid ]] && kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT

Xvfb "$DISPLAY" -screen 0 1600x1000x24 -nolisten tcp &
xvfb_pid=$!
for _ in $(seq 50); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.2; done
xdpyinfo >/dev/null 2>&1 || fail "Xvfb did not start"

eval "$(dbus-launch --sh-syntax)"

echo "Starting sql-studio..."
sql-studio >"$OUT_DIR/console.log" 2>&1 &
app_pid=$!

running() { kill -0 "$app_pid" 2>/dev/null; }

waited=0
until xdotool search --onlyvisible --name '^SqlStudio$' >/dev/null 2>&1; do
    running || { cat "$OUT_DIR/console.log"; fail "sql-studio exited before showing its main window"; }
    (( waited < WINDOW_TIMEOUT )) || fail "main window did not appear within ${WINDOW_TIMEOUT}s"
    sleep 1
    (( ++waited ))
done
echo "Main window appeared after ${waited}s."

sleep "$SETTLE_TIME"
running || { cat "$OUT_DIR/console.log"; fail "sql-studio exited within ${SETTLE_TIME}s of starting"; }
echo "Still running after another ${SETTLE_TIME}s."

# Python plugins and modules report load errors as tracebacks.
logs=("$OUT_DIR/console.log")
[[ -f $HOME/.mysql/studio/log/wb.log ]] && logs+=("$HOME/.mysql/studio/log/wb.log")
if grep -n 'Traceback' "${logs[@]}"; then
    fail "Python errors in the application output"
fi

# The app is meant to recognise every distro CI builds a package for
# (isOsSupported() in backend/wbprivate/studio/wb_module.cpp). Its warning
# dialog has no window title on a display without a window manager, so check
# the log.
if grep -n 'OS not found on supported OS list' "${logs[@]}"; then
    fail "the application reports an unsupported operating system"
fi

# Errors logged by the application itself are reported but not fatal: some
# depend on the environment (e.g. no lsb_release in a minimal container).
grep -h '\[ERR\]' "${logs[@]}" | sed 's/^/::warning::/' || true

echo "Open windows:"
xdotool search --onlyvisible --name . | while read -r w; do echo "  $(xdotool getwindowname "$w")"; done
echo "Smoke test passed."
