# Performance measurement

Measure the workflow you intend to use: one still image, repeated views through
a worker, or a movie. Keep input, view, dimensions, sampling, style, codec and
thread policy fixed while comparing builds. Confirm output equivalence before
interpreting timing differences.

The latest measured rounds are:

| Record | Scope |
| --- | --- |
| [Structure/render iteration](performance-iteration.md) | Text readers, bond search, opaque rasterization, CLI/worker and filesystem checks |
| [Faces, cell strokes and movies](performance-features.md) | 41 workflows, exact encoded output, independent movie decoding, retained-buffer and length-scaling checks |
| [Architecture/contract cleanup](archive/code-polish.md) | Follow-up output/performance parity after moving interfaces and policy |

These are dated workstation results with named baselines and uncertainty, not
universal speed or memory guarantees. The feature-round baseline was a retained
feature-complete local snapshot; it was not the earlier v0.5.0 release without
faces/movies. Reproduction needs comparable baseline source/binaries.

## Choose a measurement tool

| Question | Tool |
| --- | --- |
| Where does one command spend time? | `qasr INPUT --benchmark-stages --report result.json -o image.png` |
| Do still/movie feature workflows improve without output changes? | `tools/benchmark_features.py` |
| Do movie buffers grow with frame count? | `tools/benchmark_movie_memory.py` |
| Do repeated requests benefit from a worker/cache? | `tools/run_m10_workloads.py` with CLI and worker backends |
| How do workers and render threads share the CPU? | `tools/benchmark_worker_pool.py` |
| How do fixed still-image profiles compare? | `tools/benchmark.py`, `tools/compare_benchmarks.py` |
| How does XML parsing scale? | `tools/benchmark_xml.py` |

Command stage times exclude process startup and some report/hash work. Movie
render/encode stages combine several operations; they do not isolate the encoder.
Measure whole-process wall time when that is the question. Worker request latency
also differs from total worker startup/completion time. Separate peak-RSS samples,
CPU time and wall time are distinct metrics.

## Reproduce feature comparisons

Use Release builds with matching capabilities and dependencies. The complete
41-case matrix needs OpenMP, XML, libdeflate, JPEG, WebP and FFmpeg with libx264;
it also needs `ffmpeg`/`ffprobe` for independent validation. Cairo is disabled for
the recorded timing comparisons. See the [build instructions](../README.md).

```sh
cmake -S . -B /tmp/qasr-candidate -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FFMPEG=ON -DENABLE_JPEG=ON -DENABLE_WEBP=ON
cmake --build /tmp/qasr-candidate --parallel 8
python3 tools/benchmark_features.py \
  /tmp/qasr-baseline/qasr /tmp/qasr-candidate/qasr \
  --output artifacts/performance/comparison.json \
  --samples 30 --warmups 3 --memory-samples 3
```

Build `/tmp/qasr-baseline/qasr` from the chosen baseline snapshot first. Put both
executables on the same filesystem; the driver rejects mixed filesystem types.
Record input/output storage too. Native Linux and Windows-mounted storage can
have different startup, input and encoding costs. Select cases with `--cases`
when a full feature matrix does not fit the comparison.

The driver retains shuffled balanced AB/BA pairs, raw wall/CPU/stage samples,
source/binary/input hashes, encoded outputs and separate RSS runs. Exact bytes
must match; still scenes and movie reports/probe metadata/decoded frames are
checked outside the timed region. Strict report comparisons can also reject
different version/provenance metadata; use comparable builds rather than
weakening the correctness oracle to obtain a timing result.

For worker/cache comparisons, follow [synthetic workloads](synthetic-workloads.md)
and the [worker guide](batch-worker.md). Their runners consume driver plans,
translate them to the actual job protocol, and compare outcomes with a retained
reference. Use a new dataset/output directory for each run.

## Interpret results and historical targets

Use enough warmups and repeated samples, retain pair order, and report the
workstation, compiler/libraries, storage, thread policy and measurement scope.
Warm-cache results are not cold-cache results. Uncontrolled host load/frequency
and pointwise confidence intervals limit the conclusions. A cache hit still
requires rasterization and output encoding.

`performance-targets.json` and `tools/check_performance.py` retain the original
i9-10900K/WSL lab profiles and their required case inventory. Those scoped
thresholds are not a shared-CI or universal v0.6.0 latency promise. The full
[initial workstation/encoder/threading record](archive/performance-v0.1-v0.2.md)
explains their selection. Earlier XML and worker measurements are indexed in the
[archive](archive/README.md).
