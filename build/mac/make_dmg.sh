#!/usr/bin/env bash
#
# make_dmg.sh — Package SqlStudio.app as a .dmg with the drag-to-Applications
# window (background and icon positions from dmg_settings.py).
#
# Usage:
#   ./make_dmg.sh SqlStudio.app OUTPUT.dmg
#
# Needs Python 3.10 or newer (PYTHON, default python3) to run dmgbuild, which
# is installed into a temporary virtual environment from dmg-requirements.txt.
#
set -euo pipefail

APP="${1:?usage: make_dmg.sh SqlStudio.app OUTPUT.dmg}"
OUT="${2:?usage: make_dmg.sh SqlStudio.app OUTPUT.dmg}"
PYTHON="${PYTHON:-python3}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
VERSION="$("${SOURCE_DIR}/tools/get_wb_version.sh" full_version)"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT

"${PYTHON}" -m venv "${WORK_DIR}/venv"
"${WORK_DIR}/venv/bin/pip" install --quiet --disable-pip-version-check \
    --require-hashes --no-deps -r "${SCRIPT_DIR}/dmg-requirements.txt"

mkdir -p "$(dirname "${OUT}")"
"${WORK_DIR}/venv/bin/dmgbuild" \
    -s "${SCRIPT_DIR}/dmg_settings.py" \
    -D app="${APP}" \
    -D background="${SCRIPT_DIR}/background.png" \
    "SqlStudio ${VERSION}" "${OUT}"

hdiutil verify "${OUT}"
echo "Built ${OUT}"
