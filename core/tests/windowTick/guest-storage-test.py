#!/usr/bin/env python3
"""Real Linux guest storage lifetime under normal and persistent OOM retirement."""
from pathlib import Path
import json,shlex,subprocess,os
root=Path(__file__).resolve().parents[3]; scratch=Path(os.environ.get("TC_GUEST_STORAGE_OUTPUT", root/".build/c1f1/guest-probe"));scratch.mkdir(parents=True,exist_ok=True)
build=root/'core/tests/allCoreTests/build-linux'
entries=json.loads((build/'compile_commands.json').read_text(encoding='utf-8'))
entry=next(x for x in entries if x['file'].endswith('tcWindowRuntime.cpp'))
args=shlex.split(entry['command']); compiler=args.pop(0); flags=[];skip=False
for x in args:
 if skip:skip=False;continue
 if x=='-o':skip=True;continue
 if x=='-c' or x.endswith('tcWindowRuntime.cpp') or x.startswith(('-O','-g')):continue
 flags.append(x)
link=shlex.split((build/'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
libs=link[next(i for i,x in enumerate(link) if x.endswith('libTrussC.a')):]
subprocess.run([compiler,*flags,'-O0','-g0','-fPIC','-fno-gnu-unique','-Wl,-Bsymbolic','-shared',str(root/'core/tests/windowTick/guest-storage-guest.cpp'),'-o',str(scratch/'guest.so')],check=True)
subprocess.run([compiler,*flags,'-O0','-g0','-rdynamic',str(root/'core/tests/windowTick/guest-storage.cpp'),*libs,'-o',str(scratch/'host')],cwd=build,check=True)
subprocess.run([str(scratch/'host'),str(scratch/'guest.so')],check=True)
