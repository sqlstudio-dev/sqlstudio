#!/usr/bin/env bash
#
# prepare_3rdparty_ce.sh — Lay out 3rd-party-ce/ the way SqlStudio.xcodeproj
# expects it.
#
# The Xcode project reads headers, libraries and tools from <repo>/3rd-party-ce.
# This script fills that directory from three sources:
#   - the bundle built by build/mac/build_3rdparty.sh (MySQL client,
#     Connector/C++, GDAL, ANTLR runtime, libssh, SQLite, vsqlite++, ...),
#   - Homebrew (glib, cairo, pixman, libpng, libzip, OpenSSL, PROJ),
#   - the python.org installer (Python.framework) and the ANTLR tool jar.
#
# Entries are symlinks where Xcode only reads them, so the directory is cheap
# to recreate. Libraries that come from Homebrew are made self-contained in the
# finished app by a later step, not here.
#
# Usage:
#   ./prepare_3rdparty_ce.sh --bundle DIR --python-pkg FILE --antlr-jar FILE
#
# Options:
#   --bundle DIR        Bundle built by build_3rdparty.sh             (required)
#   --python-pkg FILE   python.org macOS installer (.pkg) for Python 3.12 (required)
#   --antlr-jar FILE    antlr-<version>-complete.jar                  (required)
#   --pyodbc-sdist FILE pyodbc source archive (.tar.gz); builds the pyodbc
#                       module used by the migration plugins       (optional)
#   --out DIR           Target directory          (default: <repo>/3rd-party-ce)
#   -h, --help          Show this help message
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUNDLE_DIR=""
PYTHON_PKG=""
ANTLR_JAR=""
PYODBC_SDIST=""
OUT_DIR="${SOURCE_DIR}/3rd-party-ce"

usage() {
    sed -n '/^# Usage:/,/^#$/p;/^# Options:/,/^#$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle)     BUNDLE_DIR="$2"; shift 2 ;;
        --python-pkg) PYTHON_PKG="$2"; shift 2 ;;
        --antlr-jar)  ANTLR_JAR="$2";  shift 2 ;;
        --pyodbc-sdist) PYODBC_SDIST="$2"; shift 2 ;;
        --out)        OUT_DIR="$2";    shift 2 ;;
        -h|--help)    usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

fail() { echo "error: $*" >&2; exit 1; }

[[ -d "${BUNDLE_DIR}/lib" ]] || fail "--bundle must point at a bundle built by build_3rdparty.sh"
[[ -f "${PYTHON_PKG}" ]]     || fail "--python-pkg must point at the python.org .pkg installer"
[[ -f "${ANTLR_JAR}" ]]      || fail "--antlr-jar must point at the ANTLR complete jar"
command -v brew >/dev/null   || fail "Homebrew is required"

BUNDLE_DIR="$(cd "${BUNDLE_DIR}" && pwd)"

mkdir -p "${OUT_DIR}"/{include,lib/mysql,lib/plugin,lib/glib-2.0,bin,Python}

link() { ln -sfn "$1" "$2"; }

# ── Bundle ─────────────────────────────────────────────────────────────────
for entry in "${BUNDLE_DIR}"/include/*; do
    link "${entry}" "${OUT_DIR}/include/$(basename "${entry}")"
done
# The project includes <cppconn/...> and <mysql_connection.h>; Connector/C++
# installs these legacy headers under jdbc/.
for entry in "${BUNDLE_DIR}"/include/jdbc/*; do
    link "${entry}" "${OUT_DIR}/include/$(basename "${entry}")"
done

for lib in "${BUNDLE_DIR}"/lib/*.dylib; do
    case "$(basename "${lib}")" in
        libgtest*|libgmock*) continue ;;   # test-only libraries
    esac
    link "${lib}" "${OUT_DIR}/lib/$(basename "${lib}")"
done
for lib in "${BUNDLE_DIR}"/lib/libmysqlclient*.dylib; do
    link "${lib}" "${OUT_DIR}/lib/mysql/$(basename "${lib}")"
done
for plugin in "${BUNDLE_DIR}"/lib/plugin/*.so; do
    [[ -e "${plugin}" ]] && link "${plugin}" "${OUT_DIR}/lib/plugin/$(basename "${plugin}")"
done
# The project expects SQLite under its Linux name.
cp -f "${BUNDLE_DIR}/lib/libsqlite3.dylib" "${OUT_DIR}/lib/libsqlite3.so"

for tool in mysql mysqldump ogrinfo ogr2ogr; do
    link "${BUNDLE_DIR}/bin/${tool}" "${OUT_DIR}/bin/${tool}"
done
cp -f "${ANTLR_JAR}" "${OUT_DIR}/bin/$(basename "${ANTLR_JAR}")"

# ── Homebrew ───────────────────────────────────────────────────────────────
brew_lib() {   # <formula> <library file>...
    local prefix
    prefix="$(brew --prefix "$1")"
    shift
    for name in "$@"; do
        [[ -e "${prefix}/lib/${name}" ]] || fail "${prefix}/lib/${name} not found (brew install $(basename "${prefix}"))"
        link "${prefix}/lib/${name}" "${OUT_DIR}/lib/${name}"
    done
}
brew_lib glib      libglib-2.0.0.dylib libgmodule-2.0.0.dylib
brew_lib cairo     libcairo.2.dylib
brew_lib pixman    libpixman-1.0.dylib
brew_lib libpng    libpng16.16.dylib
brew_lib libzip    libzip.dylib
brew_lib openssl@3 libcrypto.3.dylib libssl.3.dylib
brew_lib proj      libproj.dylib

link "$(brew --prefix glib)/include/glib-2.0"           "${OUT_DIR}/include/glib-2.0"
link "$(brew --prefix glib)/lib/glib-2.0/include"       "${OUT_DIR}/lib/glib-2.0/include"
link "$(brew --prefix cairo)/include/cairo"             "${OUT_DIR}/include/cairo"
link "$(brew --prefix libzip)/include/zip.h"            "${OUT_DIR}/include/zip.h"
link "$(brew --prefix libzip)/include/zipconf.h"        "${OUT_DIR}/include/zipconf.h"

# ── Python.framework ──────────────────────────────────────────────────────
# Taken from the installer's payload without installing it system-wide.
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
pkgutil --expand-full "${PYTHON_PKG}" "${WORK_DIR}/pkg"
rm -rf "${OUT_DIR}/Python/Python.framework"
cp -R "${WORK_DIR}/pkg/Python_Framework.pkg/Payload" "${OUT_DIR}/Python/Python.framework"

# ── pyodbc ────────────────────────────────────────────────────────────────
# Compiled directly against the embedded Python's headers and the bundle's
# iODBC, the way pyodbc's setup.py would, so it needs no host Python.
# iODBC 3.52 does not define the ODBC 3.8 constant pyodbc refers to; it is
# only used when a script sets pyodbc.odbcversion = "3.8".
if [[ -n "${PYODBC_SDIST}" ]]; then
    [[ -f "${PYODBC_SDIST}" ]] || fail "--pyodbc-sdist must point at a pyodbc .tar.gz"
    tar -xzf "${PYODBC_SDIST}" -C "${WORK_DIR}"
    pyodbc_src="$(echo "${WORK_DIR}"/pyodbc-*)"
    pyodbc_version="${pyodbc_src##*/pyodbc-}"
    py_dir="$(echo "${OUT_DIR}"/Python/Python.framework/Versions/3.*)"
    py_version="${py_dir##*/}"
    mkdir -p "${OUT_DIR}/Python/pyodbc"
    xcrun clang++ -std=c++17 -O2 -arch "$(uname -m)" \
        -mmacosx-version-min="${MACOSX_DEPLOYMENT_TARGET:-26.0}" \
        -Wno-write-strings -Wno-deprecated-declarations -DNDEBUG \
        -DPYODBC_VERSION="${pyodbc_version}" -DSQL_OV_ODBC3_80=380UL \
        -I"${py_dir}/include/python${py_version}" -I"${BUNDLE_DIR}/include" \
        "${pyodbc_src}"/src/*.cpp \
        -bundle -undefined dynamic_lookup \
        -L"${BUNDLE_DIR}/lib" -liodbc -liodbcinst \
        -o "${OUT_DIR}/Python/pyodbc/pyodbc.cpython-${py_version/./}-darwin.so"
    echo "pyodbc ${pyodbc_version} built for Python ${py_version}"
fi

echo "3rd-party-ce prepared in ${OUT_DIR}"
