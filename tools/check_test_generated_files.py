#!/usr/bin/env python3
"""Reject tracked test outputs and project tests missing a .gitignore (CI).

Generated-file checks inspect git ls-files; local build outputs are normal.
Project markers on disk require a .gitignore, including in untracked tests.
Core test projects also need a non-empty addons.make so trusscli update does
not create or rewrite it. Test bin/data/ assets are exempt. Standalone CMake
tests with a committed CMakeLists.txt and no src/ stay valid.
"""
from pathlib import Path, PurePosixPath
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = {"CMakeLists.txt", "CMakePresets.json"}
PROJECT_MARKERS = {"local.cmake", "addons.make"}


def is_test_path(parts):
    return (len(parts) >= 4 and
            (parts[:2] == ("core", "tests") or
             (parts[0] == "addons" and parts[2].startswith("tests"))))


def violations(root, tracked):
    tracked = set(tracked)
    errors = []
    projects = set()
    core_projects = set()
    for name in sorted(tracked):
        path = PurePosixPath(name)
        parts = path.parts
        if not is_test_path(parts):
            continue
        if parts[:2] == ("core", "tests"):
            core_projects.add(PurePosixPath(*parts[:3]))
        if len(parts) == 4 and path.name in PROJECT_MARKERS:
            projects.add(path.parent)
        if len(parts) == 5 and parts[3] == "bin":
            errors.append(name)
        elif len(parts) == 4 and path.name in GENERATED:
            ignore = root / path.parent / ".gitignore"
            if ignore.is_file():
                entries = {line.strip().lstrip("/")
                           for line in ignore.read_text(encoding="utf-8").splitlines()}
                if path.name in entries:
                    errors.append(name)
    for pattern in ("core/tests/*/*", "addons/*/tests*/*"):
        for marker in root.glob(pattern):
            if marker.name in PROJECT_MARKERS and marker.is_file():
                projects.add(PurePosixPath(marker.parent.relative_to(root).as_posix()))
    for project in sorted(projects):
        if not (root / project / ".gitignore").is_file():
            errors.append(str(project / ".gitignore"))
    for project in sorted(core_projects):
        # Match examples/build_all.py: standalone tests have CMakeLists.txt
        # and no src/ directory; their .gitignore may only ignore build output.
        standalone = (str(project / "CMakeLists.txt") in tracked
                      and not (root / project / "src").is_dir())
        addons = root / project / "addons.make"
        if (not standalone and (root / project / ".gitignore").is_file()
                and (not addons.is_file() or addons.stat().st_size == 0)):
            # trusscli update rewrites an empty addons.make with its header,
            # which would dirty the tree on every build.
            errors.append(str(project / "addons.make"))
    return errors


def main(root=ROOT):
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z"], cwd=root).decode("utf-8").split("\0")
    errors = violations(root, filter(None, tracked))
    for name in errors:
        if PurePosixPath(name).name == "addons.make":
            print(f"missing or empty core test project file: {name} (add \"# TrussC addons - one addon per line\")")
        elif PurePosixPath(name).name == ".gitignore":
            print(f"test project missing .gitignore: {name} (add project ignores)")
        else:
            print(f"tracked generated test file: {name} (untrack this path)")
    print(f"test generated-file check: {len(errors)} violation(s)")
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
