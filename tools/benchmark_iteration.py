# SPDX-License-Identifier: GPL-3.0-only
"""Interleaved, exact-output baseline/candidate workflow comparisons (Linux/WSL)."""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import re
import resource
import select
import shutil
import statistics
import subprocess
import tempfile
import time

from benchmark import ROOT, digest, environment, summary
from generate_m10_workloads import geometry, poscar


def comparison(rows):
    """Resample complete AB/BA pairs, retaining within-pair host correlation."""
    a = [row['baseline']['wall_ms'] for row in rows]
    b = [row['candidate']['wall_ms'] for row in rows]
    rng = random.Random(1729)
    ratios = []
    for _ in range(2000):
        indices = rng.choices(range(len(rows)), k=len(rows))
        ratios.append(statistics.median(b[i] for i in indices) /
                      statistics.median(a[i] for i in indices))
    ratios.sort()
    return {'baseline_ms': summary(a), 'candidate_ms': summary(b),
            'candidate_over_baseline': statistics.median(b) / statistics.median(a),
            'paired_bootstrap_ratio_95pct': [ratios[49], ratios[1949]],
            'baseline_cpu_ms': summary([r['baseline']['cpu_ms'] for r in rows]),
            'candidate_cpu_ms': summary([r['candidate']['cpu_ms'] for r in rows])}


def run(command):
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    start = time.perf_counter_ns()
    proc = subprocess.run(list(map(str, command)), check=True, capture_output=True, timeout=300)
    wall = (time.perf_counter_ns() - start) / 1e6
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return proc, {'wall_ms': wall, 'cpu_ms': 1000 * (
        after.ru_utime + after.ru_stime - usage.ru_utime - usage.ru_stime),
        'stages_ms': {k.decode(): float(v) for k, v in re.findall(
            rb'(config|parse|scene|render|png|encode)=([0-9.]+)', proc.stderr)}}


def cli_cases(work):
    dense = work / 'POSCAR.dense'
    dense.write_text(poscar(geometry(1000, 'slab', 1729)))
    tiny = work / 'POSCAR.tiny'
    tiny.write_text(poscar(geometry(32, 'bulk', 1729)))
    large = work / 'POSCAR.4096'
    large.write_text(poscar(geometry(4096, 'slab', 1729)))
    fixture = ROOT / 'tests/fixtures'
    co = fixture / 'slabs/01_co0001_clean/CONTCAR'
    pd = fixture / 'slabs/18_pd_ceria_co_oxidation_ts/CONTCAR'
    renders = [
        ('tiny', tiny, ['--width', '128', '--height', '128']),
        ('dense', dense, ['--width', '512', '--height', '512']),
        ('dense4096', large, ['--width', '512', '--height', '512']),
        ('aa-heavy', co, ['--view', 'top', '--width', '768', '--height', '768', '--aa', '4x']),
        ('annotated', pd, ['--width', '768', '--height', '768', '--labels', 'element', '--legend', 'on', '--axes', 'on']),
        ('repeat', co, ['--repeat', '2,2,2', '--style', 'illustrated']),
    ]
    for style, tone in [('illustrated', 'color'), ('ortep', 'color'), ('ortep', 'dualtone'),
                        ('balsac', 'color'), ('balsac', 'dualtone')]:
        renders.append((style + '-' + tone, co, ['--view', 'top', '--width', '768',
                        '--height', '768', '--style', style, '--tone', tone]))
    alpha = work / 'alpha.json'
    alpha.write_text(json.dumps({'elements': {'Co': {'color': '#b24a807f'}},
                                 'image': {'background': '#ffffff00'}}))
    renders.append(('alpha-adaptive', co, ['--aa', 'adaptive', '--config', str(alpha)]))
    cases = []
    for name, source, flags in renders:
        for threads in [1, 8]:
            cases.append((f'{name}-t{threads}', source, [*flags, '--threads', str(threads)], 'png'))
    # Explicit identical settings for the unchanged codec controls too.
    for encoder in ['libpng', 'libdeflate']:
        cases.append((encoder, pd, ['--threads', '1', '--png-encoder', encoder], 'png'))
    for suffix in ['jpg', 'webp']:
        cases.append((suffix, pd, ['--threads', '1'], suffix))
    for name in ['OUTCAR.vasp5.gz', 'vasprun.relax.xml.gz']:
        source = work / name[:-3]
        source.write_bytes(gzip.decompress((fixture / name).read_bytes()))
        for frame in ['0', 'final']:
            cases.append((name + '-' + frame, source, ['--inspect', '--frame', frame], None))
    xyz = work / 'large.extxyz'
    xyz.write_text('100000\nProperties=species:S:1:pos:R:3:charge:R:1:fixed:L:1\n' +
                   ''.join(f'C {i % 100} {i // 100 % 100} {i // 10000} 0.25 F\n' for i in range(100000)))
    cases.append(('extxyz-100k', xyz, ['--validate-config'], None))
    return cases


def measure_cli(executables, cases, args, work, report, save):
    for name, source, flags, suffix in cases:
        output = work / ('output.' + (suffix or 'json'))
        commands = {label: [exe, source, '--no-user-config', '--benchmark-stages',
                           *([] if suffix is None else ['-o', output]), *flags]
                    for label, exe in executables.items()}
        expected = None

        def sample(label):
            nonlocal expected
            proc, record = run(commands[label])
            data = output.read_bytes() if suffix else proc.stdout
            sha = hashlib.sha256(data).hexdigest()
            if expected is None:
                expected = sha
                if suffix:
                    (args.output.parent / f'{name}.{suffix}').write_bytes(data)
            if sha != expected:
                raise ValueError(f'{name}: {label} output differs from reference')
            record.update(output_sha256=sha, output_bytes=len(data))
            return record

        for _ in range(args.warmups):
            for label in executables:
                sample(label)
        rows = []
        # Balanced AB/BA ordering; seed and actual order retained in report.
        orders = [list(executables)[::(-1 if i % 2 else 1)] for i in range(args.samples)]
        random.Random(args.seed).shuffle(orders)
        for order in orders:
            row = {'order': order}
            for label in order:
                row[label] = sample(label)
            rows.append(row)
        entry = {'name': name, 'input_sha256': digest(source),
                 'commands': {k: list(map(str, v)) for k, v in commands.items()},
                 'samples': rows, 'comparison': comparison(rows), 'identical_output': True}
        if suffix:
            scenes = [run([exe, source, '--no-user-config', *flags, '--inspect-scene'])[0].stdout
                      for exe in executables.values()]
            if scenes[0] != scenes[1]:
                raise ValueError(f'{name}: scene inspection differs')
            entry['scene_sha256'] = hashlib.sha256(scenes[0]).hexdigest()
        elif '--validate-config' in flags:
            inspections = [run([arg if arg != '--validate-config' else '--inspect'
                                for arg in command])[0].stdout for command in commands.values()]
            if inspections[0] != inspections[1]:
                raise ValueError(f'{name}: validated input inspection differs')
            entry['inspection_sha256'] = hashlib.sha256(inspections[0]).hexdigest()
        for label, command in commands.items():
            rss = work / 'rss.txt'
            subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(rss),
                            *map(str, command)], check=True, capture_output=True, timeout=300)
            entry.setdefault('rss_kib_separate_run', {})[label] = int(rss.read_text())
        report['cases'].append(entry)
        save()
        print(name, round(entry['comparison']['candidate_over_baseline'], 3), flush=True)


def measure_filters(executables, args, work, report, save):
    # A real rendered RGBA image repeated to 2048² keeps realistic flat regions,
    # alpha edges and gradients without charging rendering time to the encoder.
    from png_support import decode_png
    image = work / 'codec.png'
    run([executables['baseline'], ROOT / 'tests/fixtures/slabs/18_pd_ceria_co_oxidation_ts/CONTCAR',
         '--no-user-config', '--threads', '1', '-o', image])
    w, h, pixels = decode_png(image)
    raw = work / 'codec.rgba'
    if 2048 % w or 2048 % h:
        raise ValueError('codec reference dimensions must divide 2048')
    raw.write_bytes(b''.join((pixels[y*w*4:(y+1)*w*4] * (2048 // w)) for y in range(h)) * (2048 // h))
    for mode in ['none', 'sub', 'up', 'adaptive']:
        for level in [0, 1]:
            rows, expected = [], None
            commands = {label: [exe.parent / 'png-encode-bench', '2048', '2048', raw,
                        work / 'encoded.png', 'libdeflate', str(level), mode, '2']
                        for label, exe in executables.items()}
            for i in range(args.samples):
                order = list(executables)[::(-1 if i % 2 else 1)]
                row = {'order': order}
                for label in order:
                    proc, record = run(commands[label])
                    timings = json.loads(proc.stdout)
                    record['wall_ms'] = statistics.median(timings['encode_ms'])
                    record['cpu_ms'] = statistics.median(timings['cpu_ms'])
                    sha = digest(work / 'encoded.png')
                    if expected is None:
                        expected = sha
                    if sha != expected:
                        raise ValueError('PNG filtering changed encoded bytes')
                    record['output_sha256'] = sha
                    record['encode_samples_ms'] = timings['encode_ms']
                    row[label] = record
                rows.append(row)
            entry = {'name': f'filter-{mode}-{level}', 'samples': rows,
                     'commands': {k: list(map(str, v)) for k, v in commands.items()},
                     'input_sha256': digest(raw), 'identical_output': True,
                     'comparison': comparison(rows)}
            report['cases'].append(entry)
            save()
            print(entry['name'], round(entry['comparison']['candidate_over_baseline'], 3), flush=True)


def measure_workers(executables, args, work, report, save):
    source = work / 'POSCAR.worker'
    source.write_text(poscar(geometry(1000, 'slab', 1729)))
    xml = work / 'worker.xml'
    xml.write_bytes(gzip.decompress((ROOT / 'tests/fixtures/vasprun.relax.xml.gz').read_bytes()))
    extxyz = next(c[1] for c in cli_cases(work) if c[0] == 'extxyz-100k')

    def job(path=source):
        return {'input': str(path), 'output': str(work / 'worker.png'), 'threads': 1,
                'config': {'image': {'width': 512, 'height': 512}}}

    for name in report['expected_cases']:
        workers, logs = {}, []
        try:
            for label, exe in executables.items():
                log = (work / f'{label}-worker.log').open('w')
                logs.append(log)
                workers[label] = subprocess.Popen([str(exe), '--batch'], stdin=subprocess.PIPE,
                                                  stdout=subprocess.PIPE, stderr=log, text=True)

            def request(label, op, payload=None):
                p = workers[label]
                fields = Path(f'/proc/{p.pid}/stat').read_text().rsplit(')', 1)[1].split()
                ticks = int(fields[11]) + int(fields[12])
                start = time.perf_counter_ns()
                message = {'id': 'benchmark', 'op': op}
                if payload is not None:
                    message['job'] = payload
                p.stdin.write(json.dumps(message) + '\n')
                p.stdin.flush()
                # Worker operations have the same 300-second bound as CLI runs.
                if not select.select([p.stdout], [], [], 300)[0]:
                    raise TimeoutError('worker did not respond')
                line = p.stdout.readline()
                wall = (time.perf_counter_ns() - start) / 1e6
                reply = json.loads(line)
                if not reply['ok']:
                    raise ValueError(reply)
                fields = Path(f'/proc/{p.pid}/stat').read_text().rsplit(')', 1)[1].split()
                cpu = (int(fields[11]) + int(fields[12]) - ticks) * 1000 / os.sysconf('SC_CLK_TCK')
                result = reply['result']
                record = {'wall_ms': wall, 'cpu_ms': cpu,
                          'stages_ms': result.pop('stages_ms', {})}
                return result, record

            expected = {}

            def sample(label, index):
                base = job(xml if name == 'worker-xml-frames' else source)
                if name == 'worker-extxyz-validate':
                    base = job(extxyz)
                    op = 'validate'
                else:
                    op = 'render'
                    # Preparation lies outside the timed request. This distinguishes
                    # scene/source hits, misses, and changing settings explicitly.
                    if name in ['worker-miss', 'worker-xml-frames']:
                        request(label, 'clear_cache')
                    request(label, 'render', base)
                    if name == 'worker-miss':
                        request(label, 'clear_cache')
                    elif name == 'worker-camera':
                        base['config']['camera'] = {'view': 'top'}
                    elif name == 'worker-radii':
                        base['config']['geometry'] = {'sphere_scale': 0.9}
                    elif name == 'worker-style':
                        base['config']['image']['style'] = 'balsac'
                    elif name == 'worker-xml-frames':
                        base['frame'] = 0
                result, record = request(label, op, base)
                cache = result.get('cache')
                if cache:
                    hits = (cache['source_hit'], cache['scene_hit'])
                    wanted = {'worker-miss': (False, False), 'worker-warm': (True, True),
                              'worker-camera': (True, True), 'worker-style': (True, True),
                              'worker-radii': (True, False), 'worker-xml-frames': (False, False)}[name]
                    if hits != wanted:
                        raise ValueError(f'{name}: unexpected cache hits {hits}')
                    if cache['retained_bytes'] > cache['limit_bytes']:
                        raise ValueError('worker exceeded cache bound')
                sha = hashlib.sha256(json.dumps(result, sort_keys=True).encode()).hexdigest()
                if index in expected and sha != expected[index]:
                    raise ValueError(f'{name}: worker result differs across binaries')
                expected[index] = sha
                record['result_sha256'] = sha
                record['cache'] = cache
                return record

            for index in range(-args.warmups, 0):
                for label in executables:
                    sample(label, index)
            rows = []
            orders = [list(executables)[::(-1 if i % 2 else 1)] for i in range(args.samples)]
            random.Random(args.seed).shuffle(orders)
            for index, order in enumerate(orders):
                row = {'order': order}
                for label in order:
                    row[label] = sample(label, index)
                rows.append(row)
            report['cases'].append({'name': name, 'samples': rows, 'comparison': comparison(rows),
                                    'input_sha256': digest(xml if name == 'worker-xml-frames' else extxyz if name == 'worker-extxyz-validate' else source),
                                    'identical_output': True,
                                    'rss_kib_idle': {label: int(re.search(r'VmRSS:\s+(\d+)', Path(f'/proc/{p.pid}/status').read_text())[1]) for label, p in workers.items()}})
            save()
            print(name, round(report['cases'][-1]['comparison']['candidate_over_baseline'], 3), flush=True)
        finally:
            for p in workers.values():
                p.stdin.close()
                try:
                    p.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
                p.stdout.close()
            for log in logs:
                log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path)
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--warmups', type=int, default=3)
    parser.add_argument('--seed', type=int, default=1729)
    parser.add_argument('--cases', nargs='+')
    parser.add_argument('--mode', choices=['cli', 'filters', 'worker'], default='cli')
    args = parser.parse_args()
    if args.samples < 2 or args.warmups < 1:
        parser.error('at least two paired samples and one warmup required')
    executables = {'baseline': args.baseline.resolve(), 'candidate': args.candidate.resolve()}
    work = args.work_dir or Path(tempfile.mkdtemp(prefix='qasr-iteration-'))
    work.mkdir(parents=True, exist_ok=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # The raw encoder harness resolves user defaults; isolate those too.
    os.environ['XDG_CONFIG_HOME'] = str(work.resolve() / 'isolated-user-config')
    shutil.copyfile(__file__, args.output.with_suffix('.driver.py'))
    report = {'started_utc': datetime.now(timezone.utc).isoformat(), 'passed': False,
              'scope': 'warm-cache, interleaved AB/BA; CLI wall includes startup/output/exit; filter wall is in-process encoding only; no cold-cache claim',
              'samples_per_binary': args.samples, 'warmups': args.warmups, 'seed': args.seed,
              'driver_sha256': digest(Path(__file__)),
              'work_dir': str(work), 'load_before': os.getloadavg(),
              'filesystem': subprocess.check_output(['stat', '-f', '-c', '%T', work], text=True).strip(),
              'environment': {k: environment(v) for k, v in executables.items()}, 'cases': []}

    def save():
        args.output.write_text(json.dumps(report, indent=2) + '\n')

    save()
    if args.cases and args.mode != 'cli':
        parser.error('--cases is supported for CLI mode only')
    if args.mode == 'cli':
        cases = cli_cases(work)
        if args.cases:
            missing = set(args.cases) - {c[0] for c in cases}
            if missing:
                parser.error(f'unknown cases: {sorted(missing)}')
            cases = [c for c in cases if c[0] in args.cases]
        report['expected_cases'] = [c[0] for c in cases]
        save()
        measure_cli(executables, cases, args, work, report, save)
    elif args.mode == 'filters':
        report['warmups'] = '3 internal encoder warmups per process; 2 measured encodes per process'
        report['expected_cases'] = [f'filter-{m}-{n}' for m in ['none', 'sub', 'up', 'adaptive'] for n in [0, 1]]
        measure_filters(executables, args, work, report, save)
    else:
        report['scope'] = f'warm persistent workers, interleaved AB/BA; preparation outside timed request; CPU from /proc ticks ({1000 / os.sysconf("SC_CLK_TCK"):g}ms resolution); RSS idle, not peak'
        report['expected_cases'] = ['worker-miss', 'worker-warm', 'worker-camera',
                                    'worker-style', 'worker-radii', 'worker-xml-frames', 'worker-extxyz-validate']
        measure_workers(executables, args, work, report, save)
    if report['expected_cases'] != [c['name'] for c in report['cases']]:
        raise ValueError('incomplete case inventory')
    report.update(passed=True, finished_utc=datetime.now(timezone.utc).isoformat(), load_after=os.getloadavg())
    save()


if __name__ == '__main__':
    main()
