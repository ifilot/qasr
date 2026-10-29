# SPDX-License-Identifier: GPL-3.0-only
"""Compare OpenMP policies/binaries with identical inputs, PNG bytes and raw samples."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import statistics
import subprocess
import sys
import tempfile
import time

from benchmark import environment,summary
from generate_m10_workloads import geometry,poscar,digest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from png_support import decode_png
ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--executable',action='append',required=True,help='label=/absolute/path; first is reference')
    p.add_argument('--threads',type=int,nargs='+',default=[1,2,4,8,16]);p.add_argument('--samples',type=int,default=10)
    p.add_argument('--warmups',type=int,default=3);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cases',nargs='+',choices=['tiny','dense','aa-heavy','annotated'])
    args=p.parse_args()
    if args.samples<2 or args.warmups<0 or any(t<1 or t>256 for t in args.threads):p.error('invalid samples/warmups/threads')
    args.output.parent.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='qasr-openmp-'))
    synthetic=work/'POSCAR';synthetic.write_text(poscar(geometry(1000,'slab',1729)))
    tiny=work/'tiny';tiny.write_text(poscar(geometry(32,'bulk',1729)))
    cases=[('tiny',tiny,['--width','128','--height','128']),
           ('dense',synthetic,['--width','512','--height','512']),
           ('aa-heavy',ROOT/'tests/fixtures/slabs/01_co0001_clean/CONTCAR',['--view','top','--width','768','--height','768','--aa','4x']),
           ('annotated',ROOT/'tests/fixtures/slabs/18_pd_ceria_co_oxidation_ts/CONTCAR',['--width','768','--height','768','--labels','element','--legend','on','--axes','on'])]
    if args.cases:cases=[case for case in cases if case[0] in args.cases]
    report={'scope':'WSL warm-cache CLI including startup/PNG; explicit libpng settings; no cold-cache claim','samples':args.samples,'warmups':args.warmups,'work':str(work),'executables':{},'results':[],'passed':False}
    references={}
    def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
    for item in args.executable:
        label,value=item.split('=',1);exe=Path(value).resolve();report['executables'][label]=environment(exe);save()
        for name,source,flags in cases:
            for threads in args.threads:
                out=work/f'{label}-{name}-{threads}.png'
                command=[str(exe),str(source),*flags,'--no-user-config','--threads',str(threads),'--png-encoder','libpng','--png-compression','1','--benchmark-stages','-o',str(out)]
                for _ in range(args.warmups):subprocess.run(command,check=True,capture_output=True)
                times=[];cpu=[];stages=[]
                for _ in range(args.samples):
                    usage=resource.getrusage(resource.RUSAGE_CHILDREN);before=usage.ru_utime+usage.ru_stime;start=time.perf_counter()
                    result=subprocess.run(command,check=True,capture_output=True,text=True);times.append((time.perf_counter()-start)*1000)
                    usage=resource.getrusage(resource.RUSAGE_CHILDREN);cpu.append((usage.ru_utime+usage.ru_stime-before)*1000)
                    match=re.search(r'stages_ms: config=([\d.]+) parse=([\d.]+) scene=([\d.]+) render=([\d.]+) png=([\d.]+)',result.stderr)
                    stages.append(dict(zip(['config','parse','scene','render','png'],map(float,match.groups()))))
                    sha=digest(out)
                    if name not in references:
                        w,h,rgba=decode_png(out);references[name]={'png':sha,'rgba':hashlib.sha256(rgba).hexdigest(),'width':w,'height':h}
                    if references[name]['png']!=sha:raise ValueError('PNG pixels/encoding changed: '+label+' '+name)
                record={'binary':label,'case':name,'threads':threads,'threads_used':int(re.search(r'render_threads=(\d+)',result.stderr).group(1)),'command':command,'input_sha256':digest(source),'wall_ms':summary(times),'cpu_ms':summary(cpu),'elapsed_ms':times,'cpu_samples_ms':cpu,'stage_samples_ms':stages,'stage_median_ms':{k:statistics.median(s[k] for s in stages) for k in stages[0]},'png_sha256':sha,'rgba_sha256':references[name]['rgba']}
                report['results'].append(record);save();print(label,name,threads,round(record['wall_ms']['median'],2),flush=True)
    report['passed']=True;save()


if __name__=='__main__':main()
