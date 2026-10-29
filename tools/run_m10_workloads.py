# SPDX-License-Identifier: GPL-3.0-only
"""Validate a synthetic M10 suite and measure the existing independent-call baseline."""
import argparse
import copy
from datetime import datetime,timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import resource
import statistics
import selectors
import signal
import subprocess
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from png_support import decode_png
from generate_m10_workloads import digest

FAMILIES=['independent','views','styles','frames','periodic','isolation','invalidation']


def confined(root,value):
    relative=Path(value)
    if relative.is_absolute() or '..' in relative.parts:raise ValueError('suite paths must be relative and confined: '+value)
    path=(root/relative).resolve()
    if not path.is_relative_to(root.resolve()):raise ValueError('suite path escapes its root: '+value)
    return path


def verify_suite(suite,exe,capabilities):
    manifest=json.loads((suite/'manifest.json').read_text())
    if manifest.get('schema_version')!=1 or manifest.get('generator_version')!=1:raise ValueError('unsupported synthetic suite version')
    for name,expected in manifest['files'].items():
        path=confined(suite,name)
        if path.stat().st_size!=expected['bytes'] or digest(path)!=expected['sha256']:raise ValueError('suite file changed: '+name)
    checked=0
    for name,entry in manifest['inputs'].items():
        if 'expected_exit' in entry or entry['format']=='xml' and not capabilities.get('xml'):continue
        oracles=entry.get('frame_oracles',{'0':entry.get('oracle')})
        for frame,oracle_path in oracles.items():
            expected=json.loads(confined(suite,oracle_path).read_text())
            p=subprocess.run([str(exe),str(confined(suite,name)),'--frame',frame,'--inspect','--no-user-config'],capture_output=True,text=True,timeout=120)
            if p.returncode:raise ValueError('oracle inspection failed: '+name+': '+p.stderr)
            actual=json.loads(p.stdout)
            if (actual['atom_count'],actual['frame_count'],actual['frame'])!=(expected['atoms'],entry['frames'],int(frame)):
                raise ValueError('input/frame metadata differs from oracle: '+name)
            if [a['element'] for a in actual['atoms']]!=expected['elements']:raise ValueError('source order differs from oracle: '+name)
            for av,ev in zip(actual['lattice_A'],expected['lattice_A']):
                if any(not math.isclose(a,e,rel_tol=0,abs_tol=1e-9) for a,e in zip(av,ev)):raise ValueError('lattice differs from oracle: '+name)
            for atom,position in zip(actual['atoms'],expected['positions_A']):
                if any(not math.isclose(a,e,rel_tol=0,abs_tol=1e-9) for a,e in zip(atom['position_A'],position)):raise ValueError('position differs from oracle: '+name)
            checked+=1
    return manifest,checked


def summary(values):
    return {'median':statistics.median(values),'p95':sorted(values)[math.ceil(.95*len(values))-1],'min':min(values),'max':max(values)}


def run_suite(executable,suite,output,families=None,samples=3,warmups=1,threads=None,measure_rss=False,reference=None,backend="cli",cache_mib=64):
    if backend not in ['cli','worker'] or not 0<=cache_mib<=512:raise ValueError('invalid backend/cache limit')
    if samples<1 or samples>1000 or warmups<0 or warmups>100:raise ValueError('samples must be 1..1000 and warmups 0..100')
    if threads is not None and threads!='auto' and (not str(threads).isdigit() or not 1<=int(threads)<=256):raise ValueError('threads must be auto or 1..256')
    if measure_rss and not Path('/usr/bin/time').is_file():raise ValueError('--measure-rss requires /usr/bin/time')
    exe=Path(executable).resolve();suite=Path(suite).resolve();output=Path(output).absolute()
    capabilities=json.loads(subprocess.check_output([str(exe),'--build-info'],text=True))
    if not all(capabilities.get(k) for k in ['user_config','atom_overrides','job_requests']):raise ValueError('M9-capable executable required')
    if backend=='worker' and not capabilities.get('batch_worker'):raise ValueError('worker-capable executable required')
    manifest,checked=verify_suite(suite,exe,capabilities)
    families=families or FAMILIES
    if not families or len(set(families))!=len(families) or any(f not in FAMILIES for f in families):raise ValueError('unknown or duplicate workload family')
    previous=json.loads(Path(reference).read_text()) if reference else None
    manifest_hash=digest(suite/'manifest.json')
    if previous and (previous.get('suite_sha256')!=manifest_hash or not previous.get('passed')):raise ValueError('reference must be a successful run of the same suite')
    reference_records={v['id']:v for v in previous['results']} if previous else {}
    output.parent.mkdir(parents=True,exist_ok=True);output.mkdir()
    report={'schema_version':1,'backend':backend,'cache_mib':cache_mib if backend=='worker' else None,
            'started_utc':datetime.now(timezone.utc).isoformat(),'suite_sha256':manifest_hash,'executable':str(exe),'executable_sha256':digest(exe),
            'capabilities':capabilities,'platform':platform.platform(),'cpu_count':os.cpu_count(),
            'samples':samples,'warmups':warmups,'threads_override':threads,'oracles_checked':checked,
            'measurement':('persistent request/response latency; first request includes startup; per-request CPU unavailable' if backend=='worker' else 'warm-cache process launch through exit')+'; report/hashing included, decode/oracles excluded; no cold-cache claim',
            'rss_measurement':('whole-worker peak RSS with GNU time' if backend=='worker' and measure_rss else 'one separate /usr/bin/time peak-RSS run per request in KiB' if measure_rss else 'not measured'),
            'results':[],'skipped':[],'passed':False,
            'input_filesystem':subprocess.check_output(['stat','-f','-c','%T',str(suite)],text=True).strip(),
            'output_filesystem':subprocess.check_output(['stat','-f','-c','%T',str(output.parent)],text=True).strip()}
    def save():
        temporary=output/'report.json.tmp';temporary.write_text(json.dumps(report,indent=2)+'\n');temporary.replace(output/'report.json')
    save();pixels={};active_times={};worker=None;worker_log=None
    if backend=='worker':
        command=[str(exe),'--batch','--cache-mib',str(cache_mib)]
        if measure_rss:command=['/usr/bin/time','-f','%M','-o',str(output/'worker-peak-rss.txt'),*command]
        worker_log=(output/'worker-stderr.log').open('w')
        worker=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=worker_log,start_new_session=True)
    def worker_call(identifier,job):
        payload=json.dumps({'id':identifier,'op':'render','job':job}).encode()+b'\n'
        worker.stdin.write(payload);worker.stdin.flush()
        ready=selectors.DefaultSelector();ready.register(worker.stdout,selectors.EVENT_READ)
        try:
            if not ready.select(300):raise ValueError('worker response timeout')
            line=worker.stdout.readline(1048577)
        finally:ready.close()
        if not line or not line.endswith(b'\n'):raise ValueError('worker exited or returned an oversized/incomplete response')
        reply=json.loads(line)
        if reply.get('id')!=identifier:raise ValueError('worker response id mismatch')
        if reply['ok']:return subprocess.CompletedProcess([],0,json.dumps(reply['result']),'')
        return subprocess.CompletedProcess([],reply['error']['exit_code'],'',json.dumps({'ok':False,'error':reply['error']}))
    try:
        for family in families:
            entries=[json.loads(line) for line in confined(suite,manifest['plans'][family]['path']).read_text().splitlines() if line.strip()]
            if len(entries)!=manifest['plans'][family]['requests']:raise ValueError('plan request count differs from manifest')
            for entry in entries:
                identifier=entry['id']
                if any(not capabilities.get(k) for k in entry['requires']):
                    report['skipped'].append(identifier);continue
                if entry['action']!='render':raise ValueError('unsupported driver action')
                job=copy.deepcopy(entry['job']);input_path=confined(suite,job['input'])
                expected_input_hash=digest(input_path)
                if entry.get('active_input'):
                    active=confined(output,entry['active_input']);active.parent.mkdir(parents=True,exist_ok=True)
                    with active.open('wb') as f:
                        with input_path.open('rb') as source:
                            for chunk in iter(lambda:source.read(65536),b''):f.write(chunk)
                    if str(active) not in active_times:active_times[str(active)]=active.stat().st_mtime_ns
                    fixed=active_times[str(active)];os.utime(active,ns=(fixed,fixed));input_path=active
                job['input']=str(input_path);out=confined(output,job['output']);out.parent.mkdir(parents=True,exist_ok=True)
                job['output']=str(out);job['report']='-'
                if threads is not None:job['threads']='auto' if threads=='auto' else int(threads)
                payload=json.dumps(job);expected=entry['expected'];sentinel=b'preserved-on-error\n'
                if expected['exit_code']:out.write_bytes(sentinel)
                elapsed=[];cpu=[];stages=[];last_report=None;rgba_hash=None;last_png_hash=None;error_code=None
                def execute(command):
                    return worker_call(identifier,job) if worker else subprocess.run(command,input=payload,capture_output=True,text=True,timeout=300)
                def check(p):
                    nonlocal last_report,rgba_hash,last_png_hash,error_code
                    if p.returncode!=expected['exit_code']:raise ValueError(f'{identifier}: expected exit {expected["exit_code"]}, got {p.returncode}: {p.stderr}')
                    if p.returncode:
                        error=json.loads(p.stderr)['error'];error_code=error['code']
                        if error['exit_code']!=p.returncode or out.read_bytes()!=sentinel or p.stdout:raise ValueError('failed request changed output or error contract: '+identifier)
                        return
                    last_report=json.loads(p.stdout)
                    if not last_report['ok'] or last_report['input']['sha256']!=expected_input_hash:raise ValueError('input hash differs: '+identifier)
                    if (last_report['input']['atoms'],last_report['input']['frame'])!=(expected['atoms'],expected['frame']):raise ValueError('report atoms/frame differ: '+identifier)
                    png_hash=digest(out)
                    if last_report['output']['sha256']!=png_hash:raise ValueError('output hash differs: '+identifier)
                    # Identical encoded bytes already prove identical pixels. Decode
                    # once per job, or again if a lossless encoding changes.
                    if png_hash!=last_png_hash:
                        w,h,rgba=decode_png(out)
                        if (w,h)!=(job['config']['image']['width'],job['config']['image']['height']):raise ValueError('image dimensions differ: '+identifier)
                        current=hashlib.sha256(rgba).hexdigest()
                        if rgba_hash and current!=rgba_hash:raise ValueError('repeated render pixels changed: '+identifier)
                        rgba_hash=current;last_png_hash=png_hash
                command=[str(exe),'--job','-']
                for _ in range(warmups):check(execute(command))
                for _ in range(samples):
                    usage=resource.getrusage(resource.RUSAGE_CHILDREN);before_cpu=usage.ru_utime+usage.ru_stime
                    start=time.perf_counter();p=execute(command);elapsed.append((time.perf_counter()-start)*1000)
                    usage=resource.getrusage(resource.RUSAGE_CHILDREN);cpu.append(None if worker else (usage.ru_utime+usage.ru_stime-before_cpu)*1000)
                    check(p)
                    if last_report:stages.append(last_report['stages_ms'])
                rss=None
                if measure_rss and not worker:
                    rss_path=output/'peak-rss.txt'
                    check(execute(['/usr/bin/time','-f','%M','-o',str(rss_path),*command]))
                    # GNU time adds an exit-status line for expected failure cases.
                    rss=int(rss_path.read_text().splitlines()[-1]);rss_path.unlink()
                if expected.get('same_pixels_as') and rgba_hash!=pixels[expected['same_pixels_as']]:raise ValueError('state/frame/pixel restoration failed: '+identifier)
                if expected.get('different_pixels_from') and rgba_hash==pixels[expected['different_pixels_from']]:raise ValueError('input mutation did not change rendered pixels: '+identifier)
                pixels[identifier]=rgba_hash
                record={'id':identifier,'family':family,'request_sha256':hashlib.sha256(json.dumps(entry,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
                        'exit_code':expected['exit_code'],'error_code':error_code,'input_sha256':expected_input_hash,
                        'decoded_rgba_sha256':rgba_hash,'wall_ms':summary(elapsed),'elapsed_ms':elapsed,'cpu_ms':cpu,'peak_rss_kib_separate_run':rss,
                        'stages_ms':stages,'counts':last_report['counts'] if last_report else None,
                        'cache':last_report.get('cache') if last_report else None,
                        'input_identity':{'bytes':input_path.stat().st_size,'mtime_ns':input_path.stat().st_mtime_ns,'inode':input_path.stat().st_ino}}
                if previous:
                    old=reference_records.get(identifier)
                    if not old or any(old.get(k)!=record[k] for k in ['request_sha256','exit_code','error_code','input_sha256','decoded_rgba_sha256','counts']):raise ValueError('reference correctness differs: '+identifier)
                    record['reference_median_ratio']=record['wall_ms']['median']/old['wall_ms']['median']
                report['results'].append(record);save()
            print(f'{family}: checked',flush=True)
        if worker:
            worker.stdin.close();worker.wait(timeout=30)
            if worker.returncode:raise ValueError('worker exited unsuccessfully')
            if measure_rss:report['worker_peak_rss_kib']=int((output/'worker-peak-rss.txt').read_text().splitlines()[-1])
        report['passed']=True;report['finished_utc']=datetime.now(timezone.utc).isoformat();save()
    except BaseException as e:
        report['failure']=str(e);save();raise
    finally:
        if worker and worker.poll() is None:
            try:os.killpg(worker.pid,signal.SIGKILL)
            except ProcessLookupError:pass
            worker.wait()
        if worker:
            worker.stdout.close()
            if not worker.stdin.closed:worker.stdin.close()
        if worker_log:worker_log.close()
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('executable',type=Path);p.add_argument('suite',type=Path);p.add_argument('--output',type=Path,help='new run directory; required unless --validate-only')
    p.add_argument('--families',nargs='+',choices=FAMILIES);p.add_argument('--samples',type=int,default=3);p.add_argument('--warmups',type=int,default=1)
    p.add_argument('--threads',help='override job thread count: auto or 1..256');p.add_argument('--measure-rss',action='store_true');p.add_argument('--reference',type=Path)
    p.add_argument('--backend',choices=['cli','worker'],default='cli');p.add_argument('--cache-mib',type=int,default=64)
    p.add_argument('--validate-only',action='store_true',help='check suite hashes and input/frame oracles without rendering')
    args=p.parse_args();validate_only=vars(args).pop('validate_only')
    if validate_only:
        try:
            capabilities=json.loads(subprocess.check_output([str(args.executable.resolve()),'--build-info']))
            _,checked=verify_suite(args.suite.resolve(),args.executable.resolve(),capabilities)
        except (ValueError,OSError,KeyError,subprocess.SubprocessError) as e:p.exit(1,str(e)+'\n')
        print(f'Validated dataset hashes and {checked} input/frame oracles');return
    if args.output is None:p.error('--output is required unless --validate-only')
    try:report=run_suite(**vars(args))
    except (ValueError,OSError,KeyError,subprocess.SubprocessError) as e:p.exit(1,str(e)+'\n')
    print(f'Validated {len(report["results"])} requests ({len(report["skipped"])} skipped); report: {args.output}/report.json')


if __name__=='__main__':main()
