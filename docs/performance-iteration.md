# Performance iteration — 2026-10-31

This iteration follows three independent, read-only reviews of rendering,
input/worker/codec workflows, and the benchmark evidence. It preserves rendering
settings, connectivity, depth ties, output encoding, cache invalidation and work
limits. The baseline is the clean v0.5.0 checkout at the start of the work.

## Retained changes

- Text readers acquire the stdio lock once per physical line and use
  `getc_unlocked` inside that lock. This preserves stdio EOF and seek behavior,
  including OUTCAR recovery, embedded-NUL rejection, line/byte limits and
  cancellation. It uses the project's existing POSIX platform requirement.
- Bond construction computes reciprocal-vector lengths once and reuses cutoff
  thresholds for each encountered element in an outer atom iteration. Validation
  still occurs only for visited pairs; pair order, distance calculations,
  tolerances and candidate-budget accounting stay the same.
- Rendering hoists primitive compositing/outline constants and specializes fully
  opaque output. Partial-alpha rendering retains the original expression; no
  approximate intersections or floating-point reassociation were introduced.
- Extended XYZ keeps the coordinate values already converted during typed
  property validation. Unused properties are still validated.

An experimental PNG change dispatched None/Sub/Up filtering once per row.
All eight filter/level comparisons had intervals spanning no change, so this
experiment was removed before the final build and confirmation measurements.
No PNG encoder change is retained.

Stable raster tile bins and label-visibility indexing remain follow-up candidates.
They require additional bounded allocations and a separate measurement of their
benefit. Spatial bond-search redesign and changes to SHA-256/cache identity were
outside this iteration.

## Measurement procedure

The critical comparisons use `tools/benchmark_iteration.py`: three warmups and
30 baseline/candidate pairs per scenario, balanced AB/BA ordering shuffled with
seed 1729. The paired bootstrap interval resamples complete pairs, retaining
within-pair correlation. It does not account for every source of host drift.
The driver refuses changed output and incomplete selected case inventories.
Times are warm-cache measurements; CPU frequency and Windows host activity are
uncontrolled. No cold-cache or cross-machine speed claim is made.

The workstation is an Intel Core i9-10900K under WSL, GCC 13.3.0 Release
(`-O3 -DNDEBUG`), libpng 1.6.43, libdeflate 1.19, libxml2 2.9.14, libjpeg 80 and
libwebp 1.3.2. Both measured builds enable OpenMP, XML, libdeflate, JPEG and WebP;
Cairo is disabled. Inputs generated for timing and output files use `/tmp` Linux
storage, while retained real slab inputs remain in the repository on `/mnt/d`.
Reports retain actual commands, paths, binary/input hashes, raw wall/CPU/stage
samples, output hashes and separate memory measurements.

CLI wall time includes startup, output and exit. Worker timing measures the
request/reply on an existing process, with explicitly described cache preparation
outside the timed region. Worker CPU uses `/proc` ticks and has 10 ms resolution;
worker RSS is an idle snapshot, not peak memory. Encoder-only tests run the same
2048² RGBA data through all four filters at levels 0 and 1. Each paired process
has three internal warmups and two timed encodes; its median is one sample.

The older 20-case shaded/illustrated driver also ran with 12 samples per case.
That sequential comparison establishes broad pixel parity. A 31-case interleaved
prototype comparison and eight filter experiments informed the changes; the
final 33-case interleaved comparison and final worker comparison are the primary
speed evidence.

## Final CLI results

Times below are median / p95 in milliseconds. The interval is the candidate /
baseline median ratio; an interval below 1 supports a latency improvement for
that scenario. `t1`/`t8` request one/eight render threads.

| Workflow | Baseline ms | Final ms | Median reduction | Ratio 95% interval |
| --- | ---: | ---: | ---: | ---: |
| 1000 atoms, t1 | 98.22 / 113.96 | 86.71 / 105.23 | 11.7% | 0.832–0.938 |
| 1000 atoms, t8 | 41.24 / 58.07 | 32.40 / 49.59 | 21.4% | 0.728–0.816 |
| 4096 atoms, t1 | 451.26 / 475.25 | 340.16 / 365.21 | 24.6% | 0.743–0.769 |
| 4096 atoms, t8 | 388.27 / 424.75 | 284.76 / 316.13 | 26.7% | 0.716–0.749 |
| Co slab, 4× AA, t8 | 144.37 / 152.78 | 126.04 / 140.53 | 12.7% | 0.855–0.900 |
| Pd/ceria annotations, t1 | 54.97 / 70.69 | 52.54 / 57.00 | 4.4% | 0.928–0.976 |
| Illustrated color, t1 | 250.38 / 280.32 | 213.95 / 237.14 | 14.5% | 0.840–0.869 |
| ORTEP dualtone, t1 | 317.11 / 326.43 | 251.62 / 265.42 | 20.7% | 0.786–0.805 |
| BALSAC dualtone, t1 | 353.18 / 378.10 | 285.24 / 304.26 | 19.2% | 0.792–0.817 |
| OUTCAR final inspection, 6 MB | 25.82 / 30.69 | 18.47 / 23.11 | 28.5% | 0.697–0.731 |
| Extended XYZ validation, 100k atoms | 43.39 / 50.12 | 31.20 / 36.40 | 28.1% | 0.710–0.733 |
| XML first-frame inspection | 82.43 / 97.29 | 82.49 / 125.75 | −0.1% | 0.965–1.012 |
| Partial alpha, adaptive AA, t8 | 13.97 / 16.46 | 13.90 / 15.50 | 0.5% | 0.942–1.028 |

All 33 scenarios passed exact output checks; all render scenarios also passed
exact scene-inspection JSON checks. No scenario's median-ratio interval was
entirely above 1. XML and partial-alpha medians are essentially unchanged; the
XML first-frame p95 varied substantially, so no tail-latency improvement is
claimed there. ORTEP/BALSAC color variants reduced median latency by 9–11%.
Full codec workflows reduced latency by about 5% for PNG/JPEG; WebP remained
encoding-dominated. These include faster upstream rendering, not faster codecs.

The 4096-atom, eight-thread scene stage fell from 367.44 to 265.50 ms (27.7%).
The search still enumerates atom pairs and retains the same resource limits.
Separate peak-RSS runs showed no material growth: the largest upward difference
across the CLI matrix was 388 KiB. No new heap scratch buffer or cache is added.

## Persistent worker and filesystem checks

The worker stays running. A miss clears its cache before the measured request;
warm jobs reuse source and scene. Camera and style changes reuse the scene;
radius changes rebuild it. XML alternates final/first frame selection. Cache
hit/miss expectations, retention bounds and complete results (excluding timing)
are checked across binaries for every request.

| Worker workflow | Baseline median / p95 ms | Final median / p95 ms | Median reduction | Ratio 95% interval |
| --- | ---: | ---: | ---: | ---: |
| Cache miss | 88.87 / 97.77 | 75.37 / 106.34 | 15.2% | 0.825–0.865 |
| Warm repeated render | 64.06 / 72.11 | 57.58 / 62.17 | 10.1% | 0.888–0.907 |
| Camera change | 84.45 / 92.78 | 69.67 / 75.19 | 17.5% | 0.814–0.837 |
| Style change | 86.19 / 90.64 | 77.60 / 81.26 | 10.0% | 0.892–0.913 |
| Radius change | 81.36 / 87.24 | 69.60 / 73.88 | 14.4% | 0.851–0.868 |
| XML frame change | 198.37 / 224.16 | 195.69 / 205.49 | 1.4% | 0.971–0.999 |
| Extended XYZ validation | 38.84 / 46.01 | 26.96 / 28.07 | 30.6% | 0.689–0.698 |

The worker cache-miss p95 increased despite the lower median; do not interpret
the median gain as a tail-latency guarantee. XML gains are small and were absent
in the prototype comparison; no meaningful XML optimization is claimed.

Three final CLI cases were repeated with generated inputs and outputs on
`/mnt/d` (9p), with the same 30-pair procedure:

| Workflow | Baseline median ms | Final median ms | Median reduction |
| --- | ---: | ---: | ---: |
| PNG render | 39.94 | 38.16 | 4.4% |
| OUTCAR final inspection | 225.38 | 218.91 | 2.9% |
| Extended XYZ validation | 99.73 | 87.08 | 12.7% |

Filesystem overhead limits the parser gains here. Linux-storage percentages
should not be applied to Windows-storage workflows. All three comparisons
passed exact output/inspection checks.

## Reproduction and retained evidence

Correctness checks passed on the final source:

- Release with JPEG/WebP: **16/16 CTest entries**.
- Release without OpenMP, XML or libdeflate: **16/16 entries**, including expected
  disabled-capability skips.
- RelWithDebInfo with address/undefined-behavior sanitizers, leak detection,
  OpenMP, XML, libdeflate, Cairo, JPEG and WebP: **18/18 entries**.
- New regressions cover exact physical-line limits and unterminated EOF, NULs,
  automatic detection with long titles, reordered Extended XYZ coordinates and
  invalid unused properties, paired resampling, and rejection of changed
  benchmark output.

The suites also exercise independent ray/pixel references, depth ties, alpha,
periodic bonds, candidate/allocation limits, worker cancellation and reuse,
same-identity input replacement, and preservation of outputs after failures.
The sandbox initially prevented the XML test's localhost server; the unchanged
baseline and final suite passed when run with local binding permitted. The
sanitizer suite ran outside sandbox tracing with `ASAN_OPTIONS=detect_leaks=1`
and `UBSAN_OPTIONS=halt_on_error=1`.

Keep an unmodified baseline binary before editing. Build both versions with:

```sh
cmake -S . -B /tmp/qasr-perf-final -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DENABLE_JPEG=ON -DENABLE_WEBP=ON
cmake --build /tmp/qasr-perf-final --parallel 8
ctest --test-dir /tmp/qasr-perf-final --output-on-failure -j 2

python3 tools/benchmark_iteration.py \
  /tmp/qasr-perf-baseline/qasr /tmp/qasr-perf-final/qasr \
  --output artifacts/perf-iteration/final.json --samples 30 --warmups 3

python3 tools/benchmark_iteration.py \
  /tmp/qasr-perf-baseline/qasr /tmp/qasr-perf-final/qasr --mode worker \
  --output artifacts/perf-iteration/final-worker.json --samples 30 --warmups 3

# Reproduce the rejected filter experiment with the prototype binary:
python3 tools/benchmark_iteration.py \
  /tmp/qasr-perf-baseline/qasr /tmp/qasr-perf-candidate/qasr --mode filters \
  --output artifacts/perf-iteration/filters.json --samples 30 --warmups 3
```

Raw reports and retained outputs live under the Git-ignored
`artifacts/perf-iteration/`. The tool also accepts `--cases` to select CLI cases
and `--work-dir` to repeat measurements on another filesystem. The final driver
includes the 4096-atom stress cases added after the initial 31-case prototype
matrix.

The artifact directory retains baseline/prototype/final binaries, build caches,
the clean baseline source archive, the final tracked patch and new source files,
driver snapshots, and test logs. `provenance.json` identifies the baseline commit
and measured binary hashes. The baseline commit is
`b8051d33ac05b60dcf4a022cbe7ce84c3c33b74f`.

The final binary is retained as `artifacts/perf-iteration/final/qasr` and the
fresh build as `/tmp/qasr-perf-final/qasr`. Reports `final.json`,
`final-worker.json` and `windows.json` describe that binary. `interleaved.json`,
`worker.json` and `filters.json` describe the earlier prototype; the rejected
filter patch is retained separately for reproducing that experiment.
