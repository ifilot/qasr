# SPDX-License-Identifier: GPL-3.0-only
"""Measure bounded worker/thread combinations on repeated identical rendering jobs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from generate_m10_workloads import geometry,poscar,digest


def cpu_ms(processes):
    ticks=0
    for p in processes:
        fields=Path(f'/proc/{p.pid}/stat').read_text().rsplit(')',1)[1].split();ticks+=int(fields[11])+int(fields[12])
    return ticks*1000/os.sysconf('SC_CLK_TCK')


def rss_kib(processes):
    total=0
    for p in processes:
        for line in Path(f'/proc/{p.pid}/status').read_text().splitlines():
            if line.startswith('VmRSS:'):total+=int(line.split()[1])
    return total


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('executable',type=Path);ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--jobs',type=int,default=16);ap.add_argument('--samples',type=int,default=5);ap.add_argument('--workers',nargs='+',type=int,default=[1,2,4]);ap.add_argument('--threads',nargs='+',type=int,default=[1,2,4,8])
    args=ap.parse_args()
    if args.samples<1 or args.jobs<1 or any(v<1 for v in args.workers+args.threads):ap.error('positive counts required')
    exe=args.executable.resolve();args.output.parent.mkdir(parents=True,exist_ok=True);work=Path(tempfile.mkdtemp(prefix='qasr-pool-'))
    source=work/'POSCAR';source.write_text(poscar(geometry(1000,'slab',1729)));expected=None
    report={'executable_sha256':digest(exe),'input_sha256':digest(source),'scope':'native WSL warm persistent workers, 1000 atoms, 1024x1024 2xAA, libpng 1; CPU from /proc ticks; RSS is an idle snapshot, not aggregate peak','results':[],'passed':False,'work':str(work)}
    for count in args.workers:
        for threads in args.threads:
            processes=[];logs=[];results=[]
            def request(i,index):
                job={'input':str(source),'output':str(work/f'pool-{count}-{threads}-{i}.png'),'threads':threads,'config':{'image':{'width':1024,'height':1024,'png_encoder':'libpng','png_compression':1}}}
                p=processes[i];p.stdin.write(json.dumps({'id':str(index),'op':'render','job':job})+'\n');p.stdin.flush();reply=json.loads(p.stdout.readline())
                if not reply['ok']:raise RuntimeError(reply)
                if expected and reply['result']['output']['sha256']!=expected:raise RuntimeError('pool output bytes changed')
                return reply['result']
            try:
                for i in range(count):
                    log=(work/f'pool-{count}-{threads}-{i}.log').open('w');logs.append(log)
                    processes.append(subprocess.Popen([str(exe),'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True))
                with ThreadPoolExecutor(max_workers=count) as pool:
                    warm=list(pool.map(lambda i:request(i,'warmup'),range(count)))
                    if expected is None:expected=warm[0]['output']['sha256']
                    if any(v['output']['sha256']!=expected for v in warm):raise RuntimeError('pool warmup output changed')
                    snapshots=[]
                    for sample in range(args.samples):
                        before=cpu_ms(processes);start=time.perf_counter()
                        def jobs(i):return [request(i,f'{sample}-{n}') for n in range(i,args.jobs,count)]
                        finished=list(pool.map(jobs,range(count)));wall=time.perf_counter()-start;cpu=cpu_ms(processes)-before
                        snapshots.append({'wall_ms':wall*1000,'cpu_ms':cpu,'jobs_per_second':args.jobs/wall,'rss_kib_idle_sum':rss_kib(processes)})
                    results={'workers':count,'threads_per_worker':threads,'render_threads_used':sorted({v['render_threads'] for v in warm} | {v['render_threads'] for chunk in finished for v in chunk}),'jobs_per_sample':args.jobs,'samples':snapshots,'png_sha256':expected}
                    report['results'].append(results);args.output.write_text(json.dumps(report,indent=2)+'\n');print(count,threads,round(sum(v['jobs_per_second'] for v in snapshots)/len(snapshots),2),flush=True)
            finally:
                for p in processes:
                    if p.poll() is None:p.stdin.close()
                for p in processes:
                    try:p.wait(timeout=30)
                    except subprocess.TimeoutExpired:p.kill();p.wait()
                    p.stdout.close()
                for log in logs:log.close()
    report['passed']=True;args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
