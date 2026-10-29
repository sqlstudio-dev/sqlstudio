#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECT_PARENT="$(cd "${PROJECT_ROOT}/.." && pwd)"

# Default to sibling ../bundle unless caller explicitly overrides.
if [[ -z "${SQLSTUDIO_BUNDLE:-}" && -z "${WB_BUNDLE_DIR:-}" ]]; then
    export SQLSTUDIO_BUNDLE="${PROJECT_PARENT}/bundle"
fi

usage() {
    cat <<'EOF'
Usage: ./build/build_3rdparty.sh [options]

Dispatches to the platform-specific 3rd-party builder:
  Linux  -> build/linux/build_3rdparty.sh
  macOS  -> build/mac/build_3rdparty.sh
  Windows-> build/windows/build_3rdparty.ps1

All options are forwarded to the selected script.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

case "$(uname -s)" in
    Linux)
        exec "${SCRIPT_DIR}/linux/build_3rdparty.sh" "$@"
        ;;
    Darwin)
        exec "${SCRIPT_DIR}/mac/build_3rdparty.sh" "$@"
        ;;
    MINGW*|MSYS*|CYGWIN*)
        if command -v pwsh >/dev/null 2>&1; then
            exec pwsh -File "${SCRIPT_DIR}/windows/build_3rdparty.ps1" "$@"
        fi
        echo "pwsh is required to run the Windows 3rd-party build script." >&2
        exit 1
        ;;
    *)
        echo "Unsupported platform: $(uname -s)" >&2
        exit 1
        ;;
esac
