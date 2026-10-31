#!/usr/bin/env python3
"""Supervised production-linked C3 tests. Run with tcbuild.sh; never logs out OS."""
from pathlib import Path
import json, os, shlex, subprocess, time, sys
root=Path.cwd();out=root/ os.environ.get('TC_C3_EVIDENCE','.verification/c3');scratch=root/ os.environ.get('TC_C3_SCRATCH','.build/c3') /'session';scratch.mkdir(parents=True,exist_ok=True)
build=root/'core/tests/allCoreTests/build-linux'
entry=next(e for e in json.loads((build/'compile_commands.json').read_text(encoding='utf-8')) if e['file'].endswith('tcWindowRuntime.cpp'))
args=shlex.split(entry['command']);compiler=args.pop(0);flags=[];skip=False
for a in args:
 if skip:skip=False;continue
 if a=='-o':skip=True;continue
 if a=='-c' or a.endswith('tcWindowRuntime.cpp') or a.startswith(('-O','-g')):continue
 flags.append(a)
link=shlex.split((build/'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'));libs=link[next(i for i,a in enumerate(link) if a.endswith('libTrussC.a')):]
sanitized='--tsan' in sys.argv
objects=[]
if sanitized:
 flags += ['-fsanitize=thread','-fno-omit-frame-pointer','-fno-pie']
 for source in ['tcWindowRuntime.cpp','tcExit.cpp']:
  obj=scratch/(source+'.o')
  subprocess.run([compiler,*flags,'-O1','-g1','-c',str(root/'core/include/tc/app'/source),'-o',str(obj)],cwd=build,check=True)
  objects.append(str(obj))
 flags.append('-no-pie')
binary=scratch/('probe-tsan' if sanitized else 'probe')
subprocess.run([compiler,*flags,'-O0','-g0',str(root/'core/tests/sessionEndBusy/src/main.cpp'),*objects,*libs,'-o',str(binary)],cwd=build,check=True)
# A link-time check complements timings even on small/fast Linux binaries.
symbols=subprocess.check_output(['nm','-C',str(root/'core/tests/allCoreTests/bin/allCoreTests')],text=True,encoding='utf-8')
for symbol in ['retainGuestCodeFor','makeCodeShared','dladdr']:
 if symbol in symbols:raise SystemExit('FAIL: combined binary still contains '+symbol)
print('PASS: combined binary has no per-object guest retention or dladdr symbol',flush=True)
records=[]
def run(name,args,expected=None,deadline=8):
 start=time.monotonic()
 for attempt in range(100 if sanitized else 1):
  r=subprocess.run([str(binary),*args],capture_output=True,text=True,encoding='utf-8',timeout=deadline,env=dict(os.environ,TSAN_OPTIONS='halt_on_error=1'))
  log=r.stdout+r.stderr
  startup=('FATAL: ThreadSanitizer: unexpected memory mapping' in log or (r.returncode==-11 and not log)) and 'ENTERED MAIN' not in log
  if not (sanitized and startup):break
  (out/('tsan-startup-'+name+'-'+str(attempt)+'.log')).write_text(log,encoding='utf-8')
 elapsed=time.monotonic()-start;log=r.stdout+r.stderr;(out/(('session-tsan-' if sanitized else 'session-')+name+'.log')).write_text(log,encoding='utf-8')
 good=r.returncode==0 and ': FAIL' not in log
 if expected:good &= all(x in log for x in expected)
 records.append(dict(name=name,exit=r.returncode,seconds=elapsed,valid=good))
 (out/('session-tsan-results.json' if sanitized else 'session-results.json')).write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
 print(records[-1],flush=True)
 if not good:raise SystemExit(log)
run('deterministic',[])
run('watchdog-runs',['watchdog-runs'])
run('query',['query'])
run('worker-query',['worker-query'])
if not sanitized:
 for mode in ['busy','native-busy','constructor']:
  run(mode,[mode],['busy commit returns with no App exit or watchdog: PASS'])
if sanitized:raise SystemExit(0)
logfile = scratch/'idle-final.log'
logfile.unlink(missing_ok=True)
run('idle-log',['idle-log','0','inf',str(logfile)],['App final stdout exit line','exit: clean','App final exit line 1'])
text = logfile.read_text(encoding='utf-8')
assert 'exit: clean' in text and 'App final exit line 1' in text, 'idle commit lost final file log lines'
print('PASS: idle commit preserves final exit lines in file and captured stdout',flush=True)
run('idle-check',['idle-check','0','inf'],['idle common ending includes every cleanup: PASS'])
run('idle',['idle','0','inf'],['EXIT 3','RETURN 1'])
for mode in ['negative','nan']:
 run(mode,[mode,'5','0.05'],['setLogoutTimeout()','EXIT 3'])
 text=(out/('session-'+mode+'.log')).read_text(encoding='utf-8');assert 'RETURN 3' not in text
run('zero',['idle','5','0']);assert 'EXIT' not in (out/'session-zero.log').read_text(encoding='utf-8')
for delay in ['2','5','10']:
 run('default-'+delay,['idle',delay],['EXIT 3'])
 run('configured-'+delay,['idle',delay,'0.05'],['EXIT 3'])
run('shared-budget',['idle','0.07','0.1'],['RETURN 3','EXIT 2'])
assert 'RETURN 2' not in (out/'session-shared-budget.log').read_text(encoding='utf-8')
run('infinity',['idle','0.02','inf'],['RETURN 1'])
p=subprocess.Popen([str(binary),'idle','5','inf'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8')
try:
 text=p.communicate(timeout=0.5)[0]
 raise AssertionError('infinite watchdog unexpectedly terminated: '+text)
except subprocess.TimeoutExpired:
 p.kill();text=p.communicate(timeout=5)[0]
 assert 'EXIT 3' in text and 'RETURN 3' not in text
 (out/'session-infinity-external.log').write_text(text,encoding='utf-8')
