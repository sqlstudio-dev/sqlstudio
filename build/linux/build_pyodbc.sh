#!/usr/bin/env bash
#
# build_pyodbc.sh — Build the pyodbc module against the bundle's iODBC
#
# The migration plugins (MSSQL, Access, Sybase, generic ODBC) import pyodbc.
# The distro's python3-pyodbc links unixODBC, while SqlStudio ships iODBC, so
# the module is compiled here against the bundled driver manager, the way
# pyodbc's setup.py would, with the distro's Python headers.
#
# Usage:
#   ./build_pyodbc.sh --bundle DIR --sdist FILE --out DIR
#
# Options:
#   --bundle DIR   Bundle built by build_3rdparty.sh (provides iODBC)  (required)
#   --sdist FILE   pyodbc source archive (.tar.gz)                     (required)
#   --out DIR      Directory to put the module in, e.g. the staged
#                  usr/lib/sql-studio, which is on the app's PYTHONPATH (required)
#   -h, --help     Show this help message
#
set -euo pipefail

BUNDLE_DIR=""
SDIST=""
OUT_DIR=""

usage() {
    sed -n '/^# Usage:/,/^#$/p;/^# Options:/,/^#$/p' "$0" | sed 's/^# \{0,1\}//'
    exit 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle) BUNDLE_DIR="$2"; shift 2 ;;
        --sdist)  SDIST="$2";      shift 2 ;;
        --out)    OUT_DIR="$2";    shift 2 ;;
        -h|--help) usage ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

[[ -f "${BUNDLE_DIR}/include/sql.h" ]] || { echo "--bundle must contain iODBC (include/sql.h)" >&2; exit 1; }
[[ -f "${SDIST}" ]]                    || { echo "--sdist must point at a pyodbc .tar.gz" >&2; exit 1; }
[[ -n "${OUT_DIR}" ]]                  || { echo "--out is required" >&2; exit 1; }

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "${WORK_DIR}"' EXIT
tar -xzf "${SDIST}" -C "${WORK_DIR}"
src="$(echo "${WORK_DIR}"/pyodbc-*)"
version="${src##*/pyodbc-}"
suffix="$(python3 -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"

mkdir -p "${OUT_DIR}"
# iODBC 3.52 does not define the ODBC 3.8 constant pyodbc refers to; it is
# only used when a script sets pyodbc.odbcversion = "3.8". $ORIGIN finds the
# bundled iODBC next to the module.
g++ -std=c++17 -O2 -fPIC -shared -DNDEBUG -Wno-write-strings \
    -DPYODBC_VERSION="${version}" -DSQL_OV_ODBC3_80=380UL \
    $(python3-config --includes) -I"${BUNDLE_DIR}/include" \
    "${src}"/src/*.cpp \
    -L"${BUNDLE_DIR}/lib" -liodbc -liodbcinst -Wl,-rpath,'$ORIGIN' \
    -o "${OUT_DIR}/pyodbc${suffix}"

echo "Built ${OUT_DIR}/pyodbc${suffix} (pyodbc ${version})"
