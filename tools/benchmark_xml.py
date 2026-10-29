# SPDX-License-Identifier: GPL-3.0-only
"""Warm-cache XML parse/render timing and trajectory-memory evidence."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET
from benchmark import ROOT, environment, summary, digest, decode_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/m7/benchmark.json')
    parser.add_argument('--work-dir', type=Path)
    parser.add_argument('--samples', type=int, default=30)
    args = parser.parse_args()
    if args.samples < 1: parser.error('samples must be positive')
    exe = args.executable.resolve()
    isolated = ["--no-user-config"] if json.loads(subprocess.check_output([exe,"--build-info"])).get("user_config") else []
    work = (args.work_dir or Path(tempfile.mkdtemp(prefix='qasr-xml-bench-'))).resolve()
    work.mkdir(parents=True, exist_ok=True)
    report = {'started_utc': datetime.now(timezone.utc).isoformat(), 'environment': environment(exe),
              'scope': 'Warm-cache process wall time including JSON/PNG output; parse stage excludes startup; no fsync',
              'work_dir': str(work), 'samples': args.samples, 'warmups': 3, 'cases': []}
    def measure(name, source, flags, oracle=None):
        command = [str(exe), str(source), *isolated, *map(str, flags), '--benchmark-stages']
        for _ in range(3): subprocess.run(command, capture_output=True, check=True)
        elapsed, parsing = [], []
        last = None
        for _ in range(args.samples):
            begin = time.perf_counter_ns()
            last = subprocess.run(command, capture_output=True, check=True)
            elapsed.append((time.perf_counter_ns()-begin)/1e6)
            parsing.append(float(re.search(rb' parse=([0-9.]+)', last.stderr)[1]))
        rss = work/(name+'.rss')
        subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(rss), *command], capture_output=True, check=True)
        entry = {'name': name, 'command': command, 'input_sha256': digest(source), 'input_bytes': source.stat().st_size,
                 'elapsed_ms': elapsed, 'latency_ms': summary(elapsed), 'parse_ms': parsing,
                 'parse_summary_ms': summary(parsing), 'rss_kib_separate_run': int(rss.read_text().strip())}
        if '--inspect' in flags:
            actual = json.loads(last.stdout)
            if oracle:
                assert actual['atom_count'] == oracle['atom_count'] and actual['frame_count'] == oracle['frame_count']
                assert actual['frame'] == oracle['frame']
            entry['selection'] = {k: actual[k] for k in ['atom_count', 'frame', 'frame_count']}
        else:
            png = Path(flags[flags.index('-o')+1]); w, h, pixels = decode_png(png)
            entry.update(png=str(png), png_sha256=digest(png), width=w, height=h,
                         decoded_rgba_sha256=hashlib.sha256(pixels).hexdigest())
        report['cases'].append(entry)
        print(name, 'median', round(entry['latency_ms']['median'], 2), 'ms; RSS', entry['rss_kib_separate_run'], 'KiB', flush=True)
    for fixture in ['vasprun.relax.xml.gz', 'vasprun.md.xml.gz', 'vasprun.vasp6.xml.gz']:
        source = work/fixture[:-3]; source.write_bytes(gzip.decompress((ROOT/'tests/fixtures'/fixture).read_bytes()))
        tree = ET.parse(source).getroot(); steps = tree.findall('calculation/structure')
        oracle = {'atom_count': int(tree.findtext('atominfo/atoms')), 'frame_count': len(steps)}
        for selector in ['0', 'final']:
            measure(fixture+'.'+selector, source, ['--inspect', '--frame', selector],
                    {**oracle, 'frame': 0 if selector == '0' else len(steps)-1})
        for style, size in [('shaded', 512), ('illustrated', 768)]:
            png = work/(fixture+'.'+style+'.png')
            measure(fixture+'.'+style, source, ['-o', str(png), '--style', style, '--width', str(size), '--height', str(size)])
    # Equal geometry, 100x as many frames: retained state must not grow with
    # trajectory length. Every numeric-frame read still validates to EOF.
    metadata = '<atominfo><atoms>1</atoms><types>1</types><array name="atoms"><set><rc><c>Si</c><c>1</c></rc></set></array></atominfo>\n'
    step = '<calculation><structure><crystal><varray name="basis"><v>3 0 0</v><v>0 3 0</v><v>0 0 3</v></varray></crystal><varray name="positions"><v>0 0 0</v></varray></structure></calculation>\n'
    for frames in [100, 10000]:
        source = work/f'trajectory-{frames}.xml'
        with source.open('w') as f:
            f.write('<modeling>'+metadata)
            for _ in range(frames): f.write(step)
            f.write('</modeling>\n')
        measure(f'trajectory-{frames}', source, ['--inspect', '--frame', '0'],
                {'atom_count': 1, 'frame_count': frames, 'frame': 0})
    small, large = report['cases'][-2:]
    report['bounded_state_check'] = {'small_rss_kib': small['rss_kib_separate_run'], 'large_rss_kib': large['rss_kib_separate_run'],
                                   'allowance_kib': 8192, 'passed': large['rss_kib_separate_run'] <= small['rss_kib_separate_run']+8192}
    assert report['bounded_state_check']['passed'], 'trajectory RSS unexpectedly grows with frame count'
    report['finished_utc'] = datetime.now(timezone.utc).isoformat()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__': main()
