# SPDX-License-Identifier: GPL-3.0-only
"""M3 warm-cache process-lifetime and stage baseline; no third-party Python deps."""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import tempfile
import time


def synthetic(count):
    nx=math.ceil(count ** (1/3))
    ny=nx
    nz=math.ceil(count/(nx*ny))
    cell=[nx*2.5,ny*2.5,nz*2.5]
    lines=["Synthetic cubic Co benchmark", "1", f"{cell[0]} 0 0",f"0 {cell[1]} 0",f"0 0 {cell[2]}","Co",str(count),"Cartesian"]
    for i in range(count):
        x=i%nx;y=i//nx%ny;z=i//(nx*ny)
        lines.append(f"{(x+0.5)*2.5} {(y+0.5)*2.5} {(z+0.5)*2.5}")
    return "\n".join(lines)+"\n"


def command_output(*args):
    return subprocess.check_output(args,text=True).strip()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable",type=Path)
    parser.add_argument("--output",type=Path,default=Path("artifacts/m3/baseline.json"))
    parser.add_argument("--samples",type=int,default=20)
    args=parser.parse_args()
    if args.samples<2:
        parser.error("at least two samples required")
    exe=args.executable.resolve()
    cache=(exe.parent/"CMakeCache.txt").read_text()
    build_type=re.search(r"^CMAKE_BUILD_TYPE:STRING=(.*)$",cache,re.MULTILINE)
    if not build_type or build_type.group(1)!="Release":
        parser.error("benchmark a CMake Release build")
    compiler=re.search(r"^CMAKE_C_COMPILER:FILEPATH=(.*)$",cache,re.MULTILINE).group(1)
    flags=re.search(r"^CMAKE_C_FLAGS_RELEASE:STRING=(.*)$",cache,re.MULTILINE).group(1)
    common_flags=re.search(r"^CMAKE_C_FLAGS:STRING=(.*)$",cache,re.MULTILINE).group(1)
    root=Path(__file__).resolve().parents[1]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    cases=[]
    for n in [100,500,1000]:
        path=args.output.parent/f"POSCAR.co-{n}"
        path.write_text(synthetic(n))
        cases.append((f"Co-{n}",path,[]))
    cases.extend([
        ("Co-slab-repeat-2x2x2",root/"tests/fixtures/slabs/01_co0001_clean/CONTCAR",["--repeat","2,2,2"]),
        ("Pd-ceria-249",root/"tests/fixtures/slabs/18_pd_ceria_co_oxidation_ts/CONTCAR",[]),
        ("OUTCAR-static",root/"tests/fixtures/OUTCAR.static",[]),
    ])
    outcar=args.output.parent/"OUTCAR.vasp5"
    outcar.write_bytes(gzip.decompress((root/"tests/fixtures/OUTCAR.vasp5.gz").read_bytes()))
    cases.append(("OUTCAR-relax-6MB",outcar,[]))
    results=[]
    for name,path,extra in cases:
        png=args.output.parent/f"benchmark-{name}.png"
        cmd=[str(exe),str(path),"-o",str(png),"--threads","1","--benchmark-stages",*extra]
        for _ in range(3):
            subprocess.run(cmd,check=True,capture_output=True)
        elapsed=[];stages={k:[] for k in ["config","parse","scene","render","png"]}
        for _ in range(args.samples):
            start=time.perf_counter_ns()
            proc=subprocess.run(cmd,check=True,capture_output=True,text=True)
            elapsed.append((time.perf_counter_ns()-start)/1e6)
            for k,value in re.findall(r"(config|parse|scene|render|png)=([0-9.]+)",proc.stderr):
                stages[k].append(float(value))
        rss=None
        if Path("/usr/bin/time").exists():
            with tempfile.TemporaryDirectory(prefix="qasr-bench-") as tmp:
                stat=Path(tmp)/"rss"
                subprocess.run(["/usr/bin/time","-f","%M","-o",str(stat),*cmd],check=True,capture_output=True)
                rss=int(stat.read_text())
        scene=json.loads(subprocess.check_output([str(exe),str(path),"--inspect-scene",*extra],text=True))
        results.append({"name":name,"input":str(path),"input_sha256":hashlib.sha256(path.read_bytes()).hexdigest(),
                        "instances":scene["instance_count"],"bonds":scene["bond_count"],
                        "samples":args.samples,"median_ms":statistics.median(elapsed),
                        "p95_ms":sorted(elapsed)[math.ceil(0.95*len(elapsed))-1],"elapsed_ms":elapsed,
                        "stage_medians_ms":{k:statistics.median(v) for k,v in stages.items()},
                        "rss_kib_separate_run":rss,"png_bytes":png.stat().st_size,
                        "png":str(png),"options":extra})
        print(name,round(results[-1]["median_ms"],2),"ms median",flush=True)
    cpu=next((line.split(":",1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")),"unknown")
    governor=Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    report={"measurement":"warm-cache process launch through exit, including PNG close/rename and cleanup; subprocess/capture overhead included",
            "warmups":3,"cache_state":"warm, uncontrolled OS cache; not OS-cold-cache measurements",
            "platform":platform.platform(),"cpu":cpu,"cpu_count":os.cpu_count(),
            "governor":governor.read_text().strip() if governor.exists() else "unavailable in this environment",
            "filesystem":command_output("stat","-f","-c","%T",str(args.output.parent)),
            "compiler":command_output(compiler,"--version").splitlines()[0],
            "build":f"CMake Release, {common_flags} {flags}; single thread",
            "libpng":command_output("pkg-config","--modversion","libpng"),
            "zlib":command_output("pkg-config","--modversion","zlib"),
            "image":"512x512 RGBA, fixed 2x AA, libpng compression 1, default view and cell",
            "executable_sha256":hashlib.sha256(exe.read_bytes()).hexdigest(),"cases":results}
    args.output.write_text(json.dumps(report,indent=2)+"\n")


if __name__=="__main__":
    main()
