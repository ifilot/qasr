# Performance work after v0.1.0

For the current optimization results, see the
[2026-10-31 performance iteration](performance-iteration.md).

Historical measurements below use the provisional executable name `vasp-render`.
The current application is QASR: Quick Atomic Structure Renderer; use `qasr`
for new builds. Retained commands and measurements are preserved as recorded.


v0.1.0 is the reference-renderer checkpoint before workstation optimization.
M4 compares renderer/anti-aliasing strategies, M5 compares PNG encoding and
threading, and M6 validates deployment/performance alongside overlays and
production hardening. Execute performance selection on a representative
workstation. On 2026-10-29 the user approved this WSL workstation for all
testing, performance selection and deployment acceptance. Native Linux/HPC
measurements are not required to complete M4–M6. Historical references to that
requirement in the M0–M3 records describe the earlier plan.

## Historical development observations

An earlier shaded-renderer build was measured on an Intel Core Ultra 7 258V
laptop under WSL2 Linux x86-64, using a v9fs filesystem, GCC 13.3.0 Release,
libpng 1.6.43 and zlib 1.3. Each case used 20 warm-cache process-lifetime
samples after three warmups, one thread, 512×512 RGBA, fixed 2× supersampling
and PNG compression 1. CPU frequency and other host activity were uncontrolled.

| Case | Median (ms) | p95 (ms) |
| --- | ---: | ---: |
| Synthetic Co, 100 atoms | 106.50 | 118.77 |
| Synthetic Co, 500 atoms | 149.80 | 191.52 |
| Synthetic Co, 1,000 atoms | 175.80 | 186.61 |
| Co slab repeated 2×2×2, 504 atoms | 75.96 | 81.00 |
| Pd/ceria, 249 atoms | 45.00 | 49.12 |
| Static OUTCAR, 2 atoms | 43.94 | 67.60 |
| Relaxation OUTCAR, 6.2 MB / 7 atoms | 266.46 | 289.33 |

These observations predate illustrated contours and adjustable cell widths.
They are not measurements of v0.1.0, release acceptance thresholds or a basis
for selecting a workstation backend. Raw samples/generated images remain local
Git-ignored artifacts. The large OUTCAR suggested parsing as a separate
bottleneck; the 1,000-atom case suggested rasterization and encoding work.

## Workstation procedure

1. Build and run the full correctness suite with the workstation compiler/libs.
2. Characterize CPU, governor, OS, compiler flags, shared-library versions,
   filesystem and memory; record input and executable hashes.
3. Establish a fresh baseline for both shaded and illustrated top views,
   including contours/cell boundaries and representative slabs/repetitions.
4. Compare algorithms in stages, retaining images and rejecting incorrect
   occlusion, changed connectivity, alpha halos or unacceptable silhouettes.
5. Measure complete process latency and isolated stages, reporting median,
   p95, samples, RSS and PNG bytes. Separate warm cache from genuinely
   controlled cold-cache tests.
6. Set workstation latency targets from that baseline. Earlier laptop targets
   in historical artifacts are exploratory and do not govern the next phase.

`tools/benchmark.py` supplies the existing warm-cache cases and raw report;
extend it for illustrated presets and additional workstation comparisons.
`tools/render_examples.py` reproduces the visual slab collection. All generated
results belong under `artifacts/`, which remains excluded from Git/packages.

## Current WSL workstation baseline — 2026-10-29

This is the fresh v0.1.0 baseline, measured before rendering changes. The
accepted host is an Intel Core i9-10900K (10 cores / 20 logical CPUs), 62 GiB
WSL memory, GCC 13.3.0 Release (`-O3 -DNDEBUG`), libpng 1.6.43 and zlib 1.3.
The CPU governor is unavailable through this WSL guest. Host frequency and
other Windows activity are uncontrolled; repeated measurements describe this
workstation environment. Shaded runs use the 512×512 defaults; illustrated
runs use the complete 768×768 top-view preset, including contours and 3-pixel
cell boundaries. Compare candidates within each style, not across resolutions.

Each case has three warmups and 30 measured process launches. Inputs, the
executable and timed PNG writes reside on Linux storage under `/tmp`; the
JSON reports and retained PNG copies reside under Git-ignored `artifacts/`.
RSS is sampled in a separate run. All stage samples, process samples, hashes,
commands, settings, cameras and linked libraries are retained in
`artifacts/m4-m6/reference/linux-baseline.json`. The original executable,
source archive and correctness/sanitizer logs are retained in the same folder.

| Case | Shaded median / p95 (ms) | Illustrated median / p95 (ms) |
| --- | ---: | ---: |
| Co-100 | 159.67 / 166.45 | 264.30 / 309.60 |
| Co-500 | 221.52 / 230.95 | 475.77 / 498.20 |
| Co-1000 | 289.05 / 302.51 | 613.31 / 627.53 |
| Co-slab | 66.38 / 67.85 | 492.28 / 506.39 |
| Co-slab-repeat-2x2x1 | 139.19 / 145.22 | 491.18 / 508.83 |
| Co-slab-repeat-2x2x2 | 111.89 / 118.83 | 813.49 / 838.28 |
| Pd-ceria-249 | 45.56 / 49.00 | 316.01 / 343.26 |
| skew | 84.82 / 93.30 | 146.59 / 154.90 |
| OUTCAR-static | 49.12 / 49.80 | 70.04 / 72.67 |
| OUTCAR-relax-6MB | 91.75 / 98.15 | 231.71 / 238.50 |

Rasterization dominates these cases. The illustrated 2×2×2 slab spends
about 796 ms in rasterization and 15 ms in PNG writing. The shaded 1,000-atom
case spends about 252 ms in rasterization and 11 ms in PNG writing. The 6 MB
OUTCAR takes about 22 ms to parse on Linux storage. These observations justify
beginning with rendering experiments and retaining process latency as the
selection metric.

Reproduce the baseline with an unmodified v0.1.0 executable:

```sh
python3 tools/benchmark.py /tmp/qasr-m3-reference/vasp-render \
  --output artifacts/m4-m6/reference/linux-baseline.json \
  --work-dir /tmp/qasr-bench-reference --samples 30
```

`tools/benchmark.py` now supports both styles, case selection and additional
renderer arguments. `tools/compare_benchmarks.py` checks scene equivalence,
decoded pixels and latency distributions. It reports a deterministic bootstrap
interval for the ratio of medians from independently resampled runs; this does
not account for host drift or systematic measurement bias. Pixel differences
require explicit experimental opt-in and visual review, and never pass the
lossless correctness gate merely because a candidate is faster.

## M4–M6 progress and remaining gates

- Complete: fresh 20-case shaded/illustrated Linux-storage baseline, original
  reference Release tests and address/undefined-behavior/leak sanitizer tests.
- Implemented: configurable `--aa native|2x|4x|adaptive` / JSON `image.aa`, with 2×
  remaining the default and 4× available as a quality reference. The native
  mode is a point-sampled control, not analytic edge coverage. All three grids
  pass the independent ray oracle for crossing/capped/axial cylinders and cell
  occlusion. Transparent 2×/4× edges and allocation limits are checked.
- Complete: the AA refactor exactly matches decoded RGBA for all 20 baseline
  cases at the default 2× setting. Parity uses two samples per case to check
  pixels, not to select a performance winner. Reports/images are under
  `artifacts/m4-m6/aa/default-parity*`.
- Complete: a six-case Windows-storage comparison, 30 samples per case, with
  identical RGBA. Windows/Linux median ratios are 1.14× (shaded Co-100), 1.33×
  (shaded Pd/ceria), 3.29× (shaded relaxation OUTCAR), and 1.06×, 1.07×, 1.93×
  for their illustrated counterparts. Shaded OUTCAR medians are 301.98 ms on
  Windows storage versus 91.75 ms on Linux storage. These are sequential warm
  runs on this host, not a universal filesystem cost. Raw reports are
  `artifacts/m4-m6/reference/windows-storage.json` and `storage-comparison.json`.
- M4 complete: Cairo native/2× painter and uniform/adaptive Z-buffer
  comparisons, adversarial depth gates, higher-quality metrics, visual review,
  CPU cycles and end-to-end timings. Accurate Z-buffer accepted; 2× and adaptive
  continue into M5 combination testing.
- M5 complete: fixed-buffer PNG profiles, lossless alpha gates, cached/tiled
  rasterization, 1/2/4/8-worker trials, interleaved AA/codec/storage selection
  and final automatic defaults across the full 20-case matrix.
- M6 complete: visible-surface labels, axes, legend and scale bar; allocation/
  work/atomic-output gates; Release and sanitizer suites; WSL install/package/
  source/fallback validation; scoped latency/RSS targets and final reports.
  M7 XML remains outside this goal.

LeakSanitizer cannot run under the tool sandbox's process tracing. Sanitizer
acceptance runs therefore execute outside that tracing layer with leak
detection enabled; both CTest entries passed for the untouched reference.
The AA/tooling revision passes all three CTest entries in Release and with
address, undefined-behavior and leak sanitizers.

## Initial AA controls

Native point sampling and 4× supersampling were measured on Co-100, the
2×2×2 Co slab and Pd/ceria in both styles. Native uses 30 samples per case;
4× uses ten samples per case, primarily to retain a higher-quality reference.
Its timings are exploratory, not a final performance-selection experiment.
All samples and images are under `artifacts/m4-m6/aa/`.

| Case | Native median (ms) | Native vs 4× RMSE | 2× vs 4× RMSE |
| --- | ---: | ---: | ---: |
| shaded-Co-100 | 52.14 | 6.400 | 3.009 |
| shaded-Co-slab-repeat-2x2x2 | 40.08 | 6.649 | 2.514 |
| shaded-Pd-ceria-249 | 23.00 | 7.021 | 3.341 |
| illustrated-Co-100 | 85.04 | 9.717 | 4.756 |
| illustrated-Co-slab-repeat-2x2x2 | 224.43 | 9.018 | 3.912 |
| illustrated-Pd-ceria-249 | 101.42 | 12.827 | 4.916 |

Errors use premultiplied RGBA on the 0–255 channel scale. At these sample
grids, 2× reduces RMSE against 4× by more than half for all six cases.
Visual inspection of the native Pd/ceria image shows staircase contours,
cell lines and bond edges. Native point sampling is rejected as the default
despite its speed; 2× remains the default pending Cairo/adaptive comparisons.
The 4× renderer is a denser sampling reference, not unquestionable physical
ground truth; its per-sample intersections also pass the independent ray
oracle on adversarial cylinder and occlusion tests.

## M4 rendering experiments

The optional `ENABLE_CAIRO_EXPERIMENTS` build provides `--backend cairo`.
It sorts whole primitives by mean camera depth with stable input-order ties,
paints radial-gradient spheres and gradient-filled cylinder silhouettes, and
uses Cairo coverage at native resolution or uniform 2×/4× supersampling.
Illustrated rendering retains the configured flat bands, contours and fading.
Cairo surfaces are converted from native-endian premultiplied ARGB32 to the
shared straight-RGBA encoder input, following the
[Cairo image-surface contract](https://www.cairographics.org/manual/cairo-Image-Surfaces.html).
The conversion and any downsampling are included in render-stage/process time.

Cairo is rejected as the general rendering backend. Two retained gates isolate
surface-order mistakes from shading differences:

- Equal-center-depth red/blue interpenetrating spheres: witness `(40,48)` is
  red in the Z-buffer and independent ray oracle, but blue in the painter.
  Native, 2× and 4× Cairo all fail this depth gate.
- Colored crossing finite cylinders: at `(42,48)`, the front red surface is
  `[163,0,0,255]`; the painter instead returns blue `[0,0,163,255]`. The
  cylinders exchange surface order within their overlap. Higher AA does not
  fix whole-primitive ordering.

Inputs, configuration, witness JSON and PNGs are retained in
`artifacts/m4-m6/m4/depth-gate/`. Both gate images were visually inspected.
The Cairo prototype also uses ordinary layered alpha compositing, whereas
this renderer's established contract composites only the nearest surface over
background; it is not a replacement for that transparency model.
The default build retains the accurate Z-buffer and has no Cairo dependency.

### Adaptive algorithm

The first conservative 8×8-pixel mask allowed only isolated sphere interiors.
It selected zero native tiles on the six dense representative cases and gave
no useful speedup. That negative result is retained in `m4/adaptive.json` and
`m4/initial-adaptive-comparison.json` under the artifact root.

The refined mask uses 4×4 output-pixel tiles and conservative surface-depth
intervals. For each sphere/tile intersection, its maximum possible surface
height is bounded using the nearest rectangle point. A candidate sphere must
cover the entire tile with a one-pixel silhouette guard; its minimum surface
height, computed from the farthest corner, must be strictly ahead of every
other primitive's upper depth bound, including the floating-point tie guard.
Cylinders and cell edges use conservative endpoint-plus-radius depth bounds.
Projected bounding boxes alone never establish visibility.

Only a proven front sphere interior may use a native center ray. Illustrated
tiles additionally require that neither a highlight disk nor an outline
boundary crosses the tile. Background, silhouettes, ambiguous overlap, visible
bonds and cell edges retain all four independent Z-buffer samples and the
original premultiplied-alpha downsampling. Shaded sphere interiors approximate
the four smoothly varying shades with the center shade; quality is compared
against the 4× reference. Tile storage and all transient depth-bound arrays are
included in the 1 GiB allocation budget. Mask construction is included in
render time, and tile counters are retained in each benchmark report.

Random overlapping sphere scenes, partial-alpha elements, odd image dimensions,
alpha edges, thin/crossing bonds and illustrated bands compare against uniform
2× output. The native/2×/4× intersections also retain the independent ray-oracle
checks. The experimental build passes the full five-entry CTest suite in
Release and with address, undefined-behavior and leak sanitizers; current logs
are in `artifacts/m4-m6/m4/`.

### CPU evidence

New reports retain process CPU-time samples as well as wall time. Optional
Linux x86-64 `perf_event_open` measurements collect user-space CPU cycles in
three separate runs per case, including inherited worker threads. Kernel and
hypervisor cycles are excluded. The stopped child resumes just before exec,
so a small wrapper tail is included. Counter-enabled/running times and scaled
cycle values are retained; these extra runs do not wrap or alter the primary
process-latency samples. Hardware counters work on this WSL workstation;
no `perf` package was needed. Unsupported/denied counters are explicitly
recorded as unavailable rather than replaced with inferred cycle counts.

### M4 measurements and decision

The following medians include parsing, scene construction, rendering, PNG
encoding and atomic output. Z-buffer/adaptive/native Cairo each use 30
samples after three warmups. The rejected Cairo 2× variant uses ten samples
as a staged quality/performance diagnostic. Algorithm comparisons use the
same-build 2× reference; all its tested images exactly match the original
v0.1.0 reference pixels. CPU time, separate cycle/RSS samples and PNG bytes
are retained alongside every case. No Cairo speed measurement qualifies
it for acceptance after the depth gates fail.

| Case | Z-buffer 2× (ms) | Adaptive (ms) | Cairo native (ms) | Cairo 2× (ms) |
| --- | ---: | ---: | ---: | ---: |
| shaded-Co-100 | 172.00 | 163.13 | 37.93 | 71.63 |
| shaded-Co-slab-repeat-2x2x2 | 123.14 | 121.00 | 69.26 | 129.65 |
| shaded-Pd-ceria-249 | 51.89 | 50.67 | 30.24 | 47.06 |
| illustrated-Co-100 | 284.61 | 230.12 | 55.96 | 86.50 |
| illustrated-Co-slab-repeat-2x2x2 | 865.12 | 822.47 | 218.59 | 343.13 |
| illustrated-Pd-ceria-249 | 337.68 | 340.30 | 112.69 | 157.26 |

Adaptive is bit-exact to 2× on all three illustrated cases. Against 2×,
shaded maximum channel errors are 1, 2 and 1 levels respectively, with
premultiplied RGBA RMSE 0.0715, 0.0136 and 0.0168. RMSE against the 4×
quality reference remains 3.0091, 2.5142 and 3.3407 (the same to four
decimal places as uniform 2×). Shaded Co-100 uniform/adaptive images were
also visually compared; silhouettes, cell lines and depth edges agree.

For illustrated Co-100, adaptive selects 5,626 of 36,864 tiles, reducing
median latency by 19.1% and median user-cycle count by about 19.8%. The
repeated illustrated slab improves by 4.9%; Pd/ceria has no clear benefit
(the bootstrap interval for its latency ratio includes 1). Small shaded
wall-time gains are not a universal cycle-count improvement. This supports
carrying adaptive and uniform 2× into M5, rather than changing the default
from a single favorable scene. The accurate Z-buffer is the accepted
renderer; Cairo and native point sampling are rejected as general defaults.
Uniform 2× remains the current default until the final encoder/thread/AA
combination is measured. M4 experimental gates are complete.

Reports and retained binaries are under `artifacts/m4-m6/m4/`; filenames
distinguish the initial mask from the refined `adaptive-depth` variant.
Reproduce the experimental build and gates with:

```sh
cmake -S . -B /tmp/qasr-experiments -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_CAIRO_EXPERIMENTS=ON -DBUILD_TESTING=ON
cmake --build /tmp/qasr-experiments -j 10
ctest --test-dir /tmp/qasr-experiments --output-on-failure
python3 tools/benchmark.py /tmp/qasr-experiments/vasp-render \
  --output artifacts/m4-m6/m4/adaptive-depth.json \
  --work-dir /tmp/qasr-bench-m4-adaptive-depth --samples 30 \
  --cases Co-100 Pd-ceria-249 Co-slab-repeat-2x2x2 \
  --label m4-adaptive-depth --render-args --aa adaptive
```

## M5 tile and encoder experiments

The renderer now projects each primitive once and caches cylinder axis/length
and intersection coefficients. It partitions the output into disjoint 32×32
pixel tiles. Each tile traverses primitives in the original order and touches
only its projected-bounds intersection. This preserves per-sample depth ties,
contour evaluation and integer RGBA rounding. OpenMP dynamically schedules
whole tiles; initialization, adaptive-mask construction, final downsampling
and PNG output remain outside that parallel loop. Projection scratch is
included in the total allocation budget. The initial scaling snapshot used one
thread for auto; the selected policy below now enables measured parallelism.

The first six-case scaling experiment uses 30 samples, three warmups and the
original libpng compression-1/adaptive encoder. All decoded pixels match M4
at every thread count, and recorded counters confirm the requested 1/2/4/8
workers were used. Reports, binary, source snapshot and comparison JSON are
under `artifacts/m4-m6/m5/raster/`.

| Case | Cached/tiled 1 thread (ms) | 2 threads | 4 threads | 8 threads |
| --- | ---: | ---: | ---: | ---: |
| shaded-Co-100 | 93.59 | 58.70 | 42.17 | 35.67 |
| shaded-Co-slab-repeat-2x2x2 | 63.16 | 43.48 | 33.34 | 29.67 |
| shaded-Pd-ceria-249 | 34.03 | 29.02 | 26.54 | 25.54 |
| illustrated-Co-100 | 226.99 | 136.26 | 92.24 | 69.17 |
| illustrated-Co-slab-repeat-2x2x2 | 376.38 | 212.12 | 136.15 | 95.21 |
| illustrated-Pd-ceria-249 | 158.98 | 102.51 | 76.30 | 63.48 |

Caching plus tiling alone reduces these one-thread medians to 43.5–79.8% of
the M4 same-style reference. Eight threads further reduce medians to
25.3–75.0% of that cached one-thread version. These are intermediate results;
the full matrix and encoder/AA combination determine the final defaults.

### Isolated PNG comparison

`tools/png_bench.c` reads RGBA once before timing, then measures fresh encoder
creation, filtering, compression, PNG writing, close and atomic rename in
process. Every codec receives the identical buffer. The Python driver records
30 samples after three warmups per profile/input, process CPU samples, a
separate RSS run, output bytes, hashes and an independently decoded RGBA gate.
It retains the exact raw buffers and PNGs. There are 68 profile/input pairs
(2,040 measured encodes) spanning 512-pixel shaded, 768-pixel illustrated,
partial-alpha gradient and opaque random-noise inputs. Raw results are in
`artifacts/m4-m6/m5/encoders/isolated.json`.

Profiles cover libpng compression 0/1/3/6/9 with adaptive/none/sub/up filters,
Cairo's internal PNG settings, and libdeflate 0/1/3/6/9 with none/sub/up/adaptive
filtering. The staged matrix avoids redundant high-compression/filter products.
Libdeflate's adaptive row heuristic chooses among none, sub and up; libpng's
adaptive filtering also evaluates its own other supported filters. Output
bytes are secondary to latency, and every accepted codec must preserve RGBA.

The fast encoder writes standard RGBA8 IHDR/IDAT/IEND chunks with CRCs and a
zlib-wrapped libdeflate stream, following the
[PNG datastream/chunk specification](https://www.libpng.org/pub/png/spec/iso/index-object.html).
It includes raw scanlines, compression-bound buffers, retained image/scene and
a conservative compressor allowance in the 1 GiB aggregate allocation check.
The existing libpng path remains available. All codecs share unique temporary
files, close checking and atomic destination replacement.

| RGBA input | libpng 1 adaptive (ms) | libdeflate 0 none | libdeflate 1 none | libdeflate 1 up | Cairo internal |
| --- | ---: | ---: | ---: | ---: | ---: |
| shaded-512 | 11.188 | 1.086 | 3.092 | 3.896 | 24.753 |
| illustrated-768 | 16.008 | 2.977 | 5.327 | 5.805 | 23.000 |
| alpha-512 | 7.251 | 1.133 | 4.497 | 1.474 | 57.714 |
| noise-512 | 33.147 | 1.091 | 8.136 | 8.519 | 22.433 |

All libpng/libdeflate profiles are byte-exact after independent decoding.
Cairo is exact for opaque input but changes RGB at partial alpha (up to 127
straight-channel levels in the low-alpha stress case), despite preserving the
alpha channel. Its premultiplied ARGB32 roundtrip therefore fails the lossless
RGBA gate and is rejected as a general encoder. It is also slower here.

Libdeflate 0/none is fastest on all four buffers, but writes uncompressed zlib
blocks: PNG sizes are 1,049,236 bytes at 512×512 and 2,360,312 bytes at 768×768.
For comparison, libdeflate 1/none writes 154,689 bytes for the shaded example
and 88,110 bytes for the illustrated one, at modest extra CPU cost. Full-process
and Windows-mounted-output comparisons are in progress before choosing a
latency/storage policy.

The seven-entry experimental CTest suite (57 checks) passes in Release and
with address, undefined-behavior and leak sanitizers. Added gates cover all
codec levels/filters on random partial-alpha data, RGB/RGBA decoding, threaded
render/encode integration, option precedence, aggregate memory rejection,
file-size-limit write failures and failed rename cleanup. Existing destinations
survive encoder/write failures. Sanitizer and Release logs are retained under
`artifacts/m4-m6/m5/encoders/`.

### Interleaved combination selection

Sequential full-matrix trials compared level-0/level-1 deflate and adaptive AA
at eight threads: all uniform-2× outputs match all 20 original baseline RGBA
hashes; adaptive remains exact on all ten illustrated cases and differs by at
most two channel levels on the ten shaded cases. To resolve small codec timing
differences, `tools/benchmark_combinations.py` then randomized four surviving
profiles within each of 30 sample rounds per case. The same input, executable,
thread count and filesystem were used within a trial. Per-round profile order,
stage/CPU samples, commands and decoded images are retained in
`m5/selection/interleaved-linux/` and `interleaved-windows/` under the artifact
root. These latency retakes do not recollect cycles/RSS; the earlier full
matrix retains those separate measurements.

| Case | Linux 2×/level 0 (ms) | Linux 2×/level 1 | Windows 2×/level 0 | Windows 2×/level 1 |
| --- | ---: | ---: | ---: | ---: |
| shaded-Co-100 | 25.41 | 28.18 | 41.39 | 40.03 |
| shaded-Pd-ceria-249 | 19.72 | 20.76 | 35.98 | 33.60 |
| illustrated-Co-100 | 61.50 | 62.95 | 83.58 | 77.89 |
| illustrated-Pd-ceria-249 | 49.95 | 52.66 | 74.89 | 70.23 |

The interleaved level-1/level-0 median ratio is 1.02–1.11 in the Linux cases,
and 0.93–0.97 in the four Windows POSCAR cases above. Windows OUTCAR timing is
mostly input I/O and shows a smaller codec effect. Filesystem-aware compression
therefore follows measured end-to-end behavior, rather than using the isolated
encode winner on every output path.

Adaptive improves the interleaved illustrated Co-100 case (61.50→52.67 ms on
Linux at level 0). It has little benefit on the repeated slab and a small cost
on Pd/ceria once rasterization is parallel. The general default remains uniform
2×, preserving all original image pixels. Adaptive remains an explicit option
for scenes where profiling justifies it.

The selected policy is:

- Accurate Z-buffer, uniform 2×, cached cylinder/projection data and disjoint
  32×32 raster tiles.
- Automatic threads: up to eight available workers for at least 512×512 output
  pixels, bounded by OpenMP environment/processor limits; one worker below
  that area. Explicit thread counts remain available.
- Libdeflate/no filtering, with compression `auto`: level 0 on native Linux
  storage and level 1 on the tested 9p Windows-mounted filesystem, detected
  from the destination parent. Explicit compression levels bypass this policy.
- libpng remains the reference/fallback codec; OpenMP and libdeflate can each
  be disabled at configure time. Cairo rendering/encoding remain optional
  rejected experiments.

The automatic filesystem rule is tuned to this WSL workstation's measured
storage. Other filesystems and hardware may favor different explicit settings.
The policy does not claim OS-cold-cache behavior or disk durability beyond
successful close/rename. The final default 20-case run and Windows-path check
are complete under `artifacts/m4-m6/m5/defaults/`.

### Selected defaults: complete M5 validation

The default production build has OpenMP and libdeflate enabled, and Cairo
disabled. Automatic workers resolve to eight on all 20 representative
512/768-pixel cases. Automatic compression resolves to level 0 on all
20 Linux-storage cases and level 1 on all six Windows-storage cases.
Every decoded RGBA image matches the original reference exactly. Reports,
the measured binary and full comparison JSON are retained under
`artifacts/m4-m6/m5/defaults/`.

| Case | Selected median / p95 (ms) | Original median (ms) | Median speedup |
| --- | ---: | ---: | ---: |
| shaded-Co-100 | 24.91 / 31.95 | 159.67 | 6.41× |
| shaded-Co-500 | 35.69 / 37.96 | 221.52 | 6.21× |
| shaded-Co-1000 | 61.22 / 68.80 | 289.05 | 4.72× |
| shaded-Co-slab | 18.30 / 19.51 | 66.38 | 3.63× |
| shaded-Co-slab-repeat-2x2x1 | 22.25 / 26.31 | 139.19 | 6.26× |
| shaded-Co-slab-repeat-2x2x2 | 22.33 / 24.90 | 111.89 | 5.01× |
| shaded-Pd-ceria-249 | 18.23 / 23.31 | 45.56 | 2.50× |
| shaded-skew | 17.63 / 22.62 | 84.82 | 4.81× |
| shaded-OUTCAR-static | 15.71 / 17.10 | 49.12 | 3.13× |
| shaded-OUTCAR-relax-6MB | 40.37 / 45.70 | 91.75 | 2.27× |
| illustrated-Co-100 | 59.33 / 64.59 | 264.30 | 4.46× |
| illustrated-Co-500 | 90.84 / 102.06 | 475.77 | 5.24× |
| illustrated-Co-1000 | 127.74 / 135.12 | 613.31 | 4.80× |
| illustrated-Co-slab | 62.02 / 67.23 | 492.28 | 7.94× |
| illustrated-Co-slab-repeat-2x2x1 | 60.65 / 68.07 | 491.18 | 8.10× |
| illustrated-Co-slab-repeat-2x2x2 | 82.68 / 87.74 | 813.49 | 9.84× |
| illustrated-Pd-ceria-249 | 50.36 / 59.75 | 316.01 | 6.28× |
| illustrated-skew | 37.50 / 44.07 | 146.59 | 3.91× |
| illustrated-OUTCAR-static | 30.99 / 33.45 | 70.04 | 2.26× |
| illustrated-OUTCAR-relax-6MB | 65.39 / 72.78 | 231.71 | 3.54× |

These warm-cache native-storage medians range from 15.71–61.22 ms shaded
and 30.99–127.74 ms illustrated. The slowest measured p95 is 68.80 ms
shaded and 135.12 ms illustrated. This is the M5 checkpoint; M6 retains
overlays, final deployment/package validation, further hardening and
publication of scoped workstation latency targets. No native Linux/HPC
acceptance is pending, because WSL was explicitly approved for all tests.

## M6 final acceptance

M6 adds visible-surface element/instance labels, Cartesian axes, element counts
and a physical angstrom scale bar. Legend/footer space is reserved before
camera fitting. Labels try the center and eight 55%-radius surface samples,
then use foreground-first collision filtering with stable instance-index ties.
The center-only prototype suppressed labels beneath periodic bond caps; that
negative result and binary are retained as `m6/annotated-native-center*` and
`m6/prototypes/`. Periodic-cap, coincident-surface and collision regressions
exercise the refined policy. Glyph coverage and backing plates preserve
straight-alpha compositing, and all transient label storage is budgeted.

Both plain and annotated 20-slab galleries are retained under `m6/plain-top/`
and `m6/annotated-top/`, with input/preset/PNG hashes and camera metadata. The
annotated Pd/ceria image was visually checked after collision filtering.
[M6 operating/validation notes](m6-validation.md) describe the annotation
semantics, optional-feature builds, local package commands and limitations.

The final source passes the default six-entry Release suite (63 checks), plus
the eight-entry all-feature Release and address/undefined-behavior/leak suites
(67 checks). Their logs are in `artifacts/m4-m6/m6/validation/`. Deployment
validation is captured in `m6/deployment/report.json`: private-prefix install,
shared-library resolution, packaged execution, source-manifest checks, clean
source builds/tests, a serial/libpng-only build and identical annotated pixels.
The user-approved WSL environment is sufficient for this acceptance.

### Scoped targets and measured results

`performance-targets.json` defines the lab thresholds from the fresh workstation
baseline and M5 observations. `tools/check_performance.py` independently
re-decodes the original and final PNGs, checks raw sample summaries, all 20
pixel-preserving native cases, the six Windows-path cases, eight fully annotated
cases, worker/codec resolution, annotation metadata and latency/RSS targets.
All 34 cases pass; the full machine-readable result is
`artifacts/m4-m6/m6/acceptance.json`.

| Profile | Cases | Median range (ms) | Highest p95 (ms) | Highest separate-run RSS (MiB) |
| --- | ---: | ---: | ---: | ---: |
| Native shaded | 10 | 15.49–58.91 | 64.72 | 17.12 |
| Native illustrated | 10 | 30.80–127.32 | 133.60 | 33.20 |
| Windows path | 6 | 32.61–282.70 | 340.16 | 32.34 |
| All annotations | 8 | 21.00–114.02 | 129.89 | 33.62 |

Native targets are median/p95 ≤75/90 ms shaded and ≤150/180 ms illustrated,
with representative RSS ≤128 MiB. Windows cases target ≤350/400 ms, and fully
annotated cases ≤250/350 ms. Measurements use 30 samples after three warmups,
selected automatic workers and output compression, and warm caches; close and
rename are included, fsync is not. Annotated cameras reserve panel space, so
their latency is not a pure additive annotation cost over the original framing.

Every unannotated final image matches its original reference RGBA exactly.
The best median improvement across the native representative matrix is about
9.9×. Raw samples, stage times, CPU cycles, output bytes and hashes remain local
Git-ignored artifacts. No physical-disk, cold-cache, all-host or all-VASP-version
claim follows from these results. Other hardware/storage can use explicit AA,
codec/compression and thread overrides. The measured implementation forms
v0.2.0; v0.1.0 remains the original reference checkpoint. See
[v0.2.0 release checks](release-v0.2.0.md) for versioned package validation.
