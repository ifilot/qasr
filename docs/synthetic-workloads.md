# Synthetic data for M10 and M11

`tools/generate_m10_workloads.py` creates original, seeded POSCAR and minimal
vasprun.xml inputs, geometry oracles and request plans using only the Python
standard library. No external dataset, scientific Python package or VASP license
is needed. These are artificial rendering/parser workloads, not physically
validated structures or results of a DFT/MD calculation. They complement the
retained real fixtures; they do not establish real-world performance by themselves.

## Generate and use

Run from the repository root with the M9-capable executable on master:

```sh
python3 tools/generate_m10_workloads.py \
  --profile standard --seed 1729 --output artifacts/m10/data

# Check file hashes, species/order, cells, frame counts and Cartesian positions.
python3 tools/run_m10_workloads.py build-release/vasp-render \
  artifacts/m10/data --validate-only

# Establish a baseline using independent CLI calls, before implementing a worker.
python3 tools/run_m10_workloads.py build-release/vasp-render \
  artifacts/m10/data --output artifacts/m10/baseline \
  --samples 30 --warmups 3 --measure-rss
```

Both tools refuse existing output directories. Choose a new path for each dataset
or measurement; there is no force/overwrite option. Data generation and oracle
verification occur outside the timing interval. Treat measurements as warm-cache
runs: neither script flushes OS caches. Use a quiet machine and record storage
placement; native WSL storage and `/mnt/d` can have different I/O costs.

The generated data is reproducible for the same generator version/source, seed
and parameters. The manifest has no timestamps or absolute paths. SHA-256 file
hashes and a generator-source hash identify the exact corpus. The generator uses
an integer hash for jitter and an integer triangular motion pattern, avoiding a
random-library or trigonometric dependency. The XML writer streams frames and
retains only one geometry/frame in memory.

| Profile | Main atom count | XML frames | Independent structures | View/style variants each | Planned requests | Approximate total size, seed 1729 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| smoke | 32 | 4 | 4 | 4 | 29 | 90 KB |
| standard | 512 | 128 | 12 | 12 | 53 | 4.7 MB |
| stress | 4096 | 512 | 24 | 24 | 89 | 123 MB |

Sizes use decimal units. Counts include negative cases; XML-specific requests
are skipped explicitly for XML-disabled builds. Fourteen, 22 and 34 input files
are generated respectively, including paired POSCAR frame snapshots and one
intentionally malformed POSCAR. XML contains an initial structure plus the stated
number of calculation frames; there is no extra finalpos geometry.

Override `--atoms`, `--frames`, `--structures`, `--variants` and `--image-size`
to isolate scaling effects. Atom counts are 4..10000, frames 2..4096,
structures/variants 2..128, dimensions 64..2048, seed 0..2^32−1. A conservative
512 MiB estimate rejects oversized combinations before creating the dataset.
Renderer geometry/work limits still apply: a permitted generated input can be
expensive or exceed rendering budgets under aggressive settings. Large profiles
are deliberately optional; the CTest entry uses smoke data.

## Contents and intended experiments

- `inputs/POSCAR.bulk`: alternating toy Si/O lattice with small deterministic
  jitter. The spacing exercises ordinary bond construction.
- `inputs/POSCAR.skew`: a sheared Si/O cell with the same source-order discipline.
- `inputs/POSCAR.slab`: a Co slab with xy periodicity and explicit z vacuum.
- `inputs/POSCAR.boundary`: two H atoms outside opposite primary-cell faces,
  exercising wrapping and short periodic bonds. Source Cartesian x coordinates
  are −0.1 and 4.1 Å in a 4 Å cell.
- `inputs/vasprun.synthetic.xml`: a complete geometry-only trajectory with known,
  deterministic bounded displacements, correct atom/type metadata and stable
  source indices. It is suitable for this renderer, not a full VASP output emulator.
- `oracles/`: full element/order, lattice and Cartesian positions for static
  inputs and first/middle/final trajectory frames. Matching POSCAR snapshots allow
  cross-format pixel checks. The runner checks parser results against these
  values with an absolute 1e−9 Å tolerance before measuring.
- `manifest.json`: parameters, provenance, file sizes/hashes and plan inventory.

The seven workload families are:

| Family | What it exercises |
| --- | --- |
| independent | Different structures, sizes, species and cells; process scheduling |
| views | Repeated views of one input; parsed-input/scene reuse opportunities |
| styles | Repeated styles, backgrounds, radii and source-atom colors; cache-key correctness |
| frames | First/middle/final frames, returning to frame 0, an invalid frame, then recovery |
| periodic | Centered copies, muted/greyscale colors and periodic boundary geometry |
| isolation | A baseline, changed dimensions/style/repeat/cell settings, malformed input, invalid selector, then the original baseline |
| invalidation | Original → changed → restored geometry at one private input path, retaining its inode, byte count and mtime |

For invalidation, only the runner-owned copy is overwritten. The source corpus
is immutable. Replacement changes the cell and coordinates while preserving text
length; the runner verifies that its pixels change and then return exactly to the
original pixels. The report records size, inode and nanosecond mtime so that a
future cache cannot accidentally pass by relying only on those identity fields.
This deliberately tests content-sensitive invalidation; it does not claim to
solve arbitrary concurrent input modification.

## Request plans and the baseline runner

`plans/*.jsonl` contain **driver envelopes**, not a currently supported renderer
batch protocol. Each line has `id`, `action`, `requires`, an ordinary M9 `job` and
`expected` assertions. Paths are relative to the suite root. The runner resolves
paths and submits each job using the existing `vasp-render --job -` interface.
The optional `active_input` field requests the private input-replacement scenario.
A future M10 worker adapter can reuse these envelopes and expected outcomes.

The runner supports `--backend cli` (default, independent processes) and
`--backend worker` (one persistent M10 session). Use `--reference` to compare
worker results with the CLI, and `--cache-mib 0` to isolate persistence from
caching. The worker tests additionally check bounded retention, cancellation
and state restoration. See [the worker interface](batch-worker.md). Hard-coded expected
pixel hashes are avoided because compiler/library/host changes may affect them;
a retained run supplies exact comparison hashes in a controlled environment.

Each measured request checks:

- Expected exit status and structured errors. Intentional failures must preserve
  a sentinel output file, and subsequent requests must succeed.
- Input/output SHA-256, selected frame, atom count and image dimensions.
- Repeatable decoded RGBA, plus restoration/difference assertions in the plans.
- With `--reference`, matching request/input/error/count/pixel results before
  reporting latency ratios. A failed or different-corpus reference is rejected.

For a selected experiment and optional thread comparison:

```sh
python3 tools/run_m10_workloads.py build-release/vasp-render \
  artifacts/m10/data --families views styles \
  --output artifacts/m10/one-thread --samples 30 --warmups 3 --threads 1

python3 tools/run_m10_workloads.py build-release/vasp-render \
  artifacts/m10/data --families views styles \
  --output artifacts/m10/auto-threads --samples 30 --warmups 3 --threads auto \
  --reference artifacts/m10/one-thread/report.json
```

Defaults are three measured samples and one warmup per request, with one render
thread from each generated job. `--threads` overrides that setting. Reports
checkpoint after every request and become `passed: true` only when the complete
selected plan succeeds. Timing includes process startup, input hashing, rendering,
PNG output and success-report generation; oracle checks and PNG decoding are
outside the timed region. Median/p95 and raw wall/CPU/stage samples are retained.
CPU accounting assumes sequential children without unrelated concurrent child
processes. `--measure-rss` uses one separate GNU `/usr/bin/time` invocation per
request, reporting peak RSS in KiB; it is not a steady-state worker-memory measure.
There are no speedup confidence or cold-cache claims.

## Verification and next stages

Seven automated tests cover reproducibility/seed changes, destination and size
bounds, geometry oracles, independent XML/POSCAR parsing, corpus tampering/path
confinement, all workload families, unchanged failure outputs, same-identity
replacement and reference-comparison rejection. The same suite is exercised with
Release, sanitizer and XML/OpenMP/libdeflate-disabled renderer builds.

M10 implements the persistent worker and reference comparisons. M11 is recorded in [the detailed design](next-iteration.md#m11--review-openmp-opportunities-and-thread-policy):
profile candidates for OpenMP, sweep thread counts/scheduling and worker/thread
combinations, and retain improvements only when measurements justify them.
