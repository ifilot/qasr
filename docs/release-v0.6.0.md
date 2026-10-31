# QASR v0.6.0 release — 2026-10-31

QASR v0.6.0 adds polygon faces for zeolite tiles, atom-colored bond halves,
styled unit-cell lines and optional MP4 movies. It also improves the measured
rendering workflows and clarifies internal module and ownership contracts.
The executable remains `qasr`; configuration and job schema versions remain 1.

The [offline handbook](website/index.html), [complete options guide](website/options.html)
and [quick start](../README.md) cover installation and usage. The handbook source
download builds this release; the separately labeled v0.3.0 archive is unchanged.

**Included since v0.5.0:**

- Independent polygon loops from explicit Cartesian/fractional vertices or
  source atom indices, with periodic image offsets, repetition, alpha and inset
  boundary lines. Concave loops and depth-ordered transparent faces are supported.
  Complete LTA cage and single-ring examples include bridging oxygen vertices,
  original source attribution and reproducible recipes. CIF requires conversion.
- Configurable bond radius and endpoint-colored halves through CLI, JSON jobs
  and workers. Both endpoint instances must be displayed. Expanding the context
  restores connections to included neighbors and omits stubs at the outer edge.
- Solid/dashed unit-cell lines with adjustable color, width, dash length/gap and
  a white contour measured per side in output pixels.
- Optional `ENABLE_FFMPEG=ON` support for direct streaming to shared FFmpeg
  libraries: camera orbits and fixed-camera XYZ/Extended XYZ/OUTCAR/XML playback,
  linear interpolation, stable global framing, movie reports and batch requests.
- Measured text-input, bond-search and opaque-raster improvements, followed by
  polygon compositing and cell-stroke optimizations, reusable movie sampling and
  interpolation buffers, and fewer unused trajectory snapshot copies.
- Separate image data/encoding and hash interfaces; movie policy lives in
  configuration. API documentation explains ownership, callback lifetimes,
  sequential use, retained-memory budgets and output/report commit boundaries.
- Searchable documentation for all 61 CLI controls, JSON and face fields,
  downloadable style/zeolite/reaction examples and checked offline assets.

**Compatibility and limits.** Bond endpoint membership is mandatory, with no
override. Older settings that requested clipped boundary fragments are rejected.
`base_bond_count` still describes periodic connectivity; `bond_count` describes
displayed bonds. See [cell-line behavior](cell-lines.md).

Faces and atom-colored bonds require the default Z-buffer backend. ORTEP/BALSAC
monocolor and dualtone retain their dither palette. Polygon limits, nonplanar
surfaces and transparency semantics are in the [face guide](polygon-faces.md).

FFmpeg defaults to OFF and is not linked by image-only builds. Install
`libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` to enable it.
The renderer calls the shared libraries directly; it does not need an external
`ffmpeg` process. MP4 requires even dimensions at most 4096 per axis, an opaque
background and the Z-buffer backend. H.264 requires libx264 in the linked FFmpeg
build; auto can fall back to MPEG-4 Part 2. Video uses lossy YUV 4:2:0.
Reaction playback visualizes a supplied path; it does not infer chemistry, atom
mapping or periodic unwrapping. See [movies](movies.md).

Image/movie output and report files are individually atomic. A late report or
hashing failure may follow a successful output replacement. Callers must check
exit status and hashes. Job execution remains sequential; no new concurrent-job
or filesystem-durability guarantee is introduced.

**Performance evidence.** The [feature optimization report](performance-features.md)
records 41 workflows with 30 paired measurements per executable. Representative
median CLI times fell 31.7% for an opaque polygon with labels, 15.3% for dashed
cell lines with white contours, and 24.7% for a polygon-cage orbit. All encoded
outputs, scene inspections, movie reports and independent decoded movie frames
matched. Gains depend on the workflow and measured WSL workstation. The
[subsequent code-polish review](code-polish.md) checked output parity across all
41 workflows and found no resolved timing change from the structural cleanup.

**Release checks.** The Release suite with optional movie/image codecs passed
21/21 CTest entries; the minimal optional-feature build passed 21/21; the
ASan/UBSan build including experimental Cairo passed 23/23. Unavailable features
use explicit skips in the minimal build. All 61 options-catalogue examples also
executed successfully using only the downloadable offline example kit.

```sh
cmake -S . -B /tmp/qasr-v0.6.0 -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FFMPEG=ON -DENABLE_JPEG=ON -DENABLE_WEBP=ON
cmake --build /tmp/qasr-v0.6.0 --parallel 8
ctest --test-dir /tmp/qasr-v0.6.0 --output-on-failure -j 4
python3 tools/check_documentation.py /tmp/qasr-v0.6.0/qasr
python3 tools/validate_deployment.py /tmp/qasr-v0.6.0 \
  --output artifacts/v0.6.0/deployment/report.json
```

Deployment validation passed installation, shared-library resolution, an
installed movie render, binary/source checksums, and clean-source Release and
optional-feature-free builds with 21/21 CTest entries each. All six HTML pages
also passed desktop/mobile browser checks using offline file URLs. The handbook checks release/version agreement, local links, all five
embedded/served schemas, every CLI control, all 26 rendered images, downloadable
recipes and the unchanged legacy source. Browser checks cover offline file URLs,
search/filter/deep links, mobile layouts and the no-JavaScript fallback. The
website source download omits the website to avoid recursive archives; full
source packages include it. Local logs and packages are under `artifacts/v0.6.0/`.
