#!/usr/bin/env python3
"""Use only for the local Mesa/Xvfb evidence, not native hardware routing."""
import pathlib, subprocess, sys
root=pathlib.Path(__file__).resolve().parents[3]
results=root/'tools/probes/snapshot-cost/results'
results.mkdir(exist_ok=True)
info=subprocess.check_output(['glxinfo','-B'],text=True)
(results/'local-glxinfo.txt').write_text(info)
print(info,flush=True)
for backend,windows,samples in [('gl',1,1),('gl',2,1),('gl',1,4),('gles',1,1),('gles',1,4)]:
    label=f'linux-{backend}-1080-{windows}w-s{samples}'
    print('RUN '+label,flush=True)
    cmd=[sys.executable,str(root/'tools/probes/snapshot-cost/run.py'),'--exe',f'/tmp/snapshot-cost-{backend}/bin/snapshotCost','--out',str(results/label),'--width','1920','--height','1080','--windows',str(windows),'--samples',str(samples),'--vsync','1','--frames','60','--warmup','20','--blocks','8']
    with (results/(label+'.log')).open('w') as output:
        result=subprocess.run(cmd,stdout=output,stderr=subprocess.STDOUT,timeout=240)
    print(f'END {label}: {result.returncode}',flush=True)
    if result.returncode:raise SystemExit(result.returncode)
