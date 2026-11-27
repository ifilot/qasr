# Animation rendering benchmarks for v0.8.2

The requested water fixture and reference crop PNG were absent from the
attachment. These measurements use the repository's CO geometry and two bundled
PLY meshes with 4,536 triangles in their headers. The setup approximates the
release brief's orbital workload and uses its 3.224 Å direct window. The water
fixture's crop agreement, scaling and throughput remain unverified.
[Substitute recipes](../tests/fixtures/animation-substitute/README.md)

## Machine and method

- Intel Core i9-10900K, 10 cores and 20 logical CPUs, Ubuntu 24.04 under WSL2
  (kernel 6.18.33.2). Benchmarks ran without pinning OpenMP threads.
- GCC 13.3.0, CMake Release, `-O3 -DNDEBUG -std=c11 -fopenmp`. OpenMP, libpng
  1.6.43, libdeflate 1.19 and libxml2 2.9.14 were enabled. Cairo, FFmpeg,
  JPEG, WebP and sanitizers were disabled.
- Baseline was the existing v0.8.1 build at the start of the task, with executable
  SHA-256 `2d58ff412a9bf2adb55f2d2980aaa765b07aa099ea7d994b68014df41b61d8f8`.
- Geometry, recipes and output PNGs were on native WSL storage. PLY paths
  resolved to the repository on the Windows mount. For the encoder test, the
  output location was varied and the input pixels stayed on native storage.
- The static matrix uses five jobs per cell. Render times are medians from
  `--benchmark-stages` or the worker's `stages_ms.render`. CLI wall time is
  elapsed time per subprocess. Batch wall time is total elapsed time divided
  by job count and includes the worker's mandatory report and output hashing.
  All matrix batch runs reused the scene on four of five jobs.

A static case can be repeated from the repository root with this command.

```sh
qasr examples/orbitals/co.xyz --no-user-config --threads 8 \
  --config tests/fixtures/animation-substitute/1100-4x.json \
  -o /tmp/qasr-1100.png --benchmark-stages
```

## Profile

A separate v0.8.1 `-pg` build, sampled at 1100 px with 4x and one thread,
attributed about 53% of sampled time to orbital contour work, including 37%
in the serial distance sweeps. v0.8.2 restricts contour work to projected
mesh bounds and processes independent stripes. Each stripe includes enough
neighboring rows for the two stroke thresholds to match the previous full
image result. Triangle projection and coverage use independent work units
with stable depth ties. Empty raster tiles avoid writing a depth buffer.

The v0.8.1 config-only stage measured 30–35 ms with PLYs on the Windows mount,
2.2–2.6 ms with local copies and about 0.1 ms with the orbital layers removed.
Each layer previously opened the file three times for hashing, parsing and
rehashing. The loader now computes the hash from the bytes it parses and
checks the opened file for modifications. Worker scene keys still use PLY
content hashes.

## Static render timings

Render and wall values are in milliseconds per frame. `auto` selected eight
threads on this host.

| Case | Threads | CLI render 0.8.1 → 0.8.2 | CLI wall 0.8.1 → 0.8.2 | Batch render 0.8.1 → 0.8.2 | Batch wall 0.8.1 → 0.8.2 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1100 px 4x | 1 | 1010.8 → 326.2 | 1051.1 → 353.6 | 967.5 → 316.6 | 1012.1 → 388.3 |
| 1100 px 4x | 8 | 642.0 → 99.9 | 681.2 → 129.4 | 639.7 → 93.1 | 696.8 → 149.1 |
| 1100 px 4x | auto | 616.4 → 102.1 | 654.7 → 128.0 | 645.2 → 90.5 | 697.1 → 140.6 |
| 640 px 4x | 1 | 405.5 → 183.0 | 440.3 → 202.2 | 399.4 → 167.7 | 434.4 → 200.6 |
| 640 px 4x | 8 | 242.4 → 54.9 | 275.7 → 75.1 | 238.7 → 52.4 | 275.2 → 82.6 |
| 640 px 4x | auto | 235.2 → 59.3 | 268.4 → 76.5 | 230.6 → 47.0 | 266.9 → 75.1 |
| 640 px adaptive | 1 | 106.5 → 51.7 | 142.1 → 67.8 | 107.8 → 66.2 | 146.8 → 99.3 |
| 640 px adaptive | 8 | 63.9 → 20.2 | 99.9 → 39.9 | 57.6 → 18.9 | 95.9 → 48.8 |
| 640 px adaptive | auto | 63.1 → 21.9 | 95.7 → 41.5 | 61.5 → 21.3 | 100.5 → 51.8 |

At 1100 px, the v0.8.2 eight-thread render was **6.43× faster** than
v0.8.1 at eight threads. The v0.8.2 1-to-8 render speedup was **3.27×**, below
the 3.5× target on the substitute.
The CLI config-stage median across the matrix went from **25.4 to 10.3 ms**.

## 180-frame turntable

The geometry and PLYs stayed constant while the camera changed every frame.
Both modes wrote PNGs and computed report hashes. Every batch run hit the
parsed source and scene caches on 179 of 180 frames. The measured static and
turntable frames use the existing 2x fallback when adaptive AA sees polygons.
[Turntable recipe](batch-worker.md#orbital-turntable)

| Threads | Separate v0.8.1 | Batch v0.8.1 | Separate v0.8.2 | Batch v0.8.2 |
| --- | ---: | ---: | ---: | ---: |
| 1 | 4.9 fps | 5.0 fps | 9.1 fps | 9.2 fps |
| auto | 7.4 fps | 7.7 fps | 17.1 fps | 19.0 fps |

## Concurrent CLI workers

Aggregate throughput on v0.8.2, using 60 camera poses per configuration at
640 px adaptive. The CLI measurement covers PNG output with no report sidecar.
Each worker starts a fresh QASR process for each assigned frame.

| Concurrent workers | Threads per worker | Frames/s |
| ---: | ---: | ---: |
| 1 | 20 | 13.4 |
| 4 | 1 | 37.5 |
| 8 | 1 | 63.6 |
| 12 | 1 | 72.4 |
| 16 | 1 | 75.5 |
| 20 | 1 | 73.9 |

## Lossless PNG encoding

Medians over 15 writes per level after three warmups, with the libdeflate
`none` filter and atomic output. Sizes are bytes. Times are milliseconds and
include the write and rename. Both inputs came from the baseline render
(640 px adaptive and 1280 px 2x).

| Level | 640 bytes | 640 native ms | 640 Windows mount ms | 1280 bytes | 1280 native ms | 1280 Windows mount ms |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 1,639,233 | 2.91 | 15.50 | 6,555,448 | 10.85 | 43.51 |
| 1 | 7,766 | 3.98 | 9.13 | 20,255 | 14.91 | 20.85 |
| 2 | 7,199 | 3.91 | 9.30 | 18,320 | 14.31 | 20.62 |
| 3 | 7,039 | 3.69 | 8.83 | 18,092 | 14.20 | 21.80 |
| 4 | 6,997 | 4.39 | 9.03 | 17,970 | 15.14 | 20.35 |
| 5 | 6,935 | 3.90 | 8.75 | 17,747 | 15.55 | 20.21 |
| 6 | 6,824 | 3.88 | 8.57 | 17,555 | 14.26 | 19.59 |

File-output `auto` retains v0.8.1's lossless policy to preserve existing PNG
hashes. It selects libdeflate level 0 on native storage and level 1 on a 9p
mount. Set `image.png_compression` to `1` or pass `--png-compression 1` to get
small files regardless of the output filesystem. This changes the encoded
SHA-256 on storage where `auto` selects level 0. Every decoded RGBA pixel is
preserved. The CLI's standard output carries JSON reports.

## Compatibility checks

The three repository golden renders match their decoded RGBA manifest and
retain the v0.8.1 PNG hashes below. All four static substitute recipes match
the v0.8.1 PNG hashes at 1, 8 and auto threads. Twelve additional combinations
of camera angles, outline widths and AA match v0.8.1 byte for byte.

| Baseline case | SHA-256 of PNG |
| --- | --- |
| skew.png | `79ce43583d0dd7d55e5d7a758d500411356357ec402f79b77ab91c3f38e4ad79` |
| co-repeat.png | `c8733c3983046215f57b1a025afdf894ed39b6d20a5fedaba9ae42a73c101922` |
| pd-ceria.png | `336af3ae54144de83b98035f0052ee05404f6a048ec3eed3d48f9272e7e93485` |
| 1100-4x.png | `bcf55e29f18cf59f639add721e54868d4881e9530e2a47b048c6f4bd681d15be` |
| 640-4x.png | `21faba5208f4182f1ab9ef34a7254c54f8040df985cb70569e0c4067cf16f1c7` |
| 640-adaptive.png | `b72cd4e37d688f0b1e7c87432fea154c455bcfa71ab8d2666aca89b7fad0912a` |
| 1280-2x.png | `2bc2b12288af2bbc04a543790a6f984f12ae2dd1aff546cd3726bb8a2ec086b6` |

## Large 4x orbital renders

At 1900 × 1900 and 4x, the fixed-window substitute exceeded the projected
polygon allocation budget on v0.8.1 and rendered successfully on v0.8.2.
For a 2133 × 2133 orbital recipe,
`--validate-config` now explains that the 4x image buffers alone exceed 1 GiB
and suggests 2x or smaller dimensions. Complex meshes can still encounter
the raster work budget or aggregate memory limit at smaller dimensions.

## Thread sweep at 640 px

In a separate five-job sweep, the best observed median wall time was
73 ms for 4x (auto or 12 threads) and 35 ms for adaptive (6 threads).
This sweep included 1, 2, 4, 6, 8, auto, 12, 16 and 20 threads. The result
is for a single frame in flight. Use the concurrent-worker measurements
above for frame-pool throughput.
