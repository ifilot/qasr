# M10/M11 implementation and validation — 2026-10-29

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../README.md)
> for supported behavior and setup.

M10 delivers a sequential JSONL worker with a shared CLI rendering path,
content-sensitive bounded input/scene reuse, strict request schemas, structured
responses, cache controls and cooperative cancellation. See
[batch-worker.md](../batch-worker.md) and [synthetic-workloads.md](../synthetic-workloads.md).
MCP/HTTP remain optional integration layers; no service dependency was added.

M11 investigated OpenMP opportunities using both synthetic and retained real
structures. It retains parallel buffer initialization, primitive projection and
premultiplied-AA resolution when enough work is available. Each iteration owns
its output, so no floating-point reduction or shared primitive append is needed.
Raster tiles retain deterministic primitive ordering and dynamic scheduling.
Raster cancellation is checked once per tile with a lock-free atomic flag.
The normal CLI and persistent worker both benefit.

## Threading decisions

- Initialize sample buffers in parallel for at least 1,048,576 samples.
- Project primitives in parallel for at least 4096 primitives.
- Resolve antialiasing in parallel for at least 262,144 output pixels.
- Keep the existing auto policy: one thread below 512² pixels, otherwise up to
  eight, respecting runtime availability. Explicit thread settings still work.
- Preserve the OpenMP-disabled path and ordinary output. Reports now include
  actual `render_threads`, in addition to requested thread settings.

Parsing and SHA-256 hashing remain sequential. The input parser is a streaming
state machine, while standard SHA-256 has chained state; adding naive OpenMP
loops would not preserve their semantics. Selector rules have later-rule
precedence, scene/bond construction has ordered append and shared work/allocation
budgets, and PNG encoding uses stateful library APIs. These were reviewed but
not parallelized in this iteration. More involved redesigns need their own
measurements; the repeated-input cache already avoids much scene work.

Static and guided tile scheduling were tested in prototype builds. Static was
substantially slower on the AA-heavy workload; guided gave mixed results across
cases/thread counts. Neither replaced the existing dynamic policy. Prototype
source and binaries, compile flags, executable hashes and raw results are retained
under `artifacts/m11/` and the native temporary build directories.

## M10 repeated-request measurements

The standard native-WSL comparison uses the same corpus, binary, filesystem and
one render thread in both backends: five measured repeats per request after two
warmups. All 53 requests pass independent correctness/reference checks. The
stress comparison uses nine frame/invalidation requests from the 4096-atom,
512-frame corpus (109.5 MB XML), five repeats after one warmup.

| Workload family | Median worker/CLI latency ratio |
| --- | ---: |
| Standard independent structures | 0.883 |
| Standard repeated views | 0.881 |
| Standard style recipes | 0.930 |
| Standard trajectory frames | 0.599 |
| Stress trajectory frames | 0.220 |
| Stress input replacement | 0.435 |

Ratios summarize successful request medians; expected failures are excluded.
These are warm repeated-request results. Per-recipe warmups populate caches,
so initial parsing, new-style scene rebuilds and first-miss double hashing are
not represented in those medians. They do not promise the same gain for a
single pass over distinct files/styles. The single-slot cache can miss when
alternating frames. Native input/output placement is recorded in every report;
a preliminary cross-filesystem run is retained only as a correctness check.

Whole-worker GNU-time peak RSS was 25,240 KiB for standard and 42,620 KiB for
stress. This includes transient rendering/encoding and allocator/runtime memory,
not just retained cache bytes. CLI memory measurements are separate per-request
runs and are not the same metric. Raw samples, hashes and cache metadata are
in `artifacts/m10/native-*/report.json`. The measurements predate small defensive
allocation-failure checks; final correctness checks cover the delivered source.

## M11 latency and CPU evidence

Intel i9-10900K, 20 logical CPUs, WSL; native temporary output, fixed libpng level 1,
warm cache. Four workloads were swept at 1/2/4/8/16 threads with eight samples and
two warmups for the baseline, retained setup changes, static and guided scheduling.
Every measured PNG had to equal the corresponding reference bytes.
A separate confirmation used 30 samples after three warmups at eight threads.

| Case | Before median wall, ms | After median wall, ms | Change | Before median CPU, ms | After median CPU, ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Dense synthetic, 1000 atoms, 512² 2× AA | 62.67 | 50.59 | −19.3% | 140.61 | 136.01 |
| Real Co slab, 768² 4× AA | 240.17 | 177.41 | −26.1% | 1138.10 | 1154.70 |
| Real Pd/ceria with annotations, 768² 2× AA | 57.35 | 44.76 | −22.0% | 83.18 | 109.74 |

Small 128² jobs were also checked. Their process/thread startup variability is
large relative to the useful work, so their results do not justify raising the
automatic small-job thread count. The retained changes target sufficiently large
loops. These are combined-change measurements, not isolated attribution to each
new region. Lower latency does not imply lower CPU cost: annotations used more
CPU in the confirmation. Raw median/p95/min/max, wall/CPU samples and stage times
are in `artifacts/m11/sweep.json` and `confirmation.json`. The runs are sequential
rather than randomized, and OS cache/host scheduling are uncontrolled; no universal
speedup or formal confidence-interval claim is made.

## Worker/thread combinations

A separate native-WSL experiment rendered identical 1000-atom, 1024², 2× AA jobs
through warm persistent workers. It tested 1/2/4 workers × 1/2/4/8 render threads,
12 jobs per sample, three samples after warming every worker.

| Configuration | Median jobs/s | Idle sum of worker RSS, KiB |
| --- | ---: | ---: |
| 1 worker × 8 threads | 11.76 | 27656 |
| 4 workers × 4 threads | 25.58 | 110520 |
| 4 workers × 8 threads | 22.37 | 110652 |

Four workers × four threads improved throughput for this workload at a higher
aggregate memory footprint. Raising the per-worker count to eight lowered
throughput. RSS values are idle snapshots, not aggregate peak measurements.
The raw pool report includes CPU time from `/proc` ticks and exact PNG hashes.
These results motivate explicit per-job thread counts when using a pool; they do
not justify imposing one global policy on all machines or workloads.

## Correctness, resources and deployment

The synthetic baseline covers independent structures, repeated views/styles,
frame changes/errors, periodic contexts, state restoration and same-identity
content replacement. Cache retention is bounded even over repeated requests;
small/disabled caches, clear/validate operations and malformed/oversized input
are tested. Cancellation tests preserve an existing destination, receive a
`cancelled` response and successfully reuse the worker. Large transparent/adaptive
renders compare one/eight-thread PNGs to cover the newly parallel regions.

A separate 1200-request, changing-style/context soak held cache retention below
1 MiB. After the first 200 requests, idle RSS samples spanned only 8 KiB on this
run; this is an observed plateau, not a universal RSS bound.

All 12 Release, 14 all-feature address/undefined-behavior/leak and 12
reduced-dependency CTest entries passed. All 20 retained default reference images
remain pixel-identical. Installation, binary/source packaging, fresh offline
source rebuild/tests and fallback deployment checks passed. These checks
are retained under `artifacts/m10/validation/` and `artifacts/m10/deployment/`.
No runtime dependencies beyond the existing build were added. Synthetic data and
performance timing evidence are generated local artifacts; source, tests,
schemas and documentation are versioned. WSL is the user-approved environment.
