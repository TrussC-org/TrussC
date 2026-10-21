#!/usr/bin/env python3
"""Check every public tcxImGui include path for guest-only ImGui emissions.

Run through tcbuild.sh with GCC, nm and objdump. --include-dir overlays saved pre-fix
headers. A separate upstream-only control accounts for Dear ImGui's own unused
inline helpers emitted by -fkeep-inline-functions.
"""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile


def symbols(obj, defined=False):
    output = subprocess.check_output(
        ['nm', '-C', '--defined-only' if defined else '-u', str(obj)],
        text=True, encoding='utf-8', errors='replace')
    return {line.split(maxsplit=1)[1] if not defined else line
            for line in output.splitlines()
            if re.search(r'\b(?:Im[A-Z]\w*|GImGui|simgui_\w*)', line)}


def addon_calls(obj, undefined):
    # Upstream and addon bodies can call the same symbol (GetCurrentContext,
    # for example). Check relocation origins too, so the control cannot hide
    # an addon caller merely because upstream already refers to that symbol.
    table = subprocess.check_output(['objdump', '-tC', str(obj)], text=True,
                                    encoding='utf-8', errors='replace')
    sections = set()
    for line in table.splitlines():
        match = re.search(r'\sF\s+(\S+)\s+[0-9a-f]+\s+(.*)', line)
        if match and match[2].startswith(('tcx::imgui::', 'ImGuiTcHook_',
                                         'ImGuiTestEngine')):
            sections.add(match[1])
    relocations = subprocess.check_output(['objdump', '-rC', str(obj)],
                                          text=True, encoding='utf-8',
                                          errors='replace')
    section = None
    calls = set()
    for line in relocations.splitlines():
        heading = re.match(r'RELOCATION RECORDS FOR \[(.*)\]:', line)
        if heading:
            section = heading[1]
        elif section in sections:
            fields = line.split(maxsplit=2)
            if len(fields) != 3 or not re.fullmatch(r'[0-9a-f]+', fields[0]):
                continue
            target = re.sub(r'[+-]0x[0-9a-f]+$', '', fields[2])
            if target in undefined and target.startswith(('Im', 'GImGui', 'simgui_')):
                calls.add(section + ' -> ' + target)
    return calls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    src = Path(__file__).resolve().parents[1] / 'src'
    root = src.parents[2]
    parser.add_argument('--include-dir', type=Path, default=src)
    args = parser.parse_args()
    flags = ['-std=c++20', '-O0', '-fno-inline', '-DSOKOL_GLCORE',
             '-I' + str(args.include_dir.resolve()), '-I' + str(src),
             '-I' + str(root / 'core/include'),
             '-I' + str(root / 'core/include/sokol')]
    failed = False
    with tempfile.TemporaryDirectory() as work:
        def compile_probe(name, contents, forced):
            source = Path(work) / (name + '.cpp')
            obj = source.with_suffix('.o')
            source.write_text(contents, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'g++'), *flags,
                            *(['-fkeep-inline-functions'] if forced else []),
                            '-c', str(source), '-o', str(obj)], check=True)
            return obj

        control = compile_probe('control', '#include <TrussC.h>\n'
                                '#include "sokol_imgui.h"\n'
                                '#include "imgui/imgui_internal.h"\n', True)
        upstream = symbols(control)
        for header in ['tcImGuiHooks.h', 'tcImGuiTools.h', 'tcxImGui.h']:
            source = '#include "' + header + '"\n'
            # Each TU includes only the public entry point under test.
            for forced in [False, True]:
                obj = compile_probe(header + str(forced), source, forced)
                references = symbols(obj)
                extra = references - upstream if forced else references
                calls = addon_calls(obj, references)
                emitted = {line for line in symbols(obj, defined=True)
                           if 'ImGuiTcHook_' in line or 'ImGuiTestEngine' in line
                           or 'tcx::imgui::detail::captureValue(' in line}
                if extra or emitted or calls:
                    failed = True
                    print(f'FAIL: {header} ({"forced" if forced else "normal"} emission):')
                    print('\n'.join(sorted(extra | emitted | calls)))
                else:
                    print(f'PASS: {header} ({"forced" if forced else "normal"} emission)')
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
