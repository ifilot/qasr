# SPDX-License-Identifier: GPL-3.0-only
"""Compare two QASR executables on staged CO meshes; positional arguments: baseline candidate output.json.

Records seven interleaved runs per executable/case and requires identical PNGs.
"""
import argparse,hashlib,json,statistics,subprocess,tempfile,time,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('baseline');p.add_argument('candidate');p.add_argument('output');a=p.parse_args()
root=Path(__file__).resolve().parents[1];src=root/'examples/orbitals'
executables=[Path(a.baseline).resolve(),Path(a.candidate).resolve()]
results=[]
with tempfile.TemporaryDirectory(prefix='qasr-orb-bench-') as d:
 tmp=Path(d)
 for mo in [2,7,10]:
  recipe=src/f'co-{mo:02}.json'
  if not recipe.exists():recipe=src/'recipes'/recipe.name
  config=json.loads(recipe.read_text())
  for layer in config['orbitals']:
   original=(recipe.parent/layer['file']).resolve();dest=tmp/original.name;shutil.copyfile(original,dest);layer['file']=str(dest)
  job=tmp/f'{mo}.json';job.write_text(json.dumps(config))
  for style,outline in [('illustrated','1.5'),('illustrated','0'),('shaded','1.5')]:
   case={'mo':mo,'style':style,'outline_width':outline,'seconds':[[],[]],'hashes':[None,None]}
   commands=[]
   for index,exe in enumerate(executables):
    out=tmp/f'{mo}-{style}-{outline}-{index}.png';commands.append(([str(exe),str(src/'co.xyz'),'--no-user-config','--config',str(job),'--style',style,'--outline-width',outline,'--threads','4','-o',str(out)],out))
   for index in range(2):subprocess.run(commands[index][0],check=True,capture_output=True)
   for repeat in range(7):
    for index in ([0,1] if repeat%2==0 else [1,0]):
     cmd,out=commands[index];start=time.perf_counter();subprocess.run(cmd,check=True,capture_output=True);case['seconds'][index].append(time.perf_counter()-start)
   for index,(_,out) in enumerate(commands):case['hashes'][index]=hashlib.sha256(out.read_bytes()).hexdigest()
   case['median']=[statistics.median(t) for t in case['seconds']];case['equal']=case['hashes'][0]==case['hashes'][1]
   results.append(case)
   print(mo,style,outline,[round(t*1000,1) for t in case['median']],case['equal'],flush=True)
record={'method':'7 interleaved warm-cache full CLI runs, four threads, staged meshes on same temporary filesystem, 480x400 with 4x AA; all other options unchanged.','executables':[{'path':str(e),'sha256':hashlib.sha256(e.read_bytes()).hexdigest()} for e in executables],'cases':results}
Path(a.output).parent.mkdir(parents=True,exist_ok=True);Path(a.output).write_text(json.dumps(record,indent=2)+'\n')
if not all(c['equal'] for c in results):raise SystemExit('Image parity failed')
