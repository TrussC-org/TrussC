#!/usr/bin/env python3
"""Reject tracked test binaries and ignored trusscli project files (CI).

Generated-file checks inspect git ls-files: generated files on disk are
normal build outputs. Test bin/data/ trees contain tracked assets by convention
and are exempt. Only files directly under bin/ are rejected. Core test projects must
also have addons.make on disk so trusscli update does not create it. Standalone
CMake tests with a committed CMakeLists.txt and no src/ stay valid.
"""
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = {"CMakeLists.txt", "CMakePresets.json"}


def violations(root, tracked):
    tracked = set(tracked)
    errors = []
    core_projects = set()
    for name in sorted(tracked):
        path = PurePosixPath(name)
        parts = path.parts
        core_test = len(parts) >= 4 and parts[:2] == ("core", "tests")
        addon_test = (len(parts) >= 4 and parts[0] == "addons"
                      and parts[2] == "tests")
        if core_test:
            core_projects.add(PurePosixPath(*parts[:3]))
        if not (core_test or addon_test):
            continue
        if len(parts) == 5 and parts[3] == "bin":
            errors.append(name)
        elif len(parts) == 4 and path.name in GENERATED:
            ignore = root / path.parent / ".gitignore"
            if ignore.is_file():
                entries = {line.strip().lstrip("/")
                           for line in ignore.read_text(encoding="utf-8").splitlines()}
                if path.name in entries:
                    errors.append(name)
    for project in sorted(core_projects):
        # Match examples/build_all.py: standalone tests have CMakeLists.txt
        # and no src/ directory; their .gitignore may only ignore build output.
        standalone = (str(project / "CMakeLists.txt") in tracked
                      and not (root / project / "src").is_dir())
        if (not standalone and (root / project / ".gitignore").is_file()
                and not (root / project / "addons.make").is_file()):
            errors.append(str(project / "addons.make"))
    return errors


def main(root=ROOT):
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=root).decode("utf-8").split("\0")
    errors = violations(root, filter(None, tracked))
    for name in errors:
        if PurePosixPath(name).name == "addons.make":
            print(f"missing core test project file: {name} (add an empty file)")
        else:
            print(f"tracked generated test file: {name} (untrack this path)")
    print(f"test generated-file check: {len(errors)} violation(s)")
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
