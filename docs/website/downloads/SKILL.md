---
name: qasr
description: Use QASR to inspect atomic structures and render images, polygon faces, or optional MP4 movies. Apply when preparing molecular/crystal figures or automating structure rendering, rather than running chemistry simulations.
---

# QASR: Quick Atomic Structure Renderer

QASR is a headless C executable. It reads geometry and produces images or movies;
it does not calculate energies, reactions, atom mappings or molecular dynamics.

## Find the executable and check the input

Use `qasr` from PATH, or a checkout's `build/qasr` or `build-release/qasr`.
Check capabilities before choosing optional readers, codecs or movies:

```sh
qasr --version
qasr --build-info
qasr structure.xyz --no-user-config --inspect
```

Input detection uses file contents; `--format` selects a reader explicitly.
Supported geometry subsets include POSCAR/CONTCAR, OUTCAR, optional vasprun.xml,
XYZ/Extended XYZ, Gaussian Cartesian input/orientation output, and AMS text
System blocks. CIF needs conversion to a supported format first. Input paths
refer to actual files. Inspect element identities, cell vectors, `pbc` and
frame counts before selecting replication or trajectory playback.

Use `--frame final` (default) or a zero-based frame index for a still image.
`--recover` permits an unfinished OUTCAR with a complete snapshot; malformed
geometry still fails. `--species Si,O` supplies identities for unlabeled inputs.

## Render a figure

```sh
qasr structure.xyz --no-user-config --style illustrated \
  --width 768 --height 768 --cell off -o structure.png
qasr CONTCAR --no-user-config --view top --expand-xy 1,1 \
  --image-colors greyscale --labels element --legend on -o surface.png
```

`--view top` requires independent a/b cell vectors; use `--view 1,1,1` or another
numeric direction for molecules. `--up X,Y,Z` controls in-plane orientation and
must not be parallel to the view. `--cell off` removes cell/vacuum extents from
camera fitting. Default output is 512×512 with 2× supersampling. Use
`--aa native` for previews or `--aa 4x` for more sampling.

Styles are `shaded`, `illustrated`, `ortep` and `balsac`. ORTEP draws spherical
arcs, not thermal ellipsoids. ORTEP/BALSAC accept `--tone monocolor|dualtone`;
`color` uses the ordinary palette. The Z-buffer backend supports the full feature
set. The experimental Cairo painter has style, annotation and depth limitations.

PNG is always available. `.jpg`/`.jpeg` and `.webp` require the corresponding
build capabilities. JPEG needs opaque pixels; WebP preserves RGBA losslessly.
Quote shell colors, for example `--background '#ffffff00'`. PNG encoder/filter/
compression controls affect PNG output only.

## Bonds, cells and polygon faces

- `--bond-radius 0.075` sets radius in angstroms; diameter is twice the radius.
  Thickness does not change connectivity. `--bond-colors atoms` makes two
  continuous halves meet at the midpoint using endpoint atom colors.
- Both endpoint atom instances must be displayed for a bond to appear. There is
  no override. `--repeat 2,2,1` or `--expand-xy 1,1` includes neighbors and their
  connecting bonds. Bonds beyond the outer displayed context are omitted.
  Nontrivial repeat and centered expansion cannot be combined; only periodic
  axes can replicate.
- `--cell-style dashed --cell-color '#000000' --cell-outline-width 1.5` adds
  black dashes with a white contour. Cell width/dashes/contours are output pixels;
  outline width is per side. Set `--cell-outline-width 0` to remove the contour.
- `--faces faces.json` fills user-supplied ordered loops. One independent face is
  sufficient; QASR does not infer zeolite cages or tilings. For Si–O rings with
  visible oxygen atoms, include the bridging oxygen positions in the boundary.

A face file contains a `faces` array, for example:

```json
{"faces":[{"vertices":[[0,0,0],[3,0,0],[3,3,0],[0,3,0]],
"coordinates":"cartesian","color":"#54b6d880","outline_width":0.8}]}
```

Use 3–64 boundary vertices and omit a duplicate closing vertex. Cartesian
vertices use angstroms; fractional vertices require a full cell. Alternatively,
use zero-based `atom_indices` into the selected source frame, with optional
per-vertex `cell_offsets` for periodic images. Atom-based vertices use wrapped
primary-cell coordinates. Nonzero offsets require a periodic axis.

Faces translate as whole polygons with repetition, without cell clipping.
Alpha controls translucency; outlines follow the supplied boundary, not internal
triangles. Concave simple loops work; degenerate/self-intersecting projections
fail. Faces blend with one another and the nearest atom/bond surface; they do not
provide transparent-atom volume rendering. Faces and atom-colored bonds require
the Z-buffer. Validate indices and triangulation with input plus
`--faces faces.json --validate-config` before a large render.

## Recipes, jobs and reproducibility

Use `--no-user-config` for reproducible examples unless personal preferences
are part of the requested result. Settings resolve as built-ins → personal
defaults → explicit recipe/job → CLI overrides. JSON objects merge by field;
arrays replace. Unknown/duplicate keys fail. `--config` and `--job` are mutually
exclusive; both can read one JSON object from stdin using `-`.

```json
{"image":{"style":"illustrated","width":768,"height":768},
"geometry":{"bond_radius_A":0.075,"bond_colors":"atoms"},
"overlays":{"cell_style":"dashed","cell_outline_width":1.5}}
```

Save as `recipe.json` and use `qasr INPUT --config recipe.json -o figure.png`.
Element palettes/radii, pair cutoffs and atom overrides are JSON controls.
Recipes accept root-level `faces` or `faces_file`, never both in one layer.
Structure-specific faces/atom rules belong in explicit recipes/jobs, not personal
defaults. A full job puts `input`, `output`, `frame`, `threads`, `report` and
`movie` beside its embedded `config` object.

Config/job file paths resolve against the containing file; stdin and worker
paths use the working directory. `--dump-config` returns an envelope; extract
its `config` member to reload as a recipe. Discover the five schemas with
`--schema config|user|job|worker|faces`. Supply input with `--validate-config` for
atom/face checks. `--inspect-scene` exposes displayed instances, bonds and faces.
`--json-errors` gives structured failures; check exit status.

`--report result.json` records settings, counts, hashes and timings. Output and
report files commit individually: a late report failure can follow a successful
image/movie replacement. Images and movies are written to files, not stdout.

## Movies and batch rendering

Movie capability requires a build with `ENABLE_FFMPEG=ON`. QASR calls shared
FFmpeg libraries directly; no external ffmpeg process or PNG sequence is needed.
MP4 requires an opaque background, even dimensions at most 4096 per axis and
the Z-buffer backend. `--movie-codec auto` prefers libx264 H.264, then MPEG-4;
explicit `h264` needs libx264 in the linked build. Video uses lossy YUV 4:2:0.

```sh
qasr structure.xyz --no-user-config --cell off --movie orbit \
  --movie-frames 120 --movie-fps 24 -o orbit.mp4
qasr reaction.xyz --no-user-config --cell off --movie trajectory \
  --movie-interpolate 3 --movie-fps 24 -o reaction.mp4
```

Trajectory mode accepts XYZ/Extended XYZ, OUTCAR or XML snapshots with stable
atom count/order/elements and cell/PBC identity. Cell vectors may vary. Its
camera fits all poses once and stays fixed. Interpolation is Cartesian/linear;
it does not infer chemistry or unwrap periodic boundaries. Trajectory mode uses
all source frames and rejects `--frame` and orbit-only controls.

For repeated views, start `qasr --batch --cache-mib 64 < requests.jsonl`.
Each nonblank line is a request; responses are JSONL and renders go to files:

```json
{"id":"view-1","op":"render","job":{"input":"CONTCAR","output":"view-1.png","config":{"camera":{"view":"top"}}}}
{"id":"cache","op":"stats"}
```

Other operations are `validate` and `clear_cache`. `--batch` must be the first
argument; per-render controls go inside `job`. Workers skip personal defaults
and process requests sequentially. Camera changes can reuse the scene; geometry
changes invalidate it. Movie jobs evict the still-image cache. Use distinct
output paths for separate processes. `--threads auto` is conservative; benchmark
representative inputs with `--benchmark-stages` before increasing threads.

## Build and further reference

In a source checkout, a CMake Unix Makefiles build supports system-wide install:

```sh
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build --parallel
cd build
sudo make install
```

This installs `/usr/local/bin/qasr`, presets/schemas under `/usr/local/share/qasr/`
and this guide under `/usr/local/share/doc/qasr/`. Optional movie, JPEG and WebP
libraries need their CMake switches enabled before building. Check the installed
capabilities with `qasr --build-info`.

Use the [handbook](https://ifilot.github.io/qasr/) for copyable recipes and all
controls. A source checkout or installed documentation also provides
`docs/input-formats.md`, `docs/polygon-faces.md`, `docs/cell-lines.md` and
`docs/movies.md`. Prefer the executable's help/schema for the installed version.
