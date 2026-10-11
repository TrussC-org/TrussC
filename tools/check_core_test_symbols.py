#!/usr/bin/env python3
"""Check Linux allCoreTests objects for shared external definitions (#546).

Uses CMake's compilation database so helper TUs are included and stale objects
are not. Weak/unique definitions (W/V/u) count too: an inline test helper with
external linkage can silently collide. Library namespaces are exempt for weak
definitions; strong duplicates are always reported.
"""

import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys


EXCLUDED_WEAK_NAMESPACES = [
    "std",        # C++ standard library templates and inline definitions.
    "__gnu_cxx",  # GNU C++ library implementation details.
    "__cxxabiv1", # C++ ABI runtime types and support machinery.
    "trussc",     # TrussC library headers.
    "tc",         # TrussC short namespace spelling.
    "tcx",        # TrussC extension libraries.
    "nlohmann",   # Header-only JSON library.
    "httplib",    # Header-only HTTP library.
]

DEMANGLED_PREFIXES = (
    "typeinfo for ", "typeinfo name for ", "vtable for ", "VTT for ",
    "guard variable for ", "non-virtual thunk to ", "virtual thunk to ",
    "covariant return thunk to ", "construction vtable for ",
)
STRONG_KINDS = frozenset("TDBR")
WEAK_KINDS = frozenset("WVu")


def combined_objects(build_dir, tests_dir):
    objects = {}
    entries = json.loads((build_dir / "compile_commands.json").read_text())
    for entry in entries:
        directory = Path(entry["directory"])
        source = (directory / entry["file"]).resolve()
        try:
            relative = source.relative_to(tests_dir)
        except ValueError:
            continue
        if len(relative.parts) < 3 or relative.parts[1] != "src":
            continue
        name = relative.parts[0]
        test_dir = tests_dir / name
        if (name in {"common", "allCoreTests"}
                or not (test_dir / "src" / "main.cpp").is_file()
                or (test_dir / "own-binary").exists()):
            continue
        output = entry.get("output")
        if not output:
            command = entry.get("arguments") or shlex.split(entry["command"])
            try:
                output = command[command.index("-o") + 1]
            except (ValueError, IndexError) as error:
                raise ValueError(f"No object output for {source}") from error
        objects[(directory / output).resolve()] = name
    if not objects:
        raise ValueError("No combined test objects found in compile_commands.json")
    return objects


def defined_symbols(listing):
    symbols = {}
    for line in listing.splitlines():
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 3 or len(fields[1]) != 1:
            raise ValueError(f"Unexpected nm output: {line}")
        kind, symbol = fields[1:]
        if kind in STRONG_KINDS | WEAK_KINDS:
            symbols.setdefault(symbol, set()).add(kind)
    return symbols


def demangle_symbols(symbols):
    """Use one c++filt process for the entire run, retaining raw identities."""
    names = sorted(symbols)
    if not names:
        return {}
    result = subprocess.run(["c++filt"], input="\n".join(names) + "\n",
                            capture_output=True, text=True, check=True)
    demangled = result.stdout.splitlines()
    if len(demangled) != len(names) or any(not name for name in demangled):
        raise ValueError("Unexpected c++filt output")
    return dict(zip(names, demangled))


def outermost_scope(name):
    """Find the entity's scope, ignoring return types and nested type names.

    At depth zero, whitespace separates return-type tokens from the entity.
    Template arguments, ABI tags and decltype expressions cannot supply a
    namespace. Stop at function arguments or an operator name (whose spelling
    may itself contain spaces, parentheses or angle brackets).
    """
    while True:
        prefix = next((p for p in DEMANGLED_PREFIXES if name.startswith(p)), None)
        if prefix is None:
            break
        name = name[len(prefix):]
    token = ""
    brackets = []
    closing = {"<": ">", "[": "]", "{": "}", "(": ")"}
    for i, char in enumerate(name):
        if brackets:
            if char == brackets[-1]:
                brackets.pop()
            elif char in closing:
                brackets.append(closing[char])
            continue
        if name.startswith("operator", i) and (not token or token.endswith("::")):
            token += "operator"
            break
        if char == "(" and not name[:i].rstrip().endswith("decltype"):
            # Anonymous scopes cannot match the library namespace list.
            break
        if char in closing:
            brackets.append(closing[char])
        elif char.isspace():
            token = ""
        else:
            token += char
    token = token.lstrip("*&")
    return token.split("::", 1)[0] if "::" in token else None


def is_function_type_rtti(name):
    """Recognize function types without matching functions in template arguments."""
    prefix = next((p for p in ("typeinfo for ", "typeinfo name for ")
                   if name.startswith(p)), None)
    if prefix is None:
        return False
    brackets = []
    closing = {"<": ">", "[": "]", "{": "}", "(": ")"}
    for char in name[len(prefix):]:
        if char == "(" and not brackets:
            return True
        if brackets and char == brackets[-1]:
            brackets.pop()
        elif char in closing:
            brackets.append(closing[char])
    return False


def find_duplicates(listings):
    """Return raw duplicate identities/owners and their demangled names."""
    owners = {}
    kinds = {}
    for obj, (test, listing) in listings.items():
        for symbol, definitions in defined_symbols(listing).items():
            owners.setdefault(symbol, []).append((test, obj))
            kinds.setdefault(symbol, set()).update(definitions)
    candidates = {symbol: sorted(locations) for symbol, locations in sorted(owners.items())
                  if len(locations) > 1}
    demangled = demangle_symbols(candidates)
    duplicates = {}
    for symbol, locations in candidates.items():
        if kinds[symbol] & STRONG_KINDS:
            duplicates[symbol] = locations
            continue
        scope = outermost_scope(demangled[symbol])
        # Shared test helpers/registration and compiler exception references are not
        # test helpers. Match the entity, not mentions in arguments/types.
        if symbol.startswith("DW.ref.") or scope == "tcCoreTest":
            continue
        # Function/function-pointer RTTI: compiler-generated; identical for the
        # same type; not a test helper.
        if is_function_type_rtti(demangled[symbol]):
            continue
        if scope not in EXCLUDED_WEAK_NAMESPACES:
            duplicates[symbol] = locations
    return duplicates, demangled


def check_objects(objects):
    listings = {}
    for obj, test in sorted(objects.items()):
        result = subprocess.run(["nm", "-g", "--defined-only", str(obj)],
                                capture_output=True, text=True, check=True)
        listings[obj] = (test, result.stdout)
    duplicates, demangled = find_duplicates(listings)
    if duplicates:
        print(f"Duplicate core test symbols ({len(duplicates)}):", file=sys.stderr)
        for symbol, locations in duplicates.items():
            print(f"  {demangled[symbol]}", file=sys.stderr)
            for test, obj in locations:
                print(f"    {test}: {obj}", file=sys.stderr)
        print("Fix: put test helpers in an anonymous namespace", file=sys.stderr)
        print("Rule: helpers shared through core/tests/common/ headers go in "
              "namespace tcCoreTest", file=sys.stderr)
        print("If this is a library namespace, add one line to the list in "
              "tools/check_core_test_symbols.py", file=sys.stderr)
        return 1
    print(f"Core test symbols: checked {len(objects)} objects; no duplicates.")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_dir", type=Path, help="allCoreTests CMake build directory")
    args = parser.parse_args(argv)
    tests_dir = Path(__file__).resolve().parents[1] / "core" / "tests"
    try:
        return check_objects(combined_objects(args.build_dir.resolve(), tests_dir))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print(f"Core test symbol check failed: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError) and error.stderr:
            print(error.stderr, file=sys.stderr, end="")
        return 1


if __name__ == "__main__":
    sys.exit(main())
