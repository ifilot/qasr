# SPDX-License-Identifier: GPL-3.0-only
"""Interleave surviving AA/PNG combinations using a completed baseline's cases."""
import argparse
import copy
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import random
import re
import resource
import shutil
import subprocess
import time
from benchmark import decode_png, digest, environment, summary

PROFILES={"2x-level0":("2x","0"),"2x-level1":("2x","1"),
          "adaptive-level0":("adaptive","0"),"adaptive-level1":("adaptive","1")}


def option(command,key,value):
    result=list(command)
    if key in result:
        result[result.index(key)+1]=value
    else:result.extend([key,value])
    return result


def inspection(command,mode):
    result=[];skip=False
    for arg in command:
        if skip:skip=False;continue
        if arg=="-o":skip=True;continue
        if arg=="--benchmark-stages":continue
        result.append(arg)
    return [*result,mode]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline",type=Path)
    parser.add_argument("--output-dir",type=Path,required=True)
    parser.add_argument("--samples",type=int,default=30)
    parser.add_argument("--cases",nargs="+",required=True,help="complete case names including style prefix")
    args=parser.parse_args()
    if args.samples<2:parser.error("at least two samples required")
    baseline=json.loads(args.baseline.read_text())
    if baseline.get("schema")!=2 or "finished_utc" not in baseline:parser.error("completed schema-2 baseline required")
    cases={c["name"]:c for c in baseline["cases"]}
    if set(args.cases)-set(cases):parser.error("case absent from baseline")
    out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    exe=Path(baseline["environment"]["executable"])
    if digest(exe)!=baseline["environment"]["executable_sha256"]:parser.error("baseline executable has changed")
    env=environment(exe)
    reports={}
    for profile in PROFILES:
        report=copy.deepcopy(baseline)
        report.update(label=profile,cases=[],started_utc=datetime.now(timezone.utc).isoformat(),
                      environment=env,warmups=3,sample_orders={},interleaved=True,
                      source_baseline=str(args.baseline.resolve()))
        report.pop("finished_utc",None)
        reports[profile]=report
        (out/(profile+"-images")).mkdir(exist_ok=True)
    shutil.copyfile(Path(__file__),out/"driver.py")
    def save():
        for profile,r in reports.items():
            (out/(profile+".json")).write_text(json.dumps(r,indent=2)+"\n")
    save()
    rng=random.Random(20261029)
    for name in args.cases:
        source=cases[name];commands={};data={};orders=[]
        if digest(Path(source["input"]))!=source["input_sha256"]:raise RuntimeError("baseline input changed")
        for profile,(aa,level) in PROFILES.items():
            cmd=option(source["command"],"--aa",aa)
            cmd=option(cmd,"--png-encoder","libdeflate")
            cmd=option(cmd,"--png-filter","none");cmd=option(cmd,"--png-compression",level)
            png=Path(cmd[cmd.index("-o")+1])
            cmd=option(cmd,"-o",str(png.with_name(profile+"-"+png.name)))
            commands[profile]=cmd
            data[profile]={"elapsed":[],"cpu":[],"stages":{k:[] for k in ("config","parse","scene","render","png")}}
            for _ in range(3):subprocess.run(cmd,check=True,capture_output=True,timeout=300)
        for _ in range(args.samples):
            order=list(PROFILES);rng.shuffle(order);orders.append(order)
            for profile in order:
                cmd=commands[profile];d=data[profile]
                before=resource.getrusage(resource.RUSAGE_CHILDREN);start=time.perf_counter_ns()
                proc=subprocess.run(cmd,check=True,capture_output=True,text=True,timeout=300)
                d["elapsed"].append((time.perf_counter_ns()-start)/1e6)
                after=resource.getrusage(resource.RUSAGE_CHILDREN)
                d["cpu"].append(1000*(after.ru_utime+after.ru_stime-before.ru_utime-before.ru_stime))
                stages=dict(re.findall(r"(config|parse|scene|render|png)=([0-9.]+)",proc.stderr))
                if set(stages)!=set(d["stages"]):raise RuntimeError("incomplete stage timings")
                for key,value in stages.items():d["stages"][key].append(float(value))
                d["counters"]={k:int(v) for k,v in re.findall(
                    r"(render_threads|raster_tiles|adaptive_native_tiles|adaptive_total_tiles)=([0-9]+)",proc.stderr)}
        for profile,cmd in commands.items():
            d=data[profile];record=copy.deepcopy(source)
            png=Path(cmd[cmd.index("-o")+1]);w,h,pixels=decode_png(png)
            settings=json.loads(subprocess.check_output(inspection(cmd,"--inspect"),text=True))["settings"]
            scene=json.loads(subprocess.check_output(inspection(cmd,"--inspect-scene"),text=True))
            if scene["camera"]!=source["camera"] or scene["bond_count"]!=source["bonds"]:
                raise RuntimeError("combination changed scene")
            retained=out/(profile+"-images")/png.name;shutil.copyfile(png,retained)
            record.update(command=cmd,settings=settings,samples=args.samples,elapsed_ms=d["elapsed"],
                          latency_ms=summary(d["elapsed"]),cpu_ms=d["cpu"],cpu_summary_ms=summary(d["cpu"]),
                          stages_ms=d["stages"],stage_summary_ms={k:summary(v) for k,v in d["stages"].items()},
                          render_counters=d["counters"],rss_kib_separate_run=None,user_cycles_separate_runs=[],
                          png=str(retained),png_bytes=png.stat().st_size,png_sha256=digest(png),
                          decoded_rgba_sha256=hashlib.sha256(pixels).hexdigest(),width=w,height=h)
            reports[profile]["cases"].append(record)
            reports[profile]["sample_orders"][name]=orders
            print(name,profile,f"{record['latency_ms']['median']:.2f} ms",flush=True)
        save()
    for r in reports.values():r["finished_utc"]=datetime.now(timezone.utc).isoformat()
    save()


if __name__=="__main__":
    main()
