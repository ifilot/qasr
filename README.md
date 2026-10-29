# vasp-render

Version **0.2.0**, licensed under **GPL-3.0-only**. See [CHANGELOG.md](CHANGELOG.md),
[LICENSE](LICENSE), and [third-party notices](THIRD_PARTY_NOTICES.md).

Headless C11 VASP geometry-to-PNG renderer. This release builds on the
v0.1.0 reference checkpoint with validated input,
periodic ball-and-stick geometry, and an orthographic software depth-buffer
renderer with shaded spheres/capped cylinders and a PNG output path.

Unreleased work after the v0.2.0 tag adds M7 streaming `vasprun.xml` input;
see [XML selection, validation and measurements](docs/m7-xml.md).

## Build and test

Requirements: Linux, a C11 compiler (GCC or Clang), CMake 3.16+, libpng/zlib
and libdeflate development packages, pkg-config, the compiler's OpenMP runtime,
and Python 3 for tests. M7 builds additionally use libxml2 2.9+ development
headers; disable XML with `-DENABLE_XML=OFF`. JSON parsing uses vendored
MIT-licensed cJSON 1.7.19. No network access is needed to build or test once
system dependencies are installed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Build without test/Python requirements with `-DBUILD_TESTING=OFF`.
For a libpng-only, single-thread build, use
`-DENABLE_LIBDEFLATE=OFF -DENABLE_OPENMP=OFF`. `--build-info` reports compiled
capabilities. Cairo is an optional experiment and is disabled by default.

For an optimized build and installation:

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-release
cmake --install build-release --prefix /path/to/install
/path/to/install/bin/vasp-render --version
```

Installed presets live in `/path/to/install/share/vasp-render/presets`;
pass their path with `--config`. The executable uses shared system libpng/zlib,
libdeflate, OpenMP and C/math runtimes in the default build. Installation includes the project license and upstream
notices under `share/doc/vasp_render` (or the platform's CMake doc directory).

To run memory and undefined-behavior checks:

```sh
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

LeakSanitizer requires process inspection. In a sandbox using ptrace, run
the sanitizer suite outside that restriction for leak checking. Setting
`ASAN_OPTIONS=detect_leaks=0` retains address/undefined-behavior checks but
does not check leaks.

## Inspect geometry

```sh
build/vasp-render tests/fixtures/POSCAR.skew --inspect
build/vasp-render tests/fixtures/OUTCAR.relax --inspect --frame 0
build/vasp-render OUTCAR --inspect --recover
build/vasp-render vasprun.xml --inspect --frame final
build/vasp-render POSCAR --inspect --species Si,O --config preset.json
build/vasp-render POSCAR --inspect-scene --repeat 2,2,1
```

Input is detected by content; `--format poscar|contcar|outcar` overrides it.
Compressed files must be decompressed before parsing. Frames are zero-based
ionic snapshots; the default is `final`. An unfinished OUTCAR requires
`--recover`, which warns on stderr. Malformed geometry is always an error.
XML accepts `--format xml` or content detection and requires a complete
document. `--frame N` counts ionic structures; `initialpos`, primitive cells
and SCF structures are excluded. `final` prefers `finalpos` when present.
XML does not support `--recover`; see the M7 notes for final-only semantics.
VASP 4-style POSCAR needs an explicit ordered `--species` mapping or JSON
`species` array. Comments never supply element identity.

`--inspect` reports lattice rows and Cartesian coordinates in angstroms,
source atom indices, complete frame count, recovery status, and requested
render settings (element/pair entries contain explicit overrides).
It preserves unwrapped source positions. `--inspect-scene` reports the wrapped
and repeated sphere instances, periodic bonds clipped to the supercell,
cell edges, effective colors/radii, and fitted camera. Output goes to stdout;
diagnostics and optional `--benchmark-stages` timings go to stderr. Stage
timings exclude process startup and inspection output.

## Render PNG

```sh
build/vasp-render POSCAR -o structure.png
build/vasp-render CONTCAR -o repeated.png --repeat 2,2,1
build/vasp-render CONTCAR -o detail.png --cell off --view 1,-1,0.7
build/vasp-render OUTCAR -o final.png --background '#ffffff00' --benchmark-stages
```

Default output is 512×512 with 2× supersampling and ambient/diffuse
lighting. Orthographic auto-fit includes all spheres, bond widths, and the cell
when enabled. Turning the cell off fits the geometry without slab vacuum.
Camera `--view` points from scene center toward the camera; `--up` sets vertical.
The output is encoded to a unique neighboring temporary file and replaces its
destination only after successful encoding and close. Errors preserve an
existing destination and remove temporary output.

Cell boundaries default to 2 output pixels. Set `--cell-width 4` or JSON
`"overlays": {"cell_width": 4}` to adjust them (positive values up to 32).
The illustrated preset uses 3-pixel boundaries. Auto-fit reserves space for
the selected width; `--cell off` hides the boundaries.

## Illustrated top views

```sh
build/vasp-render CONTCAR -o illustrated.png --config presets/illustrated.json
build/vasp-render CONTCAR -o top.png --style illustrated --view top
build/vasp-render CONTCAR -o outlined.png --config presets/illustrated.json --outline-width 2
python3 tools/render_examples.py build-release/vasp-render
```

`--style illustrated` uses flat colored disks, darker rims, pale circular
highlights, solid gray bonds and black atom/bond contours. The original smooth
lighting remains available as `--style shaded` (the default). Geometry and
occlusion use the same depth intersections in both styles. `--outline-width`
sets the illustrated contour width in output pixels, from 0 (off) to 8; the
default is 1 pixel. The illustrated slab preset uses 1.5 pixels.

`--view top` looks down the surface normal `a × b`, choosing its sign toward
the third lattice vector. The first lattice vector points right in the image;
an explicit `--up` overrides that alignment. This works for tilted and skewed
cells. The same choices are supported as JSON `image.style`,
`image.outline_width` and `camera.view: "top"`.

The illustrated preset uses a white background, 768×768 images, visible unit-cell boundaries,
and a curated display palette/radii for the supplied slabs. Its
`image.depth_fade: 0.9` softens deeper layers toward white; 0 disables fading.
Contours stay black. These are visual settings; all atoms and the Atom
Architect bond rules are retained. Running `tools/render_examples.py` creates
the local `artifacts/illustrated-top/README.md` gallery for all 20 CONTCAR samples,
with source/preset hashes and camera vectors in its manifest. Generated
artifacts are ignored by Git and are not included in release source archives.

Colors, display radii for all 118 elements, and pair cutoff defaults come from
the pinned [Atom Architect parameter snapshot](vendor/atom-architect/README.md).
These replace M0's provisional preset and include the materials in your slabs.
JSON overrides remain available. Boundary-crossing bonds stop at the repeated
cell; outside neighbor atoms are not added. The neighbor search accounts for
arbitrary skew within explicit work/resource limits, including nonzero-image
self bonds. `--labels element|index`, `--axes on`, `--legend on` and
`--scale-bar on` add annotations. Labels use depth-tested front-surface anchors;
indices are zero-based rendered atom-instance IDs. The legend and footer have
reserved space outside the fitted geometry. [M6 notes](docs/m6-validation.md)
describe units, layouts, limits and validation.
`--threads auto` uses up to eight available OpenMP workers at 512×512 or larger
(by pixel count), and one worker below that threshold. OpenMP environment limits
are respected. `--threads 1` forces the serial path; explicit counts support
repeatable scaling tests. Every tile preserves primitive order and sample depth.

Transparent backgrounds and premultiplied AA edge filtering are supported.
For workstation experiments, `--aa native|2x|4x|adaptive` (JSON `image.aa`) selects the
sample grid. Native uses one sample per pixel; 4× provides a higher-quality
reference at sixteen samples per pixel. Adaptive uses one sample in sphere
interiors with proven surface-depth separation, and four samples near silhouettes,
illustrated color-band boundaries and ambiguous overlap. The default remains 2×.

The default PNG encoder is libdeflate with no filtering. Compression `auto`
selects level 0 on native Linux storage and level 1 on the measured WSL 9p
Windows-mounted filesystem. Level 0 prioritizes latency and writes about 1 MiB
at 512×512; `--png-compression 1` gives smaller compressed files on any storage.
`--png-encoder libpng` selects the reference codec. Tuning flags are
`--png-compression auto|0..9` and `--png-filter adaptive|none|sub|up`; JSON keys
are `image.png_encoder`, `image.png_compression`, and `image.png_filter`.

An optional painter prototype can be built with
`-DENABLE_CAIRO_EXPERIMENTS=ON` (system Cairo development files required) and
selected with `--backend cairo` (JSON `image.backend`). Its native mode provides Cairo edge coverage.
It fails the interpenetrating-surface depth gate and is retained for experiments.
The default `--backend zbuffer` keeps per-sample surface depth.
Element alpha blends the nearest surface over the background; layered
transparent geometry is not implemented. Fully transparent primitives are skipped.

Configuration precedence is built-in defaults, JSON, then CLI regardless of
argument order. Unknown keys, duplicate keys/options, conflicting element
identity, invalid types, NaN/infinite values and overflow are errors. JSON
supports `image`, `camera`, `geometry`, `elements`, `bond_cutoffs_A`, `overlays`,
and `species`. See [the design](docs/design.md) for schema, exact limits,
exit codes, frozen M0 choices and the remaining milestones.

Example configuration:

```json
{
  "image": {"width": 640, "height": 480, "background": "#ffffff"},
  "geometry": {"repeat": [2, 1, 1]},
  "elements": {"Si": {"color": "#e7bd71", "radius_A": 0.65}},
  "bond_cutoffs_A": {"Si-O": 2.0},
  "overlays": {"cell": true, "labels": "none"}
}
```

`--help` lists current options. The 1 GiB allocation budget includes scene and
supersampling buffers. Very skewed cells or excessive cutoffs can hit the
200-million-candidate work limit and fail explicitly.

Fixture-backed OUTCAR coverage includes VASP 5.2.11/5.2.12 and 6.2.0;
[fixture provenance and coverage gaps](tests/fixtures/README.md) distinguish
real outputs from synthetic tests. General VASP 5/6 and real MD compatibility
are not yet established.

The regression suite also includes the 20 user-provided slab CONTCAR examples
in `tests/fixtures/slabs`, preserving their original manifest and hashes.

The base suite has 63 test groups across six CTest entries; enabling Cairo
adds three painter experiments and a colored crossing-cylinder depth gate
across two more CTest entries. Geometry is compared
with a brute-force periodic-image oracle; raster output is compared with an
independent per-ray reference for crossing cylinders, caps, near-parallel axes,
and cell occlusion. Fixed-environment decoded-RGBA hashes are recorded in
`tests/golden_pixels.json`. Tests also cover transparent edges, determinism,
resource limits, atomic replacement/cleanup, and PNG rendering of all 20 slabs.
M7 adds 19 XML test groups in one additional CTest entry; XML-disabled builds
run the capability check and skip the 18 XML-specific groups. The XML network
denial test uses a local HTTP sentinel and needs permission to bind localhost.

## Workstation handoff and release packages

v0.1.0 is the pre-optimization release checkpoint. v0.2.0 has
completed M4–M6 on the accepted WSL workstation: renderer/AA/encoder/thread
selection, annotations, deployment validation and scoped performance acceptance.
[Performance notes](docs/performance.md) retain the
fresh shaded/illustrated baselines, rejected candidates and measured adaptive
results separately from historical laptop observations. The accurate Z-buffer
is selected with cached tile parallelism and filesystem-aware PNG settings;
uniform 2× remains the default and adaptive AA is available for profiling.

For Linux-storage measurements on WSL, run:

```sh
cmake -S . -B /tmp/qasr-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build /tmp/qasr-release
python3 tools/benchmark.py /tmp/qasr-release/vasp-render --work-dir /tmp/qasr-bench
```

Prepare local release archives after building:

```sh
python3 tools/package_source.py
cpack --config /tmp/qasr-release/CPackConfig.cmake -B artifacts/releases
```

The source archive has a SHA-256 file and a per-file manifest. Its explicit
allowlist includes source, presets, docs, tests and vendor snapshots, excluding
artifacts, builds, Git metadata and local agent/credential configuration.
The Linux binary archive includes the executable, presets, documentation and
license notices; matching system runtime libraries must be installed.

See [v0.2.0 release notes and validation](docs/release-v0.2.0.md) for current
scope, checks and limitations. The [v0.1.0 notes](docs/release-v0.1.0.md)
retain the reference checkpoint history.
