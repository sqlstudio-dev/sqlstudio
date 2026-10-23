#!/usr/bin/env bash
#
# make_deb.sh — Package an installed SqlStudio tree as a .deb
#
# Takes the tree produced by `DESTDIR=<stage> cmake --install <build-dir>`
# (configured with CMAKE_INSTALL_PREFIX=/usr) and wraps it in a Debian
# package. Runtime dependencies on distro packages are computed with
# dpkg-shlibdeps, so the package must be built on the distro it targets.
#
# Usage:
#   ./make_deb.sh --stage DIR [--out DIR] [--release N] [--distro-tag TAG]
#
# Options:
#   --stage DIR       Installed tree (contains usr/)               (required)
#   --out DIR         Where to write the .deb                       (default: .)
#   --release N       Debian revision                               (default: 1)
#   --distro-tag TAG  Version suffix, e.g. ubuntu22.04              (default: from /etc/os-release)
#   -h, --help        Show this help message
#
# Environment:
#   DEB_MAINTAINER    Overrides the Maintainer field
#   DEB_HOMEPAGE      Overrides the Homepage field
#
set -euo pipefail

PACKAGE="sql-studio-community"
MAINTAINER="${DEB_MAINTAINER:-Marcin Marek <marcin@dev4fun.net>}"
HOMEPAGE="${DEB_HOMEPAGE:-https://www.dev4fun.net/}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

STAGE_DIR=""
OUT_DIR="."
RELEASE="1"
DISTRO_TAG=""

usage() {
    sed -n '/^# Usage:/,/^#$/p;/^# Options:/,/^#$/p;/^# Environment:/,/^#$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)      STAGE_DIR="$2";  shift 2 ;;
        --out)        OUT_DIR="$2";    shift 2 ;;
        --release)    RELEASE="$2";    shift 2 ;;
        --distro-tag) DISTRO_TAG="$2"; shift 2 ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -n "${STAGE_DIR}" && -d "${STAGE_DIR}/usr" ]] || {
    echo "--stage must point at an installed tree containing usr/" >&2
    exit 1
}

for tool in dpkg-deb dpkg-shlibdeps dpkg-query dpkg-architecture objdump strip patchelf; do
    command -v "$tool" >/dev/null || { echo "$tool not found (run build/linux/install_deps.sh)" >&2; exit 1; }
done

if [[ -z "${DISTRO_TAG}" ]]; then
    # shellcheck disable=SC1091
    DISTRO_TAG="$(. /etc/os-release && echo "${ID}${VERSION_ID}")"
fi

UPSTREAM_VERSION="$("${SOURCE_DIR}/tools/get_wb_version.sh" full_version)"
VERSION="${UPSTREAM_VERSION}-${RELEASE}~${DISTRO_TAG}"
ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"
LIB_DIR="usr/lib/sql-studio"

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
PKG_DIR="${WORK_DIR}/${PACKAGE}"

cp -a "${STAGE_DIR}" "${PKG_DIR}"
mkdir -p "${PKG_DIR}/DEBIAN"

# ── Runtime dependencies ──────────────────────────────────────────────────
# dpkg-shlibdeps needs a debian/control in its working directory.
mkdir -p "${WORK_DIR}/shlibs/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "${PACKAGE}" "${PACKAGE}" \
    > "${WORK_DIR}/shlibs/debian/control"

mapfile -t ELF_FILES < <(
    find "${PKG_DIR}/usr" -type f -print0 |
    while IFS= read -r -d '' f; do
        [[ "$(head -c 4 "$f" | od -An -c | tr -d ' ')" == '177ELF' ]] && echo "$f"
    done
)
[[ ${#ELF_FILES[@]} -gt 0 ]] || { echo "No ELF files found in ${STAGE_DIR}" >&2; exit 1; }

# ── Binaries ──────────────────────────────────────────────────────────────
for f in "${ELF_FILES[@]}"; do
    # Keep only $ORIGIN-relative RUNPATH entries; absolute ones point into the
    # build machine (e.g. SQLite's configure adds the bundle's lib dir).
    runpath="$(patchelf --print-rpath "$f")"
    if [[ -n "$runpath" ]]; then
        kept="$(tr ':' '\n' <<<"$runpath" | grep '^\$ORIGIN' | paste -sd: || true)"
        if [[ -z "$kept" ]]; then
            patchelf --remove-rpath "$f"
        elif [[ "$kept" != "$runpath" ]]; then
            patchelf --set-rpath "$kept" "$f"
        fi
    fi
    # Same as dh_strip: symbols are not needed at run time.
    if [[ "$f" == *.so || "$f" == *.so.* ]]; then
        strip --remove-section=.comment --remove-section=.note --strip-unneeded "$f"
    else
        strip --remove-section=.comment --remove-section=.note "$f"
    fi
done

# Libraries bundled in LIB_DIR satisfy each other and are not distro
# packages, so their missing package info is expected.
SHLIBS_DEPENDS="$(
    cd "${WORK_DIR}/shlibs" &&
    dpkg-shlibdeps -O --ignore-missing-info \
        -l"${PKG_DIR}/${LIB_DIR}" -l"${PKG_DIR}/${LIB_DIR}/plugins" -l"${PKG_DIR}/${LIB_DIR}/modules" \
        "${ELF_FILES[@]}" 2>"${WORK_DIR}/shlibdeps.log" |
    sed -n 's/^shlibs:Depends=//p'
)" || { cat "${WORK_DIR}/shlibdeps.log" >&2; exit 1; }
[[ -n "${SHLIBS_DEPENDS}" ]] || { cat "${WORK_DIR}/shlibdeps.log" >&2; exit 1; }

# dpkg-shlibdeps maps a bundled library to the distro package that ships the
# same SONAME when one is installed (e.g. the bundled OpenSSL 3 on Ubuntu
# 26.10 → libssl3t64). The package already contains those libraries, so
# drop such dependencies instead of installing a second copy.
declare -A BUNDLED_PKGS=()
for lib in "${PKG_DIR}/${LIB_DIR}"/*.so.*; do
    [[ -f "$lib" && ! -L "$lib" ]] || continue
    soname="$(objdump -p "$lib" 2>/dev/null | awk '$1 == "SONAME" { print $2 }')"
    [[ -n "$soname" ]] || continue
    while IFS= read -r pkg; do
        [[ -n "$pkg" ]] && BUNDLED_PKGS["$pkg"]=1
    done < <(dpkg-query -S "*/${soname}" 2>/dev/null | sed -n 's/^\([^:,]*\).*/\1/p')
done
if [[ ${#BUNDLED_PKGS[@]} -gt 0 ]]; then
    kept=()
    IFS=',' read -ra deps <<<"${SHLIBS_DEPENDS}"
    for dep in "${deps[@]}"; do
        dep="${dep# }"
        name="${dep%% *}"
        if [[ -n "${BUNDLED_PKGS[${name%%:*}]:-}" ]]; then
            echo "Dropped dependency on ${dep}: the library is bundled"
        else
            kept+=("$dep")
        fi
    done
    SHLIBS_DEPENDS="$(printf '%s, ' "${kept[@]}")"
    SHLIBS_DEPENDS="${SHLIBS_DEPENDS%, }"
fi

# ── Documentation required by Debian policy ───────────────────────────────
DOC_DIR="${PKG_DIR}/usr/share/doc/${PACKAGE}"
mkdir -p "${DOC_DIR}"

cat > "${DOC_DIR}/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: SqlStudio
Source: https://github.com/marcinm1980/sqlstudio

Files: *
Copyright: 2007-2019 Oracle and/or its affiliates
           2026 dev4fun
License: GPL-2
 This program is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License, version 2.0, as
 published by the Free Software Foundation, with the additional permissions
 described in License.txt in this directory, which also lists the licenses
 of the bundled third-party libraries.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 2 can be found in /usr/share/common-licenses/GPL-2.
EOF

GIT_COMMIT="${GITHUB_SHA:0:8}"
[[ -n "${GIT_COMMIT}" ]] || GIT_COMMIT="$(git -C "${SOURCE_DIR}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
cat <<EOF | gzip -9n > "${DOC_DIR}/changelog.Debian.gz"
${PACKAGE} (${VERSION}) unstable; urgency=medium

  * Package built from commit ${GIT_COMMIT}.

 -- ${MAINTAINER}  $(LC_ALL=C date -R)
EOF

# Lintian reports these on purpose-made choices; see the comments.
mkdir -p "${PKG_DIR}/usr/share/lintian/overrides"
cat > "${PKG_DIR}/usr/share/lintian/overrides/${PACKAGE}" <<EOF
# The MySQL client, Connector/C++, GDAL, libssh, SQLite and others are built
# from source and shipped in /usr/lib/sql-studio (build/linux/list.txt), so
# the package does not depend on the distro's versions of them.
${PACKAGE}: embedded-library
EOF

# ── Control files ─────────────────────────────────────────────────────────
INSTALLED_SIZE="$(du -sk --exclude=DEBIAN "${PKG_DIR}" | cut -f1)"

cat > "${PKG_DIR}/DEBIAN/control" <<EOF
Package: ${PACKAGE}
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_SIZE}
Depends: ${SHLIBS_DEPENDS}
Suggests: gnome-keyring
Section: database
Priority: optional
Homepage: ${HOMEPAGE}
Description: Visual database design, administration and development tool
 SqlStudio is a development, administration and data modeling tool for
 MySQL and MariaDB. Besides editing and running SQL queries and scripts,
 it supports the design of databases through EER diagrams, server
 administration, performance monitoring and migration from other RDBMS
 products.
EOF

( cd "${PKG_DIR}" && find usr -type f -print0 | xargs -0 md5sum ) > "${PKG_DIR}/DEBIAN/md5sums"

mkdir -p "${OUT_DIR}"
DEB_FILE="${OUT_DIR}/${PACKAGE}_${VERSION}_${ARCH}.deb"
dpkg-deb --root-owner-group -Zxz --build "${PKG_DIR}" "${DEB_FILE}"

echo "Built ${DEB_FILE}"
echo "Depends: ${SHLIBS_DEPENDS}"
