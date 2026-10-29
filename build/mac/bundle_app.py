#!/usr/bin/env python3
"""Make SqlStudio.app self-contained.

After xcodebuild, binaries in the app still load libraries from where they
were built or installed: Homebrew (/opt/homebrew), the 3rd-party bundle, the
Xcode build directory, or the system-wide /Library/Frameworks/Python.framework
whose layout the embedded Python.framework copies.

This script rewrites every non-system library reference of every Mach-O file
in the app so that it points at a file inside the app, using a path relative
to the referencing file (@loader_path). Libraries that are not in the app yet
are copied into Contents/Frameworks, together with their own dependencies.
Modified files are signed again ad hoc, because install_name_tool invalidates
their signatures. The script fails if any reference is left that points
outside the app or the operating system.

Usage: bundle_app.py SqlStudio.app [--search DIR]...
       bundle_app.py SqlStudio.app --check

--check only verifies an already bundled app (for example one installed from
the .dmg) and changes nothing.

--search adds a directory where libraries referenced only by name (for
example "libantlr4-runtime.4.13.2.dylib" or "@rpath/libssh.4.dylib") are
looked up when the app does not contain them yet, such as the 3rd-party
bundle's lib directory.
"""

import argparse
import os
import shutil
import subprocess
import sys

SYSTEM_PREFIXES = ("/usr/lib/", "/System/")
PYTHON_FRAMEWORK = "/Library/Frameworks/Python.framework/"
MACHO_MAGICS = {b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xfe\xed\xfa\xcf"}


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def is_macho(path):
    # Static archives (Python.framework ships a few .a stubs) are never loaded.
    if os.path.islink(path) or not os.path.isfile(path) or path.endswith(".a"):
        return False
    with open(path, "rb") as f:
        return f.read(4) in MACHO_MAGICS


def install_id(path):
    lines = run("otool", "-D", path).splitlines()
    return lines[1].strip() if len(lines) > 1 else None


def dependencies(path):
    """Library references of a Mach-O file, without its own install id."""
    own_id = install_id(path)
    deps = []
    for line in run("otool", "-L", path).splitlines()[1:]:
        line = line.strip()
        if not line or not line.endswith(")"):
            continue
        ref = line.rsplit(" (", 1)[0]
        if ref != own_id:
            deps.append(ref)
    return deps


class Bundler:
    def __init__(self, app, search_dirs):
        self.app = os.path.abspath(app)
        self.macos = os.path.join(self.app, "Contents", "MacOS")
        self.frameworks = os.path.join(self.app, "Contents", "Frameworks")
        self.python = os.path.join(self.frameworks, "Python.framework")
        self.search_dirs = search_dirs
        self.origin = {}  # copied library -> directory it was copied from
        self.by_name = {}
        self.modified = set()
        self.problems = []
        for root, _, files in os.walk(self.app):
            for name in files:
                path = os.path.join(root, name)
                # Libraries inside Python.framework are only used by Python.
                if not path.startswith(self.python + os.sep):
                    self.by_name.setdefault(name, path)

    def resolve_in_app(self, ref, referrer):
        """Path inside the app that a reference should point to, or None."""
        name = os.path.basename(ref)
        if ref.startswith(PYTHON_FRAMEWORK):
            target = os.path.join(self.python, ref[len(PYTHON_FRAMEWORK):])
            return target if os.path.exists(target) else None
        if ref.startswith("@executable_path/"):
            target = os.path.normpath(os.path.join(self.macos, ref[len("@executable_path/"):]))
            if os.path.exists(target):
                return target
        if ref.startswith("@loader_path/"):
            target = os.path.normpath(os.path.join(os.path.dirname(referrer), ref[len("@loader_path/"):]))
            if os.path.exists(target):
                return target
        return self.by_name.get(name)

    def find_outside(self, ref, referrer):
        """Existing file outside the app for a reference, or None."""
        if os.path.isabs(ref) and os.path.exists(ref):
            return os.path.realpath(ref)
        name = os.path.basename(ref)
        # A copied library's @rpath dependencies usually sit next to it
        # (e.g. Homebrew's libwebp and libsharpyuv).
        dirs = [self.origin[referrer]] if referrer in self.origin else []
        for directory in dirs + self.search_dirs:
            candidate = os.path.join(directory, name)
            if os.path.exists(candidate):
                return os.path.realpath(candidate)
        return None

    def copy_in(self, source, name):
        target = os.path.join(self.frameworks, name)
        shutil.copy2(source, target)
        os.chmod(target, 0o755)
        run("install_name_tool", "-id", "@rpath/" + name, target)
        self.by_name[name] = target
        self.origin[target] = os.path.dirname(source)
        self.modified.add(target)
        print(f"  copied {name} from {source}")
        return target

    def fix_file(self, path, queue):
        for ref in dependencies(path):
            if ref.startswith(SYSTEM_PREFIXES):
                continue
            target = self.resolve_in_app(ref, path)
            if target is None:
                source = self.find_outside(ref, path)
                if source is None:
                    self.problems.append(f"{os.path.relpath(path, self.app)}: cannot find {ref}")
                    continue
                target = self.copy_in(source, os.path.basename(ref))
                queue.append(target)
            new_ref = "@loader_path/" + os.path.relpath(target, os.path.dirname(path))
            if new_ref != ref:
                run("install_name_tool", "-change", ref, new_ref, path)
                self.modified.add(path)

    def macho_files(self):
        files = [os.path.join(root, name)
                 for root, _, names in os.walk(self.app) for name in names]
        return [p for p in files if is_macho(p)]

    def check(self):
        files = self.macho_files()
        print(f"Checking {len(files)} Mach-O files in {self.app}")
        return self.verify(files)

    def run(self):
        queue = self.macho_files()
        print(f"Checking {len(queue)} Mach-O files in {self.app}")
        done = set()
        while queue:
            path = queue.pop()
            if path in done:
                continue
            done.add(path)
            self.fix_file(path, queue)

        print(f"Signing {len(self.modified)} modified files ad hoc")
        for path in sorted(self.modified):
            run("codesign", "--force", "--sign", "-", path)
        run("codesign", "--force", "--deep", "--sign", "-", self.app)

        if self.problems:
            print("Unresolved library references:", file=sys.stderr)
            for problem in self.problems:
                print("  " + problem, file=sys.stderr)
            return 1
        return self.verify(done)

    def verify(self, files):
        leaks = []
        for path in sorted(files):
            for ref in dependencies(path):
                if ref.startswith(SYSTEM_PREFIXES):
                    continue
                if not ref.startswith("@loader_path/") or self.resolve_in_app(ref, path) is None:
                    leaks.append(f"{os.path.relpath(path, self.app)}: {ref}")
        if leaks:
            print("References that still point outside the app:", file=sys.stderr)
            for leak in leaks:
                print("  " + leak, file=sys.stderr)
            return 1
        print("All library references resolve inside the app.")
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("app")
    parser.add_argument("--search", action="append", default=[], metavar="DIR")
    parser.add_argument("--check", action="store_true", help="only verify, change nothing")
    args = parser.parse_args()
    bundler = Bundler(args.app, [os.path.abspath(d) for d in args.search])
    return bundler.check() if args.check else bundler.run()


if __name__ == "__main__":
    sys.exit(main())
