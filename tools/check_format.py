#!/usr/bin/env python3
"""Check (or fix) C/C++ formatting, honoring .clang-format-ignore."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inl"}
BATCH_SIZE = 50


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="Check working-tree files changed since the merge base with this ref")
    parser.add_argument("--all", action="store_true", help="Check all tracked files instead of changed files")
    parser.add_argument("--fix", "--fix-format", dest="fix", action="store_true",
                        help="Reformat the selected files in place")
    parser.add_argument("--clang-format", default="clang-format", help="Formatter executable")
    args = parser.parse_args()
    if args.all and args.base:
        parser.error("--all and --base cannot be combined")

    expected = (ROOT / "tools/format-requirements.txt").read_text().strip().split("==")[1]
    version = subprocess.check_output([args.clang_format, "--version"], text=True)
    match = re.search(r"clang-format version (\d+\.\d+\.\d+)\b", version)
    if not match or match.group(1) != expected:
        parser.error(f"clang-format {expected} is required; found {version.strip()}")

    # Validate configuration even when the change contains no C/C++ files.
    subprocess.run(
        [args.clang_format, "--style=file", "--assume-filename=check.cpp", "--dump-config"],
        cwd=ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, check=True,
    )
    if args.all:
        listed = git("ls-files", "-z")
    else:
        base = git("merge-base", args.base, "HEAD").decode().strip() if args.base else "HEAD"
        listed = git("diff", "--name-only", "-z", "--diff-filter=ACMRT", base, "--")
    untracked = git("ls-files", "--others", "--exclude-standard", "-z")
    paths = sorted({os.fsdecode(p) for p in (listed + untracked).split(b"\0") if p})
    candidates = []
    for path in paths:
        source = ROOT / path
        if source.suffix.lower() in EXTENSIONS and not source.is_symlink() and source.is_file():
            candidates.append("./" + path)

    mode = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    failed = False
    for start in range(0, len(candidates), BATCH_SIZE):
        # Pass filenames directly so clang-format applies its native ignore rules.
        result = subprocess.run(
            [args.clang_format, "--style=file", *mode, "--", *candidates[start:start + BATCH_SIZE]],
            cwd=ROOT,
        )
        failed |= result.returncode != 0
    action = "Formatted" if args.fix else "Checked"
    print(f"{action} {len(candidates)} candidate C/C++ file(s); .clang-format-ignore exclusions apply.")
    return int(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Formatting check failed: {error}", file=sys.stderr)
        sys.exit(1)
