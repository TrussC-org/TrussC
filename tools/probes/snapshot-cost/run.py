#!/usr/bin/env python3
"""Run one device/configuration and summarize the probe's block CSV."""
import argparse, datetime, hashlib, json, math, os, pathlib, platform, subprocess, sys
p=argparse.ArgumentParser()
p.add_argument('--exe',required=True);p.add_argument('--out',required=True)
for k,v in dict(width=1920,height=1080,windows=1,samples=1,vsync=0,frames=180,warmup=60,blocks=8).items():p.add_argument('--'+k,type=int,default=v)
a=p.parse_args();out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
keys=['width','height','windows','samples','vsync','frames','warmup','blocks']
cmd=[str(pathlib.Path(a.exe).resolve())]
for k in keys:cmd+=['--'+k,str(getattr(a,k))]
metadata=dict(environment={k:os.environ[k] for k in ('DISPLAY','LIBGL_ALWAYS_SOFTWARE','__GLX_VENDOR_LIBRARY_NAME','__EGL_VENDOR_LIBRARY_FILENAMES','MESA_SHADER_CACHE_DIR','LP_NUM_THREADS') if k in os.environ},utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),host=platform.node(),platform=platform.platform(),machine=platform.machine(),processor=platform.processor(),command=cmd)
metadata['probe_sha256']={str(p.relative_to(pathlib.Path(__file__).parent)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [pathlib.Path(__file__),pathlib.Path(__file__).parent/'src/main.cpp',pathlib.Path(__file__).parent/'src/native.cpp',pathlib.Path(__file__).parent/'CMakeLists.txt']}
try:metadata['base']=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
except (OSError,subprocess.SubprocessError):pass
(out/'metadata.json').write_text(json.dumps(metadata,indent=2)+'\n')
result=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
(out/'raw.log').write_text(result.stdout,encoding='utf-8');print(result.stdout,end='')
if result.returncode:sys.exit(result.returncode)
columns='window,block,snapshot,cpu_n,gpu_n,frame_interval_ms,fps,cpu_submit_ms,cpu_p95_ms,scene_gpu_ms,copy_gpu_ms,total_gpu_ms,gpu_p95_ms'.split(',')
rows=[]
for line in result.stdout.splitlines():
    parts=line.split(',')
    if len(parts)==len(columns) and parts[0].isdigit():rows.append(dict(zip(columns,map(float,parts))))
summary=[]
for window in range(a.windows):
    modes=[]
    for mode in (0,1):
        rs=[r for r in rows if r['window']==window and r['snapshot']==mode]
        if not rs:raise RuntimeError('missing result blocks')
        if any(r['cpu_n']!=a.frames for r in rs):raise RuntimeError('incomplete CPU sample block')
        def avg(key):
            weight='gpu_n' if 'gpu' in key else 'cpu_n'
            pairs=[(r[key],r[weight]) for r in rs if math.isfinite(r[key]) and r[weight]>0]
            return sum(v*n for v,n in pairs)/sum(n for _,n in pairs) if pairs else None
        m=dict(snapshot=bool(mode),cpu_n=sum(int(r['cpu_n']) for r in rs),gpu_n=sum(int(r['gpu_n']) for r in rs))
        for key in ['frame_interval_ms','cpu_submit_ms','scene_gpu_ms','copy_gpu_ms','total_gpu_ms']:m[key]=avg(key)
        m['fps']=1000/m['frame_interval_ms']
        m['gpu_block_range_ms']=[min(r['total_gpu_ms'] for r in rs),max(r['total_gpu_ms'] for r in rs)] if m['gpu_n'] else None
        modes.append(m)
    off,on=modes
    gpu_delta=(on['total_gpu_ms']-off['total_gpu_ms']) if on['total_gpu_ms'] is not None and off['total_gpu_ms'] is not None else None
    summary.append(dict(window=window,modes=modes,gpu_delta_ms=gpu_delta,gpu_delta_percent=100*gpu_delta/off['total_gpu_ms'] if gpu_delta is not None and off['total_gpu_ms'] else None,fps_delta=on['fps']-off['fps'],fps_delta_percent=100*(on['fps']/off['fps']-1)))
(out/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
print(json.dumps(summary,indent=2,allow_nan=False))
