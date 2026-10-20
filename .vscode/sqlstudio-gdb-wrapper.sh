#!/bin/bash
set -eu

workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

# GDB loads sql-studio-bin for symbols and supplies it as the first argument.
# The installed shell launcher sets up its environment and execs that binary.
shift

# Keep setup in this shell so the final exec is the native executable.
unset GTK_PATH GTK_EXE_PREFIX GTK_MODULES GTK_IM_MODULE_FILE
unset GDK_PIXBUF_MODULE_FILE GDK_PIXBUF_MODULEDIR GIO_MODULE_DIR
export LD_PRELOAD="$workspace_dir/build/output/library/grt/src/libgrt.so:$workspace_dir/build/output/library/mysql.canvas/src/libmdcanvasgtk.so"
export WB_DEBUG=exec

# The installed launcher expects unset environment variables to expand to empty.
set +u
source "$workspace_dir/../sqlstudio-install/bin/sql-studio" "$@"
