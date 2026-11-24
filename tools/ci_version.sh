#!/usr/bin/env bash
#
# ci_version.sh — Derive the version of a CI build from its branch or tag
#
# The numeric version (x.y.z) comes from backend/wbprivate/studio/wb_version.h.
# The stage adds a suffix and a build number (see CONTRIBUTING.md):
#
#   develop, other branches      1.2.0-dev.<run>   APP_RELEASE_TYPE "dev"
#   releases/x.y.z               1.1.0-rc0.<run>   APP_RELEASE_TYPE "rc"
#   tag vX.Y.Z-rcN               1.1.0-rcN         APP_RELEASE_TYPE "rcN"
#   master, tag vX.Y.Z           1.1.0             APP_RELEASE_TYPE ""
#
# For a pull request the stage comes from the target branch. <run> is
# GITHUB_RUN_NUMBER; it is also the build number in every stage.
# Release branch builds are rc0.<run>, so dpkg sorts them before the first
# tagged rc1 (rc.<run> would sort after it).
#
# Usage:
#   tools/ci_version.sh [--apply]
#
# Options:
#   --apply     Write APP_RELEASE_TYPE and APP_BUILD_NUMBER into wb_version.h.
#               CI does this in its workspace before the build; never commit it.
#   -h, --help  Show this help message
#
# Environment (set by GitHub Actions; outside CI the current git branch and
# build number 0 are used):
#   GITHUB_EVENT_NAME, GITHUB_REF_TYPE, GITHUB_REF_NAME, GITHUB_BASE_REF,
#   GITHUB_RUN_NUMBER
#
# Prints, and appends to GITHUB_ENV when it is set:
#   APP_VERSION       numeric version, e.g. 1.1.0 (MSI ProductVersion, macOS
#                     CFBundleShortVersionString)
#   RELEASE_TYPE      dev, rc, rcN or empty
#   BUILD_NUMBER      the run number
#   VERSION_SUFFIX    dev.<run>, rc0.<run>, rcN or empty
#   PACKAGE_VERSION   APP_VERSION plus -VERSION_SUFFIX, for file names
#
set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADER="${SOURCE_DIR}/backend/wbprivate/studio/wb_version.h"

APPLY=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --apply)   APPLY=1; shift ;;
        -h|--help) sed -n '/^# Usage:/,/^set -euo/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

fail() { echo "::error::$*" >&2; exit 1; }

header_number() {
    local value
    value="$(sed -n "s/^#define $1 \([0-9][0-9]*\).*/\1/p" "${HEADER}")"
    [[ -n "${value}" ]] || fail "$1 is missing from ${HEADER}"
    echo "${value}"
}

APP_VERSION="$(header_number APP_MAJOR_NUMBER).$(header_number APP_MINOR_NUMBER).$(header_number APP_RELEASE_NUMBER)"
BUILD_NUMBER="${GITHUB_RUN_NUMBER:-0}"
[[ "${BUILD_NUMBER}" =~ ^[0-9]+$ ]] || fail "GITHUB_RUN_NUMBER is not a number: ${BUILD_NUMBER}"

if [[ "${GITHUB_REF_TYPE:-}" == "tag" ]]; then
    tag="${GITHUB_REF_NAME}"
    [[ "${tag}" =~ ^v([0-9]+\.[0-9]+\.[0-9]+)(-(rc[1-9][0-9]*))?$ ]] ||
        fail "Tag ${tag} is not vX.Y.Z or vX.Y.Z-rcN"
    [[ "${BASH_REMATCH[1]}" == "${APP_VERSION}" ]] ||
        fail "Tag ${tag} does not match the code version ${APP_VERSION} (wb_version.h)"
    RELEASE_TYPE="${BASH_REMATCH[3]}"
    VERSION_SUFFIX="${RELEASE_TYPE}"
else
    if [[ "${GITHUB_EVENT_NAME:-}" == "pull_request" ]]; then
        branch="${GITHUB_BASE_REF}"
    else
        branch="${GITHUB_REF_NAME:-$(git -C "${SOURCE_DIR}" rev-parse --abbrev-ref HEAD 2>/dev/null || echo local)}"
    fi
    case "${branch}" in
        master)
            RELEASE_TYPE=""
            VERSION_SUFFIX="" ;;
        releases/*)
            # releases/1.1.0 must build 1.1.0; anything else is a missed
            # set_version (a hotfix branch) or a typo in the branch name.
            name="${branch#releases/}"
            if [[ "${name}" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ && "${name}" != "${APP_VERSION}" ]]; then
                fail "Branch ${branch} holds version ${APP_VERSION} (wb_version.h); run ./set_version ${name} on it"
            fi
            RELEASE_TYPE="rc"
            VERSION_SUFFIX="rc0.${BUILD_NUMBER}" ;;
        *)
            RELEASE_TYPE="dev"
            VERSION_SUFFIX="dev.${BUILD_NUMBER}" ;;
    esac
fi

PACKAGE_VERSION="${APP_VERSION}${VERSION_SUFFIX:+-${VERSION_SUFFIX}}"

if [[ ${APPLY} -eq 1 ]]; then
    sed -e "s/^#define APP_RELEASE_TYPE .*/#define APP_RELEASE_TYPE \"${RELEASE_TYPE}\"/" \
        -e "s/^#define APP_BUILD_NUMBER .*/#define APP_BUILD_NUMBER ${BUILD_NUMBER}/" \
        "${HEADER}" > "${HEADER}.tmp"
    mv "${HEADER}.tmp" "${HEADER}"
    if ! grep -q "^#define APP_RELEASE_TYPE \"${RELEASE_TYPE}\"$" "${HEADER}" ||
       ! grep -q "^#define APP_BUILD_NUMBER ${BUILD_NUMBER}$" "${HEADER}"; then
        fail "Could not write the version into ${HEADER}"
    fi
fi

for var in APP_VERSION RELEASE_TYPE BUILD_NUMBER VERSION_SUFFIX PACKAGE_VERSION; do
    echo "${var}=${!var}"
    [[ -z "${GITHUB_ENV:-}" ]] || echo "${var}=${!var}" >> "${GITHUB_ENV}"
done
