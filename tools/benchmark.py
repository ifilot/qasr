# SPDX-License-Identifier: GPL-3.0-only
"""Reproducible warm-cache workstation comparisons; no third-party Python deps."""
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
import shutil
import sys
import resource
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from png_support import decode_png
from cpu_counters import measure_cycles


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
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT).strip()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summary(values):
    return {"median": statistics.median(values),
            "p95": sorted(values)[math.ceil(0.95 * len(values)) - 1],
            "min": min(values), "max": max(values)}


def environment(exe):
    cache = (exe.parent / "CMakeCache.txt").read_text()
    def setting(key):
        match = re.search(r"^" + re.escape(key) + r":[^=]+=(.*)$", cache, re.MULTILINE)
        return match.group(1) if match else ""
    if setting("CMAKE_BUILD_TYPE") != "Release":
        raise ValueError("benchmark a CMake Release build")
    governor = Path("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")
    dependencies = {}
    for name in ("libpng", "zlib", "cairo", "libdeflate"):
        try:
            dependencies[name] = command_output("pkg-config", "--modversion", name)
        except subprocess.CalledProcessError:
            dependencies[name] = None
    cpu = next((line.split(":", 1)[1].strip()
                for line in Path("/proc/cpuinfo").read_text().splitlines()
                if line.startswith("model name")), "unknown")
    # Only tracked changes are captured; local credentials/settings stay excluded.
    diff = subprocess.check_output(["git", "-C", str(ROOT), "diff", "HEAD", "--",
                                    "src", "tools", "CMakeLists.txt", "presets", "tests"])
    return {
        "platform": platform.platform(), "cpu": cpu, "cpu_count": os.cpu_count(),
        "affinity": sorted(os.sched_getaffinity(0)),
        "meminfo": Path("/proc/meminfo").read_text(),
        "governor": governor.read_text().strip() if governor.exists() else "unavailable",
        "compiler": command_output(setting("CMAKE_C_COMPILER"), "--version").splitlines()[0],
        "build_type": setting("CMAKE_BUILD_TYPE"),
        "c_flags": setting("CMAKE_C_FLAGS"),
        "release_flags": setting("CMAKE_C_FLAGS_RELEASE"),
        "build_options": {key: setting(key) for key in (
            "ENABLE_SANITIZERS", "ENABLE_CAIRO_EXPERIMENTS", "ENABLE_OPENMP", "ENABLE_LIBDEFLATE")},
        "dependencies": dependencies,
        "linked_libraries": command_output("ldd", str(exe)),
        "executable": str(exe), "executable_sha256": digest(exe),
        "source_commit": command_output("git", "-C", str(ROOT), "rev-parse", "HEAD"),
        "tracked_diff_sha256": hashlib.sha256(diff).hexdigest(),
        "source_note": "Checkout provenance at benchmark start; for retained binaries consult their build source archive. Executable SHA-256 identifies the measured binary.",
    }


def cases(work):
    result = []
    for n in (100, 500, 1000):
        path = work / f"POSCAR.co-{n}"
        path.write_text(synthetic(n))
        result.append((f"Co-{n}", path, []))
    sources = [
        ("Co-slab", "slabs/01_co0001_clean/CONTCAR", []),
        ("Co-slab-repeat-2x2x1", "slabs/01_co0001_clean/CONTCAR", ["--repeat", "2,2,1"]),
        ("Co-slab-repeat-2x2x2", "slabs/01_co0001_clean/CONTCAR", ["--repeat", "2,2,2"]),
        ("Pd-ceria-249", "slabs/18_pd_ceria_co_oxidation_ts/CONTCAR", []),
        ("skew", "POSCAR.skew", []),
        ("OUTCAR-static", "OUTCAR.static", []),
        ("OUTCAR-relax-6MB", "OUTCAR.vasp5.gz", []),
    ]
    for name, source, options in sources:
        original = ROOT / "tests/fixtures" / source
        path = work / f"input-{name}"
        data = original.read_bytes()
        path.write_bytes(gzip.decompress(data) if original.suffix == ".gz" else data)
        result.append((name, path, options))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--output", type=Path, default=Path("artifacts/m4-m6/baseline.json"))
    parser.add_argument("--work-dir", type=Path,
                        help="input/output storage during timing; images are copied beside report")
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--warmups", type=int, default=3)
    parser.add_argument("--cycle-samples", type=int, default=3,
                        help="separate perf_event runs; zero disables hardware counters")
    parser.add_argument("--styles", nargs="+", choices=("shaded", "illustrated"),
                        default=["shaded", "illustrated"])
    parser.add_argument("--cases", nargs="+", help="case names; default: all")
    parser.add_argument("--label", default="reference")
    parser.add_argument("--threads", default="1", help="renderer thread count or auto")
    parser.add_argument("--render-args", nargs=argparse.REMAINDER, default=[],
                        help="extra renderer flags, passed last; must be the final benchmark option")
    args = parser.parse_args()
    if args.samples < 2 or args.warmups < 0 or args.cycle_samples < 0:
        parser.error("at least two samples and nonnegative warmups/cycle samples required")
    exe = args.executable.resolve()
    report_path = args.output.resolve()
    work = (args.work_dir or report_path.parent / "work").resolve()
    work.mkdir(parents=True, exist_ok=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    image_dir = report_path.parent / (report_path.stem + "-images")
    image_dir.mkdir(exist_ok=True)
    try:
        env = environment(exe)
    except ValueError as error:
        parser.error(str(error))
    preset = work / "illustrated.json"
    shutil.copyfile(ROOT / "presets/illustrated.json", preset)
    available = cases(work)
    if args.cases:
        missing = set(args.cases) - {name for name, _, _ in available}
        if missing:
            parser.error(f"unknown cases: {sorted(missing)}")
        available = [case for case in available if case[0] in args.cases]
    report = {
        "schema": 2, "label": args.label,
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "measurement": "warm-cache process launch through exit; includes capture/startup/PNG close/rename/cleanup",
        "cache_state": "warm; OS cache uncontrolled; no cold-cache claims",
        "warmups": args.warmups, "environment": env,
        "work_dir": str(work),
        "work_filesystem": command_output("stat", "-f", "-c", "%T", str(work)),
        "report_filesystem": command_output("stat", "-f", "-c", "%T", str(report_path.parent)),
        "preset_sha256": digest(preset), "render_args": args.render_args,
        "load_before": os.getloadavg(), "cases": [],
    }
    # Save provenance before starting, then checkpoint after every complete case.
    def save():
        report_path.write_text(json.dumps(report, indent=2) + "\n")
    save()
    shutil.copyfile(Path(__file__), report_path.parent / (report_path.stem + "-driver.py"))
    for style in args.styles:
        for name, path, options in available:
            key = f"{style}-{name}"
            png = work / f"{key}.png"
            settings = (["--config", str(preset)] if style == "illustrated" else [])
            settings += ["--threads", args.threads, *options, *args.render_args]
            cmd = [str(exe), str(path), "-o", str(png), "--benchmark-stages", *settings]
            for _ in range(args.warmups):
                subprocess.run(cmd, check=True, capture_output=True, timeout=300)
            elapsed = []
            cpu_ms = []
            stages = {k: [] for k in ("config", "parse", "scene", "render", "png")}
            for _ in range(args.samples):
                start = time.perf_counter_ns()
                before_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
                proc = subprocess.run(cmd, check=True, capture_output=True, text=True, timeout=300)
                elapsed.append((time.perf_counter_ns() - start) / 1e6)
                after_cpu = resource.getrusage(resource.RUSAGE_CHILDREN)
                cpu_ms.append(1000*(after_cpu.ru_utime+after_cpu.ru_stime
                                   -before_cpu.ru_utime-before_cpu.ru_stime))
                values = dict(re.findall(r"(config|parse|scene|render|png)=([0-9.]+)", proc.stderr))
                if set(values) != set(stages):
                    raise RuntimeError(f"incomplete stage timings: {proc.stderr}")
                for stage, value in values.items():
                    stages[stage].append(float(value))
            rss = None
            if Path("/usr/bin/time").exists():
                with tempfile.TemporaryDirectory(prefix="qasr-bench-") as tmp:
                    stat = Path(tmp) / "rss"
                    subprocess.run(["/usr/bin/time", "-f", "%M", "-o", str(stat), *cmd],
                                   check=True, capture_output=True, timeout=300)
                    rss = int(stat.read_text())
            scene = json.loads(subprocess.check_output(
                [str(exe), str(path), "--inspect-scene", *settings], text=True, timeout=300))
            inspected = json.loads(subprocess.check_output(
                [str(exe), str(path), "--inspect", *settings], text=True, timeout=300))
            cycle_samples = [measure_cycles(cmd) for _ in range(args.cycle_samples)]
            width, height, pixels = decode_png(png)
            retained_png = image_dir / png.name
            shutil.copyfile(png, retained_png)
            report["cases"].append({
                "name": key, "input": str(path), "input_sha256": digest(path),
                "instances": scene["instance_count"], "bonds": scene["bond_count"],
                "settings": inspected["settings"], "camera": scene["camera"],
                "command": cmd, "samples": args.samples, "elapsed_ms": elapsed,
                "cpu_ms": cpu_ms, "cpu_summary_ms": summary(cpu_ms),
                "render_counters": {k:int(v) for k,v in re.findall(
                    r"(adaptive_native_tiles|adaptive_total_tiles|render_threads|raster_tiles|png_compression_resolved|overlay_labels|overlay_labels_skipped)=([0-9]+)",proc.stderr)},
                "latency_ms": summary(elapsed), "stages_ms": stages,
                "stage_summary_ms": {k: summary(v) for k, v in stages.items()},
                "rss_kib_separate_run": rss, "png_bytes": png.stat().st_size,
                "user_cycles_separate_runs": cycle_samples,
                "png": str(retained_png), "png_sha256": digest(png),
                "decoded_rgba_sha256": hashlib.sha256(pixels).hexdigest(),
                "width": width, "height": height,
            })
            save()
            print(f"{key}: {statistics.median(elapsed):.2f} ms median", flush=True)
    report["finished_utc"] = datetime.now(timezone.utc).isoformat()
    report["load_after"] = os.getloadavg()
    save()


if __name__ == "__main__":
    main()
