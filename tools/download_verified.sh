#!/usr/bin/env bash
#
# download_verified.sh — Download a file and check its SHA-256, trying each URL
#
# Used by CI (.github/workflows/build.yml) for downloads that are not part of
# the 3rd-party bundle, such as the ANTLR jar.
#
# Usage:
#   tools/download_verified.sh DEST SHA256 URL [URL...]
#
# A DEST that already exists (e.g. restored from a cache) is kept if its
# checksum matches, and downloaded again otherwise. The URLs are tried in
# order. Each download goes to a temporary file and replaces DEST only once its
# checksum matches, so a broken file or an error page served with HTTP 200
# moves on to the next URL. Retries have a time limit, so a long Retry-After
# from one server cannot hold the job up before the next URL is tried, and a
# transfer that stalls is abandoned while a slow but steady one may finish.
#
set -euo pipefail

[[ $# -ge 3 ]] || { sed -n '/^# Usage:/,/^# A DEST/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//' >&2; exit 2; }

dest="$1"
sha256="$2"
shift 2

[[ "${sha256}" =~ ^[0-9a-f]{64}$ ]] || { echo "::error::Not a SHA-256 checksum: ${sha256}" >&2; exit 2; }

checksum() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

[[ ! -d "${dest}" ]] || { echo "::error::${dest} is a directory" >&2; exit 2; }

if [[ -f "${dest}" ]]; then
    if [[ "$(checksum "${dest}")" == "${sha256}" ]]; then
        echo "Using $(basename "${dest}") (checksum OK)"
        exit 0
    fi
    echo "::warning::$(basename "${dest}") has the wrong checksum; downloading it again"
    rm -f "${dest}"
fi

mkdir -p "$(dirname "${dest}")"
tmp="$(mktemp "${dest}.XXXXXX")"
trap 'rm -f "${tmp}"' EXIT

for url in "$@"; do
    # Below 1 KB/s for a minute counts as stalled.
    if ! curl -fsSL --retry 4 --retry-max-time 120 --connect-timeout 20 \
            --speed-limit 1024 --speed-time 60 -o "${tmp}" "${url}"; then
        echo "::warning::Download failed: ${url}"
        continue
    fi
    if [[ "$(checksum "${tmp}")" != "${sha256}" ]]; then
        echo "::warning::Wrong checksum: ${url}"
        continue
    fi
    chmod 644 "${tmp}"    # mktemp creates it as 600
    mv "${tmp}" "${dest}"
    echo "Downloaded $(basename "${dest}") from ${url}"
    exit 0
done

echo "::error::Could not download $(basename "${dest}") with SHA-256 ${sha256}" >&2
exit 1
