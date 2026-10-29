#!/usr/bin/env bash
#
# Smoke test for a built SqlStudio.app: starts it, waits for one of its
# windows to appear and checks that the application is still running a while
# later without Python errors.
#
# Usage: smoke_test.sh SqlStudio.app [output-dir]
#
# The output directory receives the console output, the application's own
# log (wb.log) and, in CI only (CI is set), a screenshot of the desktop, for
# inspection when the test fails.

set -euo pipefail

APP="${1:?usage: smoke_test.sh SqlStudio.app [output-dir]}"
OUT_DIR="${2:-smoke-test}"
WINDOW_TIMEOUT=${SMOKE_WINDOW_TIMEOUT:-120}   # seconds to wait for a window
SETTLE_TIME=${SMOKE_SETTLE_TIME:-20}          # seconds it must keep running after that
WB_LOG="${HOME}/Library/Application Support/MySQL/SqlStudio/log/wb.log"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
mkdir -p "${OUT_DIR}"
OUT_DIR="$(cd "${OUT_DIR}" && pwd)"

fail() {
    echo "::error::Smoke test: $*"
    exit 1
}

app_pid=
cleanup() {
    # Collected on every exit, so a failure can be inspected too. The
    # screenshot shows the whole desktop, so it is only taken in CI, never on
    # a developer's machine.
    if [[ -n "${CI:-}" ]]; then
        screencapture -x "${OUT_DIR}/screenshot.png" 2>/dev/null || true
    fi
    cp "${WB_LOG}" "${OUT_DIR}/" 2>/dev/null || true
    [[ -n ${app_pid} ]] && kill "${app_pid}" 2>/dev/null || true
}
trap cleanup EXIT

# The previous run's log would hide errors of this one.
rm -f "${WB_LOG}"

echo "Starting ${APP}..."
"${APP}/Contents/MacOS/SqlStudio" > "${OUT_DIR}/console.log" 2>&1 &
app_pid=$!

running() { kill -0 "${app_pid}" 2>/dev/null; }
window_count() { swift "${SCRIPT_DIR}/count_windows.swift" "${app_pid}" 2>/dev/null || echo 0; }

waited=0
until [[ "$(window_count)" -gt 0 ]]; do
    running || { cat "${OUT_DIR}/console.log"; fail "SqlStudio exited before showing a window"; }
    (( waited < WINDOW_TIMEOUT )) || fail "no window appeared within ${WINDOW_TIMEOUT}s"
    sleep 2
    (( waited += 2 ))
done
echo "First window appeared after about ${waited}s."

sleep "${SETTLE_TIME}"
running || { cat "${OUT_DIR}/console.log"; fail "SqlStudio exited within ${SETTLE_TIME}s of starting"; }
echo "Still running after another ${SETTLE_TIME}s, $(window_count) window(s) open."

# Python plugins and modules report load errors as tracebacks.
logs=("${OUT_DIR}/console.log")
[[ -f "${WB_LOG}" ]] && logs+=("${WB_LOG}")
if grep -n 'Traceback' "${logs[@]}"; then
    fail "Python errors in the application output"
fi

# Errors logged by the application itself are reported but not fatal.
grep -h '\[ERR\]' "${logs[@]}" | sed 's/^/::warning::/' || true

echo "Smoke test passed."
