#!/usr/bin/env bash
#
# build_3rdparty.sh — Download, build, and install 3rd-party dependencies
# for SqlStudio into a self-contained bundle directory.
#
# The bundle directory produced by this script is consumed by compile.sh
# via the WB_BUNDLE_DIR / SQLSTUDIO_BUNDLE environment variable and
# feeds CMake with the include/, lib/, and bin/ paths it needs.
#
# Usage:
#   ./build_3rdparty.sh [options]
#
# Options:
#   --bundle DIR        Root of the bundle directory
#   --list FILE         Path to the dependency list (default: list.txt)
#   --clean             Remove build directories before rebuilding
#   --download-only     Download archives only — do not extract or build
#   --build-only        Skip downloads — archives must already exist
#   -j, --jobs N        Parallel make jobs (default: hw.ncpu)
#   --verbose           Stream full subprocess output to the terminal
#   -h, --help          Show this help message
#
# Environment:
#   SQLSTUDIO_BUNDLE  Fallback bundle path when --bundle is not given.
#   WB_BUNDLE_DIR       Alternative fallback bundle path.
#
# Dependencies list (list.txt):
#   Each non-empty, non-comment line uses pipe-delimited fields:
#       name|version|url
#   Archives may be .tar.gz, .tar.bz2, .tar.xz, or .zip.
#
set -euo pipefail

###############################################################################
# Colours (disabled when stdout is not a terminal)
###############################################################################
if [[ -t 1 ]]; then
    C_RESET='\033[0m'
    C_BOLD='\033[1m'
    C_DIM='\033[2m'
    C_RED='\033[1;31m'
    C_GREEN='\033[1;32m'
    C_YELLOW='\033[1;33m'
    C_BLUE='\033[1;34m'
    C_MAGENTA='\033[1;35m'
    C_CYAN='\033[1;36m'
    C_WHITE='\033[1;37m'
else
    C_RESET='' C_BOLD='' C_DIM='' C_RED='' C_GREEN=''
    C_YELLOW='' C_BLUE='' C_MAGENTA='' C_CYAN='' C_WHITE=''
fi

###############################################################################
# Logging helpers
###############################################################################
info()    { echo -e "  ${C_CYAN}[INFO]${C_RESET}    $*"; }
ok()      { echo -e "  ${C_GREEN}[ OK ]${C_RESET}    $*"; }
warn()    { echo -e "  ${C_YELLOW}[WARN]${C_RESET}    $*"; }
fail()    { echo -e "  ${C_RED}[FAIL]${C_RESET}    $*" >&2; }
section() { echo -e "\n${C_MAGENTA}${C_BOLD}── $* ──${C_RESET}"; }
detail()  { echo -e "    ${C_DIM}$*${C_RESET}"; }

banner() {
    echo -e "${C_BLUE}${C_BOLD}"
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║  SqlStudio — macOS 3rd-Party Dependency Builder      ║"
    echo "╚══════════════════════════════════════════════════════════╝"
    echo -e "${C_RESET}"
}

# Print a step header with a Unicode progress bar
# Usage: step_header <current> <total> <label>
step_header() {
    local idx=$1 total=$2 label=$3
    local bar_len=30 filled pct bar=""

    if [[ $total -gt 0 ]]; then
        pct=$(( idx * 100 / total ))
        filled=$(( bar_len * idx / total ))
    else
        pct=0; filled=0
    fi

    local i
    for (( i = 0; i < filled; i++ ));    do bar+="█"; done
    for (( i = filled; i < bar_len; i++ )); do bar+="░"; done

    echo -e "\n${C_BLUE}${C_BOLD}[${idx}/${total}]${C_RESET} ${C_WHITE}${bar}${C_RESET}  ${C_BOLD}${label}${C_RESET}"
}

###############################################################################
# Resolve paths
###############################################################################
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PROJECT_PARENT="$(cd "${PROJECT_ROOT}/.." && pwd)"

###############################################################################
# Defaults
###############################################################################
BUNDLE_DIR=""
LIST_FILE=""
DO_CLEAN=false
DOWNLOAD_ONLY=false
BUILD_ONLY=false
VERBOSE=false
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

###############################################################################
# Usage
###############################################################################
usage() {
    cat <<'EOF'
Usage: build_3rdparty.sh [options]

Options:
  --bundle DIR        Root of the bundle directory
                      (default: $SQLSTUDIO_BUNDLE or <project>/../bundle)
  --list FILE         Dependency list file (default: list.txt beside script)
  --clean             Remove build trees before rebuilding
  --download-only     Download archives only — skip extract and build
  --build-only        Skip downloads — archives must already exist
    -j, --jobs N        Parallel make jobs (default: hw.ncpu)
  --verbose           Stream full subprocess output to the terminal
  -h, --help          Show this help

Environment:
    SQLSTUDIO_BUNDLE  Fallback bundle directory (overridden by --bundle)
    WB_BUNDLE_DIR       Alternative fallback bundle directory

Example:
  ./build_3rdparty.sh --bundle /opt/wb-deps --verbose
  ./build_3rdparty.sh --clean -j8
  ./build_3rdparty.sh --download-only
EOF
    exit 0
}

###############################################################################
# Parse arguments
###############################################################################
while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle)        BUNDLE_DIR="$2";   shift 2 ;;
        --list)          LIST_FILE="$2";    shift 2 ;;
        --clean)         DO_CLEAN=true;     shift   ;;
        --download-only) DOWNLOAD_ONLY=true; shift  ;;
        --build-only)    BUILD_ONLY=true;   shift   ;;
        -j|--jobs)       JOBS="$2";         shift 2 ;;
        --verbose)       VERBOSE=true;      shift   ;;
        -h|--help)       usage ;;
        *)
            fail "Unknown option: ${C_BOLD}$1"
            echo "Run $0 --help for usage." >&2
            exit 1
            ;;
    esac
done

###############################################################################
# Resolve bundle directory (--bundle > $SQLSTUDIO_BUNDLE > $WB_BUNDLE_DIR > default)
###############################################################################
resolve_bundle_dir() {
    if [[ -n "${BUNDLE_DIR}" ]]; then
        BUNDLE_DIR="$(cd "$(dirname "${BUNDLE_DIR}")" 2>/dev/null && pwd)/$(basename "${BUNDLE_DIR}")"
        return
    fi

    if [[ -n "${SQLSTUDIO_BUNDLE:-}" ]]; then
        BUNDLE_DIR="$(cd "$(dirname "${SQLSTUDIO_BUNDLE}")" 2>/dev/null && pwd)/$(basename "${SQLSTUDIO_BUNDLE}")"
        return
    fi

    if [[ -n "${WB_BUNDLE_DIR:-}" ]]; then
        BUNDLE_DIR="$(cd "$(dirname "${WB_BUNDLE_DIR}")" 2>/dev/null && pwd)/$(basename "${WB_BUNDLE_DIR}")"
        return
    fi

    BUNDLE_DIR="${PROJECT_PARENT}/bundle"
}

resolve_bundle_dir

# Derived paths
BUNDLE_BIN="${BUNDLE_DIR}/bin"
BUNDLE_BUILD="${BUNDLE_DIR}/build"
BUNDLE_INCLUDE="${BUNDLE_DIR}/include"
BUNDLE_LIB="${BUNDLE_DIR}/lib"
BUNDLE_SOURCE="${BUNDLE_DIR}/source"

###############################################################################
# Resolve dependency list file
###############################################################################
if [[ -z "${LIST_FILE}" ]]; then
    LIST_FILE="${SCRIPT_DIR}/list.txt"
fi
LIST_FILE="$(cd "$(dirname "${LIST_FILE}")" && pwd)/$(basename "${LIST_FILE}")"

###############################################################################
# Dependency arrays (parallel indexed)
###############################################################################
DEP_NAMES=()
DEP_VERSIONS=()
DEP_URLS=()

# Parse list.txt into arrays
parse_list() {
    local file="$1"
    local lineno=0

    while IFS= read -r raw || [[ -n "$raw" ]]; do
        (( lineno++ )) || true
        local line="${raw##*( )}"   # trim leading
        line="${line%%*( )}"        # trim trailing
        line="$(echo "$line" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"

        # skip blanks & comments
        [[ -z "$line" || "$line" == \#* ]] && continue

        IFS='|' read -r name version url <<< "$line"
        name="$(echo "$name" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
        version="$(echo "$version" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
        url="$(echo "$url" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"

        if [[ -z "$name" || -z "$version" || -z "$url" ]]; then
            fail "${file}:${lineno}: expected 3 pipe-separated fields"
            exit 1
        fi

        DEP_NAMES+=("$name")
        DEP_VERSIONS+=("$version")
        DEP_URLS+=("$url")
    done < "$file"
}

# Helpers to derive names from index
slug()         { echo "${DEP_NAMES[$1]}-${DEP_VERSIONS[$1]}"; }
archive_name() { echo "${DEP_URLS[$1]##*/}"; }

###############################################################################
# Ensure bundle directory structure
###############################################################################
ensure_bundle() {
    mkdir -p "${BUNDLE_BIN}" "${BUNDLE_BUILD}" "${BUNDLE_INCLUDE}" \
             "${BUNDLE_LIB}" "${BUNDLE_SOURCE}"
}

validate_bundle() {
    for d in "${BUNDLE_BIN}" "${BUNDLE_BUILD}" "${BUNDLE_INCLUDE}" \
             "${BUNDLE_LIB}" "${BUNDLE_SOURCE}"; do
        [[ -d "$d" ]] || return 1
    done
    return 0
}

###############################################################################
# Download with progress
###############################################################################
# Remove a partial download so the next run does not mistake it for a complete archive.
download_failed() {
    rm -f "$1"
    fail "Download failed: $2"
    exit 1
}

download_archive() {
    local idx=$1
    local url="${DEP_URLS[$idx]}"
    local dest="${BUNDLE_SOURCE}/$(archive_name "$idx")"

    if [[ -f "$dest" ]]; then
        ok "Archive already present: ${C_DIM}$(basename "$dest")${C_RESET}"
        return 0
    fi

    info "Downloading ${C_WHITE}${url}${C_RESET}"

    # Prefer wget for built-in progress bar; fall back to curl
    if command -v wget &>/dev/null; then
        if $VERBOSE; then
            wget --no-check-certificate -O "$dest" "$url" || download_failed "$dest" "$url"
        else
            # grep finds no percentages when the server sends no Content-Length
            # (e.g. GitHub tag archives), so only wget's exit status may fail this.
            set +e
            wget --no-check-certificate -O "$dest" "$url" 2>&1 | \
                { grep --line-buffered -oE '[0-9]+%' || true; } | \
                while IFS= read -r pct_str; do
                    local pct="${pct_str%\%}"
                    local bar_len=40
                    local filled=$(( bar_len * 10#$pct / 100 ))
                    local bar=""
                    local i
                    for (( i = 0; i < filled; i++ ));       do bar+="█"; done
                    for (( i = filled; i < bar_len; i++ )); do bar+="░"; done
                    printf "\r    ${C_GREEN}%s${C_RESET}  %3d%%" "$bar" "$((10#$pct))"
                done
            local rc=${PIPESTATUS[0]}
            set -e
            echo  # newline after bar
            [[ $rc -eq 0 ]] || download_failed "$dest" "$url"
        fi
    elif command -v curl &>/dev/null; then
        if $VERBOSE; then
            curl -fSL -o "$dest" "$url" || download_failed "$dest" "$url"
        else
            curl -fSL --progress-bar -o "$dest" "$url" || download_failed "$dest" "$url"
        fi
    else
        fail "Neither wget nor curl found — cannot download."
        exit 1
    fi

    if [[ ! -f "$dest" ]]; then
        fail "Download failed: ${url}"
        exit 1
    fi

    ok "Saved → ${C_DIM}${dest}${C_RESET}"
}

###############################################################################
# Extract archive
###############################################################################
extract_archive() {
    local idx=$1
    local _slug; _slug="$(slug "$idx")"
    local archive="${BUNDLE_SOURCE}/$(archive_name "$idx")"
    local target="${BUNDLE_SOURCE}/${_slug}"

    if [[ -d "$target" ]]; then
        ok "Source already extracted: ${C_DIM}${target}${C_RESET}"
        return 0
    fi

    info "Extracting ${C_DIM}$(basename "$archive")${C_RESET}"

    local tmp_target="${BUNDLE_SOURCE}/.${_slug}.extracting"
    rm -rf "$tmp_target"
    mkdir -p "$tmp_target"

    local lc_name
    lc_name="$(echo "$archive" | tr '[:upper:]' '[:lower:]')"

    case "$lc_name" in
        *.tar.gz|*.tgz)   tar xzf "$archive" -C "$tmp_target" ;;
        *.tar.bz2)        tar xjf "$archive" -C "$tmp_target" ;;
        *.tar.xz)         tar xJf "$archive" -C "$tmp_target" ;;
        *.zip)            unzip -q "$archive" -d "$tmp_target" ;;
        *)
            fail "Unsupported archive format: $(basename "$archive")"
            rm -rf "$tmp_target"
            exit 1
            ;;
    esac

    # Unwrap single top-level directory (common tarball layout)
    local children
    children=( "$tmp_target"/* )
    if [[ ${#children[@]} -eq 1 && -d "${children[0]}" ]]; then
        mv "${children[0]}" "$target"
        rm -rf "$tmp_target"
    else
        mv "$tmp_target" "$target"
    fi

    ok "Extracted → ${C_DIM}${target}${C_RESET}"
}

###############################################################################
# Run a command with optional output capture
###############################################################################
run_cmd() {
    local label="$1"; shift
    detail "\$ $*"

    local t_start elapsed rc
    t_start=$SECONDS

    local log_file
    log_file="$(mktemp)"

    # Temporarily disable errexit so a failing pipeline doesn't kill the
    # script before we can capture the exit code and show diagnostics.
    set +e

    if $VERBOSE; then
        # Full raw output to terminal + log
        "$@" 2>&1 | tee "$log_file"
        rc=${PIPESTATUS[0]}
    else
        # Stream output with real-time colorisation of key lines.
        # Everything is shown (not filtered), but errors/warnings/progress
        # get highlighted so they stand out.
        "$@" 2>&1 | tee "$log_file" | while IFS= read -r line; do
            if [[ "$line" == *rror:* || "$line" == *RROR:* || "$line" == *"CMake Error"* ]]; then
                echo -e "    ${C_RED}${line}${C_RESET}"
            elif [[ "$line" == *arning:* || "$line" == *ARNING:* || "$line" == *"CMake Warning"* ]]; then
                echo -e "    ${C_YELLOW}${line}${C_RESET}"
            elif [[ "$line" =~ ^\[\ *[0-9]+%\] ]]; then
                echo -e "    ${C_GREEN}${line}${C_RESET}"
            elif [[ "$line" == --\ * ]]; then
                echo -e "    ${C_CYAN}${line}${C_RESET}"
            elif [[ "$line" == Linking* || "$line" == Scanning* || "$line" == Building* ]]; then
                echo -e "    ${C_BLUE}${line}${C_RESET}"
            else
                echo "    ${line}"
            fi
        done
        rc=${PIPESTATUS[0]}
    fi

    # Re-enable errexit
    set -e

    elapsed=$(( SECONDS - t_start ))

    if [[ $rc -eq 0 ]]; then
        ok "${label} ${C_DIM}(${elapsed}s)${C_RESET}"
        rm -f "$log_file"
    else
        echo
        fail "Command failed (${label}) with exit code ${C_RED}${rc}${C_RESET}"
        echo -e "\n${C_RED}── last output ─────────────────${C_RESET}"
        tail -40 "$log_file" | sed 's/^/  /'
        echo -e "${C_RED}────────────────────────────────${C_RESET}\n"
        rm -f "$log_file"
        exit 1
    fi
}

###############################################################################
# Build-system detection
###############################################################################
detect_build_system() {
    local source_dir="$1"

    if [[ -f "${source_dir}/CMakeLists.txt" ]]; then
        echo "cmake"
    elif [[ -f "${source_dir}/configure" ]]; then
        echo "configure"
    elif [[ -f "${source_dir}/Makefile" || -f "${source_dir}/makefile" ]]; then
        echo "makefile"
    else
        echo "unknown"
    fi
}

###############################################################################
# Build strategies
###############################################################################

build_cmake() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}CMake${C_RESET}"
    mkdir -p "$build_dir"

    run_cmd "cmake configure" \
        cmake \
        -S "$source_dir" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DCMAKE_INSTALL_BINDIR="${BUNDLE_BIN}" \
        -DCMAKE_PREFIX_PATH="${BUNDLE_DIR}"

    run_cmd "cmake build" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install" \
        cmake --install "$build_dir"
}

build_autotools() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}configure (autotools)${C_RESET}"
    mkdir -p "$build_dir"

    run_cmd "configure" \
        "${source_dir}/configure" \
        --prefix="${BUNDLE_DIR}" \
        --libdir="${BUNDLE_LIB}" \
        --includedir="${BUNDLE_INCLUDE}" \
        --bindir="${BUNDLE_BIN}"

    run_cmd "make" \
        make -C "$build_dir" -j"${JOBS}"

    run_cmd "make install" \
        make -C "$build_dir" install
}

build_plain_make() {
    local source_dir="$1"

    info "Build system: ${C_GREEN}Makefile${C_RESET}"

    run_cmd "make" \
        make -C "$source_dir" -j"${JOBS}" PREFIX="${BUNDLE_DIR}"

    run_cmd "make install" \
        make -C "$source_dir" install PREFIX="${BUNDLE_DIR}"
}

build_header_only() {
    local source_dir="$1" _slug="$2"

    info "Build system: ${C_YELLOW}header-only / copy${C_RESET}"

    if [[ -d "${source_dir}/include" ]]; then
        info "Copying headers from ${C_DIM}${source_dir}/include${C_RESET}"
        cp -a "${source_dir}/include/." "${BUNDLE_INCLUDE}/"
        ok "Headers installed."
    else
        warn "No include/ directory and no build system detected for ${C_BOLD}${_slug}${C_RESET} — skipping."
    fi
}

###############################################################################
# Per-dependency build overrides
#
# When a dependency needs special cmake flags or a non-standard build
# procedure, define a function named  build_override_<name>  where <name>
# is the dependency name with dashes replaced by underscores.
# Signature:  build_override_XXX <source_dir> <build_dir>
###############################################################################

# Check whether an override function exists for a given dep name
has_build_override() {
    local fn="build_override_${1//-/_}"
    declare -f "$fn" &>/dev/null
}

run_build_override() {
    local fn="build_override_${1//-/_}"
    shift
    "$fn" "$@"
}

# ── mysql-server ──────────────────────────────────────────────────────────
# Build only the client library, CLI tools, and authentication plugins.
# Skips the full server, router, unit tests, man pages, etc.
build_override_mysql_server() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}CMake${C_RESET} ${C_YELLOW}(mysql-server — client-only)${C_RESET}"
    mkdir -p "$build_dir"

    # MySQL Server 8.0 requires a very specific Boost version (e.g. 1.77.0)
    # that differs from the Boost used by Studio itself.  Let the MySQL
    # build download it automatically into a dedicated directory.
    local mysql_boost_dir="${BUNDLE_SOURCE}/boost_for_mysql"
    mkdir -p "$mysql_boost_dir"

    run_cmd "cmake configure (mysql-server)" \
        cmake \
        -S "$source_dir" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DCMAKE_INSTALL_BINDIR="${BUNDLE_BIN}" \
        -DWITHOUT_SERVER=ON \
        -DWITH_ROUTER=OFF \
        -DWITH_UNIT_TESTS=OFF \
        -DWITH_RAPID=OFF \
        -DWITH_MAN=OFF \
        -DWITH_INNODB_MEMCACHED=OFF \
        -DDOWNLOAD_BOOST=ON \
        -DWITH_BOOST="${mysql_boost_dir}" \
        -DWITH_SSL=system \
        -DWITH_ZLIB=bundled \
        -DWITH_LZ4=bundled \
        -DWITH_ZSTD=bundled \
        -DWITH_EDITLINE=bundled \
        -DWITH_PROTOBUF=bundled \
        -DFORCE_INSOURCE_BUILD=OFF

    run_cmd "cmake build (mysql-server)" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install (mysql-server)" \
        cmake --install "$build_dir"

    # Ensure auth plugins land in lib/mysql/ (some builds put them in lib/plugin/)
    if [[ -d "${BUNDLE_LIB}/plugin" ]]; then
        mkdir -p "${BUNDLE_LIB}/mysql"
        find "${BUNDLE_LIB}/plugin" \( -name '*.so' -o -name '*.dylib' \) -exec cp -a {} "${BUNDLE_LIB}/mysql/" \;
        ok "Auth plugins copied to ${C_DIM}${BUNDLE_LIB}/mysql/${C_RESET}"
    fi
}

# ── mysql-connector-cpp ───────────────────────────────────────────────────
# Needs to find the libmysqlclient that was just built above.
build_override_mysql_connector_cpp() {
    local source_dir="$1" build_dir="$2"
    local dependency_cmake="${source_dir}/cdk/cmake/dependency.cmake"
    local protobuf_cmake="${source_dir}/cdk/extra/protobuf/CMakeLists.txt"

    info "Build system: ${C_GREEN}CMake${C_RESET} ${C_YELLOW}(mysql-connector-cpp)${C_RESET}"
    mkdir -p "$build_dir"

    if [[ -f "${protobuf_cmake}" ]]; then
        info "Normalizing bundled protobuf CMake policy compatibility"
        sed -i.bak 's/^cmake_minimum_required(VERSION 3\.1)$/cmake_minimum_required(VERSION 3.5)/' "${protobuf_cmake}"
        rm -f "${protobuf_cmake}.bak"
    fi

    if [[ -f "${dependency_cmake}" ]]; then
        info "Forwarding CMake policy minimum into bundled dependency builds"
        perl -0pi.bak -e 's/(set\(EXT_FWD\s*\n\s*CMAKE_BUILD_TYPE\s*\n)/$1  CMAKE_POLICY_VERSION_MINIMUM\n/' "${dependency_cmake}"
        rm -f "${dependency_cmake}.bak"
    fi

    run_cmd "cmake configure (connector-cpp)" \
        cmake \
        -S "$source_dir" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DCMAKE_PREFIX_PATH="${BUNDLE_DIR}" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DMYSQL_DIR="${BUNDLE_DIR}" \
        -DWITH_JDBC=ON \
        -DWITH_SSL=system

    run_cmd "cmake build (connector-cpp)" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install (connector-cpp)" \
        cmake --install "$build_dir"
}

# ── boost ─────────────────────────────────────────────────────────────────
# Header-only for SqlStudio — skip the full CMake/b2 build entirely.
build_override_boost() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_YELLOW}header-only / copy${C_RESET} ${C_YELLOW}(boost)${C_RESET}"

    # Boost headers live under libs/*/include or a top-level boost/ dir
    if [[ -d "${source_dir}/boost" ]]; then
        info "Copying boost/ headers"
        mkdir -p "${BUNDLE_INCLUDE}/boost"
        cp -a "${source_dir}/boost/." "${BUNDLE_INCLUDE}/boost/"
    elif [[ -d "${source_dir}/libs" ]]; then
        info "Copying headers from Boost modular layout"
        find "${source_dir}/libs" -path '*/include/boost' -type d | while read -r d; do
            cp -a "$d/." "${BUNDLE_INCLUDE}/boost/"
        done
    else
        warn "Could not locate boost headers in ${source_dir}"
        return 0
    fi

    ok "Boost headers installed."
}

# ── rapidjson ─────────────────────────────────────────────────────────────
# Pure header-only — copy include/rapidjson into bundle.
build_override_rapidjson() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_YELLOW}header-only / copy${C_RESET} ${C_YELLOW}(rapidjson)${C_RESET}"

    if [[ -d "${source_dir}/include/rapidjson" ]]; then
        mkdir -p "${BUNDLE_INCLUDE}/rapidjson"
        cp -a "${source_dir}/include/rapidjson/." "${BUNDLE_INCLUDE}/rapidjson/"
        # 1.1.0, the last release, has a copy assignment operator that assigns
        # to const members. GCC 14 and Clang 19 reject it even when unused.
        # Apply the upstream fix (commits 3b2441b and 862c39b): declare it only.
        perl -pi -e 's{GenericStringRef& operator=\(const GenericStringRef& rhs\) \{ s = rhs\.s; length = rhs\.length; \}}{GenericStringRef& operator=(const GenericStringRef& rhs) /* = delete */;}' \
            "${BUNDLE_INCLUDE}/rapidjson/document.h"
        ok "Rapidjson headers installed."
    else
        warn "Could not locate rapidjson headers in ${source_dir}"
    fi
}

# ── vsqlitepp ─────────────────────────────────────────────────────────────
# vsqlite++ uses autotools but ships only configure.ac — autogen.sh must be
# run first.  Build depends on sqlite3 already being in the bundle.
build_override_vsqlitepp() {
    local source_dir="$1" build_dir="$2"
    local version_backup=""

    info "Build system: ${C_GREEN}autotools${C_RESET} ${C_YELLOW}(vsqlitepp)${C_RESET}"

    pushd "$source_dir" > /dev/null

    # On default macOS case-insensitive filesystems, vsqlitepp's VERSION file
    # can shadow libc++ <version> during compilation.
    if [[ -f VERSION ]]; then
        version_backup="VERSION.vsqlitepp.bak"
        mv VERSION "${version_backup}"
    fi

    # Generate configure if it doesn't exist
    if [[ ! -f configure ]]; then
        if ! command -v autoreconf >/dev/null 2>&1; then
            fail "vsqlitepp requires autoreconf but it is not installed."
            info "Install autotools on macOS: ${C_WHITE}brew install autoconf automake libtool${C_RESET}"
            exit 1
        fi
        run_cmd "autogen (vsqlitepp)" \
            ./autogen.sh
    fi

    # Point to our bundled sqlite3
    export PKG_CONFIG_PATH="${BUNDLE_LIB}/pkgconfig:${PKG_CONFIG_PATH:-}"

    run_cmd "configure (vsqlitepp)" \
        ./configure \
        --prefix="${BUNDLE_DIR}" \
        --libdir="${BUNDLE_LIB}" \
        --includedir="${BUNDLE_INCLUDE}" \
        --bindir="${BUNDLE_BIN}" \
        --enable-shared \
        --disable-static \
        CXXFLAGS="-g -O2 -I${BUNDLE_INCLUDE}" \
        LDFLAGS="-L${BUNDLE_LIB}"

    # Upstream vsqlitepp injects GNU ld-only link flags in Makefile.am.
    # Strip them for Darwin/ld64.
    if [[ -f Makefile ]]; then
        sed -i.bak \
            -e 's/-Wl,--as-needed//g' \
            -e 's/-Wl,-soname[[:space:]]*-Wl,[^[:space:]]*//g' \
            Makefile
        rm -f Makefile.bak
    fi

    run_cmd "make (vsqlitepp)" \
        make -j"${JOBS}"

    run_cmd "make install (vsqlitepp)" \
        make install

    if [[ -n "${version_backup}" && -f "${version_backup}" ]]; then
        mv "${version_backup}" VERSION
    fi

    popd > /dev/null
}

# ── gdal ──────────────────────────────────────────────────────────────────
# Minimal GDAL build — only the core library, ogr2ogr, and ogrinfo.
build_override_gdal() {
    local source_dir="$1" build_dir="$2"
    local cmake_prefix_path="${BUNDLE_DIR}"
    local proj_prefix=""

    info "Build system: ${C_GREEN}CMake${C_RESET} ${C_YELLOW}(gdal — minimal)${C_RESET}"

    if command -v brew >/dev/null 2>&1; then
        proj_prefix="$(brew --prefix proj 2>/dev/null || true)"
        if [[ -n "${proj_prefix}" && -d "${proj_prefix}" ]]; then
            cmake_prefix_path="${BUNDLE_DIR};${proj_prefix}"
            info "Using Homebrew PROJ from ${C_DIM}${proj_prefix}${C_RESET}"
        fi
    fi

    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    run_cmd "cmake configure (gdal)" \
        cmake \
        -S "$source_dir" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DCMAKE_INSTALL_BINDIR="${BUNDLE_BIN}" \
        -DCMAKE_PREFIX_PATH="${cmake_prefix_path}" \
        -DGDAL_FIND_PACKAGE_PROJ_MODE=CONFIG \
        -DBUILD_APPS=ON \
        -DBUILD_TESTING=OFF \
        -DGDAL_USE_PROJ=OFF \
        -DGDAL_BUILD_OPTIONAL_DRIVERS=OFF \
        -DOGR_BUILD_OPTIONAL_DRIVERS=OFF \
        -DGDAL_USE_ODBC=OFF \
        -DBUILD_PYTHON_BINDINGS=OFF

    run_cmd "cmake build (gdal)" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install (gdal)" \
        cmake --install "$build_dir"
}

# ── sqlite ────────────────────────────────────────────────────────────────
# Autotools-based — the autoconf tarball provides ./configure.
build_override_sqlite() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}configure${C_RESET} ${C_YELLOW}(sqlite)${C_RESET}"
    mkdir -p "$build_dir"

    # SQLite's configure generates Makefile in CWD, so run from build_dir
    pushd "$build_dir" > /dev/null

    run_cmd "configure (sqlite)" \
        "${source_dir}/configure" \
        --prefix="${BUNDLE_DIR}" \
        --libdir="${BUNDLE_LIB}" \
        --includedir="${BUNDLE_INCLUDE}" \
        --bindir="${BUNDLE_BIN}" \
        --enable-shared \
        --disable-static

    run_cmd "make (sqlite)" \
        make -j"${JOBS}"

    run_cmd "make install (sqlite)" \
        make install

    popd > /dev/null
}

# ── libiodbc ──────────────────────────────────────────────────────────────
# iODBC uses autotools; build with GTK admin tool.
build_override_libiodbc() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}configure${C_RESET} ${C_YELLOW}(libiodbc)${C_RESET}"

    # iODBC doesn't support out-of-source builds — build in source dir
    pushd "$source_dir" > /dev/null

    # iODBC uses K&R-style function pointers with () meaning "unspecified params".
    # GCC 15 defaults to C23 where () means "zero params". Force gnu11 to fix.
    run_cmd "configure (libiodbc)" \
        ./configure \
        --prefix="${BUNDLE_DIR}" \
        --libdir="${BUNDLE_LIB}" \
        --includedir="${BUNDLE_INCLUDE}" \
        --bindir="${BUNDLE_BIN}" \
        --with-iodbc-inidir="${BUNDLE_DIR}/etc" \
        --disable-gui \
        CFLAGS="-g -O2 -std=gnu11"

    run_cmd "make (libiodbc)" \
        make -j"${JOBS}"

    run_cmd "make install (libiodbc)" \
        make install

    popd > /dev/null
}

# ── libssh ────────────────────────────────────────────────────────────────
# CMake with specific options.
build_override_libssh() {
    local source_dir="$1" build_dir="$2"

    info "Build system: ${C_GREEN}CMake${C_RESET} ${C_YELLOW}(libssh)${C_RESET}"
    mkdir -p "$build_dir"

    run_cmd "cmake configure (libssh)" \
        cmake \
        -S "$source_dir" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DCMAKE_INSTALL_BINDIR="${BUNDLE_BIN}" \
        -DWITH_EXAMPLES=OFF \
        -DWITH_SERVER=ON \
        -DUNIT_TESTING=OFF

    run_cmd "cmake build (libssh)" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install (libssh)" \
        cmake --install "$build_dir"
}

# ── antlr4-runtime ────────────────────────────────────────────────────────
# The source zip contains the runtime directly (no top-level CMakeLists.txt
# in the usual place); point CMake at the runtime sub-directory if needed.
build_override_antlr4_runtime() {
    local source_dir="$1" build_dir="$2"

    local antlr_cmake="${source_dir}/CMakeLists.txt"
    if [[ -f "${antlr_cmake}" ]]; then
        info "Normalizing ANTLR4 license install paths"
        perl -0pi.bak -e 's/if\(EXISTS LICENSE\.txt\)\ninstall\(FILES LICENSE\.txt\n        DESTINATION "share\/doc\/libantlr4"\)\nelseif\(EXISTS \.\.\/\.\.\/LICENSE\.txt\)\ninstall\(FILES \.\.\/\.\.\/LICENSE\.txt\n    DESTINATION "share\/doc\/libantlr4"\)\nendif\(\)/if(EXISTS "\${CMAKE_CURRENT_SOURCE_DIR}\/LICENSE.txt")\ninstall(FILES "\${CMAKE_CURRENT_SOURCE_DIR}\/LICENSE.txt"\n        DESTINATION "share\/doc\/libantlr4")\nendif()/g' "${antlr_cmake}"
        rm -f "${antlr_cmake}.bak"
    fi

    # Prefer runtime/Cpp when present. Some archives ship a top-level
    # CMakeLists that installs docs using paths relative to runtime/Cpp.
    local cmake_root="$source_dir"
    if [[ -f "${source_dir}/runtime/Cpp/CMakeLists.txt" ]]; then
        cmake_root="${source_dir}/runtime/Cpp"
    fi

    info "Build system: ${C_GREEN}CMake${C_RESET} ${C_YELLOW}(antlr4-runtime)${C_RESET}"
    rm -rf "$build_dir"
    mkdir -p "$build_dir"

    run_cmd "cmake configure (antlr4-runtime)" \
        cmake \
        -S "$cmake_root" \
        -B "$build_dir" \
        -G "Unix Makefiles" \
        -DCMAKE_INSTALL_PREFIX="${BUNDLE_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_INSTALL_LIBDIR="${BUNDLE_LIB}" \
        -DCMAKE_INSTALL_INCLUDEDIR="${BUNDLE_INCLUDE}" \
        -DANTLR4_INSTALL=ON \
        -DWITH_DEMO=OFF

    run_cmd "cmake build (antlr4-runtime)" \
        cmake --build "$build_dir" -- -j"${JOBS}"

    run_cmd "cmake install (antlr4-runtime)" \
        cmake --install "$build_dir"
}

###############################################################################
# Stamp helpers (track what has been built)
###############################################################################
stamp_file()  { echo "${BUNDLE_BUILD}/$1/.build_3rdparty.done"; }
is_built()    { [[ -f "$(stamp_file "$1")" ]]; }
mark_built()  { echo "built at $(date '+%Y-%m-%d %H:%M:%S')" > "$(stamp_file "$1")"; }

###############################################################################
# Process a single dependency
###############################################################################
process_dep() {
    local idx=$1
    local _slug; _slug="$(slug "$idx")"

    # ── download ──────────────────────────────────────────────────────
    if ! $BUILD_ONLY; then
        download_archive "$idx"
    else
        local archive="${BUNDLE_SOURCE}/$(archive_name "$idx")"
        if [[ ! -f "$archive" ]]; then
            fail "--build-only requested but archive missing: ${archive}"
            exit 1
        fi
    fi

    $DOWNLOAD_ONLY && return 0

    # JAR tools are installed directly; they do not need extraction or compilation.
    local artifact="${BUNDLE_SOURCE}/$(archive_name "$idx")"
    if [[ "$artifact" == *.jar ]]; then
        mkdir -p "${BUNDLE_BIN}"
        cp -f "$artifact" "${BUNDLE_BIN}/$(basename "$artifact")"
        ok "Installed JAR → ${C_DIM}${BUNDLE_BIN}/$(basename "$artifact")${C_RESET}"
        return 0
    fi

    # ── extract ───────────────────────────────────────────────────────
    extract_archive "$idx"

    local source_path="${BUNDLE_SOURCE}/${_slug}"
    local build_path="${BUNDLE_BUILD}/${_slug}"

    # ── clean (optional) ──────────────────────────────────────────────
    if $DO_CLEAN && [[ -d "$build_path" ]]; then
        warn "Cleaning previous build: ${C_DIM}${build_path}${C_RESET}"
        rm -rf "$build_path"
    fi

    # ── skip if already built ─────────────────────────────────────────
    if is_built "$_slug" && ! $DO_CLEAN; then
        ok "Already built — skipping ${C_DIM}(use --clean to rebuild)${C_RESET}"
        return 0
    fi

    # ── detect & build ────────────────────────────────────────────────
    local dep_name="${DEP_NAMES[$idx]}"

    if has_build_override "$dep_name"; then
        info "Using custom build override for ${C_BOLD}${dep_name}${C_RESET}"
        run_build_override "$dep_name" "$source_path" "$build_path"
    else
        local build_sys
        build_sys="$(detect_build_system "$source_path")"

        case "$build_sys" in
            cmake)     build_cmake      "$source_path" "$build_path" ;;
            configure) build_autotools   "$source_path" "$build_path" ;;
            makefile)  build_plain_make  "$source_path" ;;
            unknown)   build_header_only "$source_path" "$_slug" ;;
            *)
                fail "Cannot build ${_slug}: no recognised build system"
                exit 1
                ;;
        esac
    fi

    mkdir -p "$build_path"
    mark_built "$_slug"
    ok "${C_GREEN}${_slug} installed into bundle${C_RESET}"
}

###############################################################################
# Check host tools
###############################################################################
check_host_tools() {
    local missing_tools=()
    for tool in cmake make clang clang++ pkg-config; do
        local path
        path="$(command -v "$tool" 2>/dev/null || true)"
        if [[ -n "$path" ]]; then
            ok "$(printf '%-14s' "$tool") ${C_DIM}${path}${C_RESET}"
        else
            fail "$(printf '%-14s' "$tool") ${C_RED}NOT FOUND${C_RESET}"
            missing_tools+=("$tool")
        fi
    done

    if [[ ${#missing_tools[@]} -gt 0 ]]; then
        fail "Missing tools: ${missing_tools[*]}"
        info "Install command line tools: ${C_WHITE}xcode-select --install${C_RESET}"
        exit 1
    fi

    if command -v brew >/dev/null 2>&1; then
        ok "$(printf '%-14s' "homebrew") ${C_DIM}$(command -v brew)${C_RESET}"

        # autotools are required by vsqlitepp/autogen.sh
        if command -v autoreconf >/dev/null 2>&1; then
            ok "$(printf '%-14s' "autoreconf") ${C_DIM}$(command -v autoreconf)${C_RESET}"
        else
            warn "autoreconf not found. vsqlitepp build will fail without autotools."
            info "Install with: ${C_WHITE}brew install autoconf automake libtool${C_RESET}"
        fi
    else
        warn "Homebrew not found. Some dependencies may require it on macOS."
    fi
}

###############################################################################
# Main
###############################################################################
main() {
    banner

    # ── configuration summary ─────────────────────────────────────────
    section "Configuration"
    info "Bundle directory : ${C_WHITE}${BUNDLE_DIR}${C_RESET}"
    info "Dependency list  : ${C_WHITE}${LIST_FILE}${C_RESET}"
    info "Platform         : ${C_WHITE}$(uname -s) $(uname -m)${C_RESET}"
    info "Parallel jobs    : ${C_WHITE}${JOBS}${C_RESET}"

    $DO_CLEAN      && warn "--clean: build trees will be removed before rebuilding"
    $DOWNLOAD_ONLY && info "Mode             : ${C_YELLOW}download only${C_RESET}"
    $BUILD_ONLY    && info "Mode             : ${C_YELLOW}build only (no downloads)${C_RESET}"

    # ── validate list file ────────────────────────────────────────────
    if [[ ! -f "$LIST_FILE" ]]; then
        fail "Dependency list not found: ${LIST_FILE}"
        fail "Create one or pass --list <path>."
        exit 1
    fi

    parse_list "$LIST_FILE"
    local total=${#DEP_NAMES[@]}

    if [[ $total -eq 0 ]]; then
        warn "Dependency list is empty — nothing to do."
        return 0
    fi

    section "Dependencies (${total})"
    for (( i = 0; i < total; i++ )); do
        detail "$(printf "${C_BOLD}%-35s${C_RESET}  %s" "$(slug "$i")" "${DEP_URLS[$i]}")"
    done

    # ── create bundle layout ──────────────────────────────────────────
    section "Bundle layout"
    ensure_bundle
    if validate_bundle; then
        ok "Bundle directory ready: ${C_DIM}${BUNDLE_DIR}${C_RESET}"
    else
        fail "Failed to create bundle structure at ${BUNDLE_DIR}"
        exit 1
    fi
    for sub in bin build include lib source; do
        detail "${sub}/"
    done

    # ── host tools ────────────────────────────────────────────────────
    section "Host tools"
    check_host_tools

    # ── process each dependency ───────────────────────────────────────
    section "Processing dependencies"
    local t_start=$SECONDS

    for (( i = 0; i < total; i++ )); do
        step_header $(( i + 1 )) "$total" "$(slug "$i")"
        process_dep "$i"
    done

    local elapsed=$(( SECONDS - t_start ))

    # ── summary ───────────────────────────────────────────────────────
    echo
    echo -e "${C_GREEN}${C_BOLD}╔══════════════════════════════════════════════════════════╗${C_RESET}"
    echo -e "${C_GREEN}${C_BOLD}║           All dependencies processed successfully!      ║${C_RESET}"
    echo -e "${C_GREEN}${C_BOLD}╚══════════════════════════════════════════════════════════╝${C_RESET}"
    echo
    detail "Bundle root : ${BUNDLE_DIR}"
    detail "Headers     : ${BUNDLE_INCLUDE}"
    detail "Libraries   : ${BUNDLE_LIB}"
    detail "Binaries    : ${BUNDLE_BIN}"
    detail "Total time  : $(printf '%dm %ds' $((elapsed/60)) $((elapsed%60)))"
    echo
    info "Export for compile.sh:  ${C_WHITE}export WB_BUNDLE_DIR=${BUNDLE_DIR}${C_RESET}"
    echo
}

main
