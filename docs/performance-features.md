# Performance round: faces, cell strokes and movies

This round measures the feature-complete renderer after adding polygon faces,
atom-colored bonds, styled unit-cell edges and optional FFmpeg movies. Its
baseline is that local feature revision, rather than the older v0.5.0 release.
Three read-only reviews identified rendering, movie-processing and measurement
opportunities before implementation. A second review checked the resulting
ownership, memory-budget and benchmark changes.

## Retained changes

- Polygon rasterization still gathers every hit and enforces the 64-face layer
  limit. It skips compositing uncovered background, directly copies a sole
  opaque hit, and omits color calculations for layers erased by a nearer opaque
  surface. Sorting, tie ordering, triangulation and barycentric depth stay the
  same. Polygon-outline edge thresholds are prepared once per triangle.
- Styled cell edges cache their finite inner and white-surround dash cylinders
  once per projected frame. Calculations retain their original operation order,
  including the nearly end-on fallback. The complete optional cache must fit
  the remaining render allocation budget; insufficient space or allocation
  failure selects the existing per-sample path. The cache is released before
  polygon scratch allocation. Label visibility uses the uncached path.
- Movies reuse the depth and RGBA sampling buffers between frames. Every frame
  still initializes all samples, and each output image retains independent
  pixel ownership. Resizing and failure cleanup release owned storage. Scene
  construction accounts for the live buffers and movie reservation before
  allocating or growing a subsequent trajectory scene.
- Interpolated trajectory snapshots reuse their atom arrays, preserving the
  original interpolation expressions. Observer passes retain frame zero in
  their unused selected-snapshot result while continuing to visit and validate
  every source frame through EOF. The initial full input validation, camera-fit
  pass, render pass and content-change checks are retained.

Encoding preset, encoder thread count, YUV conversion, image quality, camera
framing, connectivity and file-commit behavior have not been retuned for speed.
The first/full-frame and invalid-input semantics remain covered by regression
tests. A new C gate checks reusable-sample ownership/resizing and scene-growth
budget rejection without allocating a large scene.

## Measurement method

`tools/benchmark_features.py` runs 41 scenarios: 30 still-image workflows and
11 movies, with 1/8-thread variants where applicable. The matrix includes
single/full/repeated polygon faces, opaque-face labels, partial alpha,
atom-colored bonds, solid/dashed/outlined cells, camera orbits, interpolated
reaction paths, XYZ/XML/OUTCAR trajectories and existing dense/style/codec
controls. Source recipes, sidecars and generated input geometries are retained
or reproducible from the driver.

For each scenario, three warmups precede 30 paired measurements per executable.
The pairs have balanced AB/BA ordering shuffled with seed 1729. CLI wall time
includes startup, parsing, rendering, output and process exit. Output validation
and independent decoding occur outside the timed interval. Reports retain wall,
CPU and available stage times, median/p95, raw pair order, binary/source/input
hashes and three separate peak-RSS samples per executable and scenario.

The confidence intervals are pointwise 95% paired bootstrap intervals for the
candidate/baseline median ratio. They do not adjust for multiple comparisons or
all host drift. Measurements use warm caches on one WSL workstation; CPU
frequency and unrelated host activity are uncontrolled. There is no cold-cache,
cross-machine or universal tail-latency claim.

The measured workstation is an Intel Core i9-10900K under WSL2, with GCC 13.3.0
Release flags `-O3 -DNDEBUG`. Both builds enable OpenMP, XML, libdeflate,
JPEG, WebP and FFmpeg; Cairo is disabled for timing. The linked FFmpeg library
versions are libavcodec 60.31.102, libavformat 60.16.100, libavutil 58.29.100 and
libswscale 7.5.100. Full environment and dependency details are in the raw report.

Both executables run from native Linux storage, with generated inputs and
outputs on `/tmp`. Real slab fixtures and retained example recipes use the same
repository paths for both executables. The driver rejects executable pairs on
different filesystem types. An initial discovery run executed the archived
baseline from the Windows mount, so its timings are excluded from conclusions.

Every timed run must match exact encoded image or MP4 bytes. Still-image scenes
must match inspection JSON. Movie reports must match after removing timings;
independent ffprobe metadata and per-frame decoded RGBA hashes must also match.
Frame inventory, timestamps, dimensions, codec, duration and camera information
are retained. Movie `render_encode` includes several operations, so gains in
that stage must not be attributed solely to encoding.

An immutable baseline archive and hashes include the newly added, untracked
feature source files. The final report likewise hashes all measured source,
recipe and test files, including untracked files. The measured candidate binary
and source snapshot are retained beside the reports.

## Reproduce

Use the retained baseline executable with a fresh Release candidate built using
the same settings:

```sh
cmake -S . -B /tmp/qasr-features-final -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FFMPEG=ON -DENABLE_JPEG=ON -DENABLE_WEBP=ON
cmake --build /tmp/qasr-features-final --parallel 8
python3 tools/benchmark_features.py \
  /tmp/qasr-features-baseline/qasr /tmp/qasr-features-final/qasr \
  --output artifacts/perf-features/final.json \
  --samples 30 --warmups 3 --memory-samples 3
```

The complete matrix requires OpenMP, XML, JPEG, WebP and FFmpeg support with
libx264, plus the ffmpeg/ffprobe executables for independent validation. The
renderer itself uses shared FFmpeg libraries directly. Use `--cases` to select
named scenarios; selected inventories are still checked for completeness.

`artifacts/perf-features/final.json` is the primary comparison. Discovery and
prototype reports describe earlier measurements and must not be mixed with the
final case inventory. Additional length-scaling memory checks and test results
are recorded with the final validation artifacts.

## Correctness checks

- Release with FFmpeg, JPEG and WebP: 21/21 CTest groups passed.
- Release with FFmpeg, OpenMP, XML and libdeflate disabled: 21/21 groups passed,
  including the expected unavailable-capability skips.
- Address/undefined-behavior sanitizers with leak detection, optional codecs and
  Cairo enabled: 23/23 groups passed.
- The additional allocation-boundary/fallback gate also passed after its final
  expansion, under Release, disabled-feature and sanitizer builds.

The new regressions cover uncovered polygon backgrounds including zero and low
alpha, opaque hits, preservation of the layer limit behind an opaque face,
end-on cell strokes, reusable buffer ownership/resizing, optional dash-cache
fallback, and early scene/style allocation-budget rejection. Existing movie
tests cover fixed cameras, interpolation, timestamps, atom identity validation,
malformed trailing frames, cancellation and destination preservation.

## Final measurements

Times are median / p95 wall milliseconds; reductions compare medians. Ratio
intervals are candidate/baseline, with values below 1 favoring the candidate.

| Workflow | Baseline ms | Optimized ms | Median reduction | Ratio 95% interval |
| --- | ---: | ---: | ---: | ---: |
| Cage with translucent faces, t1 | 141.75 / 153.79 | 128.71 / 134.96 | 9.2% | 0.899–0.917 |
| Four cages with faces, t1 | 146.96 / 152.71 | 128.32 / 133.63 | 12.7% | 0.865–0.880 |
| Single translucent face, t1 | 106.48 / 123.51 | 87.91 / 93.37 | 17.4% | 0.806–0.838 |
| Opaque face with labels, t1 | 103.25 / 108.32 | 70.49 / 74.11 | 31.7% | 0.674–0.689 |
| Dashed cell, t1 | 236.57 / 250.13 | 224.27 / 230.78 | 5.2% | 0.942–0.957 |
| Dashed cell + white contour, t1 | 299.37 / 314.90 | 253.64 / 281.36 | 15.3% | 0.836–0.855 |
| 24-frame polygon cage orbit, t1 | 855.95 / 896.33 | 644.81 / 696.78 | 24.7% | 0.747–0.761 |
| 16-frame 1024² orbit, t1 | 796.41 / 815.48 | 675.62 / 713.14 | 15.2% | 0.841–0.852 |
| 128-atom / 64-frame XML movie, t1 | 135.40 / 141.87 | 126.68 / 140.12 | 6.4% | 0.925–0.943 |
| 128-atom / 64-frame OUTCAR movie, t1 | 126.13 / 129.91 | 116.42 / 130.39 | 7.7% | 0.913–0.932 |
| 4096-atom still control, t1 | 362.49 / 379.61 | 306.13 / 318.35 | 15.5% | 0.838–0.854 |
| 4096-atom still control, t8 | 297.91 / 307.18 | 242.35 / 252.73 | 18.7% | 0.805–0.827 |

All 41 cases passed exact encoded-output comparison. All 11 movies also
passed independent decoded-frame/timestamp comparisons and stable-report
comparison. No scenario had a pointwise ratio interval wholly above 1.
Reaction playback, unfilled atom-colored bonds, ORTEP/BALSAC and codec controls
were essentially unchanged; improvements are specific to the measured paths.
The eight-thread translucent-cage still interval crossed 1, so its small
median change is not treated as a confirmed gain.

The dense controls also improved in a separate 30-pair confirmation. This round
does not change the atom-pair enumeration algorithm; no complexity reduction is
claimed. See `dense-confirmation.json` for that additional measurement.

## Memory

Across the 41 workflow cases, the largest increase in the median of three
separate peak-RSS samples was 552 KiB. The reaction movie samples used about
4–5 MiB less peak RSS; these are observed process measurements, not allocation
guarantees for other structures.

The separate 128²/native, one-thread MPEG-4 controls increased output length
from 16 to 1024 frames, for both orbit and trajectory playback. Median optimized
peak RSS stayed around 34 MiB, without growth proportional to movie length.
All encoded outputs/reports matched the baseline and ffprobe confirmed the
actual frame counts. These simple-geometry controls establish the measured
length-scaling behavior; they are not a general memory bound for larger inputs.

```sh
python3 tools/benchmark_movie_memory.py \
  /tmp/qasr-features-baseline/qasr /tmp/qasr-features-final/qasr \
  --output artifacts/perf-features/movie-memory.json --samples 3
```
