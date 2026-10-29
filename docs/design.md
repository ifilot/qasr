# Technical Design v0.1 — Headless VASP Geometry-to-PNG Renderer

**Status:** v0.1.0 is the M0–M3 reference checkpoint on 2026-10-29. Published v0.2.0 completes M4–M6. Subsequent unreleased M7 adds validated streaming XML input. The user accepts WSL results for performance selection and deployment validation; native Linux/HPC access is not an acceptance requirement. Sections 16, 18–25 record decisions and validation.

**Language:** C11. **Platform:** Linux workstations and HPC nodes. **Interface:** standalone CLI. **Default image:** 512 × 512 PNG.

**Primary objective:** minimize end-to-end latency for one PNG while maintaining correct geometry and acceptable visual quality.
Implementation lives in `src/`; build and inspection instructions are in `README.md`.

## 1. Executive summary

Build a headless C executable, provisionally named `vasp-render`, that reads VASP geometry from **POSCAR, CONTCAR, OUTCAR, or optionally `vasprun.xml`** and produces an orthographic, automatically framed, shaded ball-and-stick PNG. XML was deferred through v0.2.0 and is implemented by M7. XDATCAR and other VASP data formats are explicitly out of scope. Typical input structures contain 100–1,000 atoms. The pipeline implements correct handling of skewed cells and periodic bonds, configurable cell repetition, deterministic view settings, and strict validation.

Rendering backend, anti-aliasing method, PNG encoder, and parallelism are **benchmark decisions** rather than fixed technology choices. Compare a Cairo painter's-algorithm implementation with an accurate software depth-buffer implementation. Select the fastest method that passes visual correctness tests.

## 2. Requirements and boundaries

### Confirmed behavior

- Required formats: POSCAR, CONTCAR, OUTCAR. POSCAR and CONTCAR use one parser. Optional M7 format: `vasprun.xml` using streaming libxml2 (`ENABLE_XML`, enabled by default).
- In multi-step OUTCAR inputs, use the **last complete ionic geometry** by default; `--frame` selects a specific geometry. A frame is an ionic coordinate snapshot, *not* an electronic SCF iteration.
- Ball-and-stick: shaded spheres, shaded cylindrical-looking bonds, directional lighting and anti-aliased boundaries.
- Element-pair-specific bond cutoffs, with periodic-bond support.
- Repeat cell using `--repeat Nx,Ny,Nz`, default `1,1,1`.
- Orthographic projection only; auto-fit preserves image aspect ratio; deterministic camera with optional `--view` and `--up`.
- Built-in atom colors/radii and configurable per-element colors/radii, sphere scale, bond radius, and background.
- Unit-cell outline enabled by default; optional atom labels, indices, axes, legend, and scale bar.
- Default 512×512 output. Configurable via CLI/JSON.
- Built-in settings < optional JSON config < explicit CLI overrides. Reject unknown keys and invalid values.
- Standalone executable only; no GUI, display server, public C library API, or persistent worker protocol.
- Standard system libraries are acceptable, dynamically linked where convenient.
- Strict errors, nonzero exit status, diagnostics on stderr, and no partial destination PNG on failures.
- Deterministic pixel output in a fixed software environment; cross-platform byte-identical PNG streams not required.
- Establish target latency **after** measuring a reference implementation.

### Out of scope for v1

- XDATCAR, CHGCAR, WAVECAR, vaspout.h5, volumetric fields, isosurfaces, electron densities, interactive manipulation, GPU requirement, energy/force visualization, animations, and multi-image batch service.
- Inferring chemical bond order: cutoff-derived bonds represent visualization connectivity only.
- Guaranteeing that every OUTCAR from every historical VASP version is understood without versioned fixtures.

## 3. End-to-end architecture

```text
CLI + JSON config
      |
      v
Input sniffing / explicit format
      |
      +-- POSCAR/CONTCAR parser
      +-- OUTCAR streaming geometry parser
      +-- [optional] vasprun.xml streaming reader
      |
      v
Normalized Structure: species, positions, lattice, frame metadata
      |
      v
Repeat cells + periodic bond construction + scene primitives
      |
      v
Camera orientation + orthographic auto-fit + clipping
      |
      +-- Cairo painter path [prototype]
      +-- software depth-buffer path [prototype]
      |
      v
Anti-aliasing and RGBA pixel buffer
      |
      v
PNG encoder [benchmarked candidates]
      |
      v
Temporary output, validated close, atomic rename where feasible
```

Internal modules (not exposed as public ABI): `main/cli`, `config`, `elements`, `parse_poscar`, `parse_outcar`, optional `parse_xml`, `structure`, `periodic`, `bonding`, `camera`, `scene`, `render_cairo`, `render_zbuf`, `aa`, `png_io`, `diagnostics`, `bench_support`.

## 4. Core data structures

- `Vec3` and `Mat3` using `double` for input coordinates and geometry calculations; image-space arithmetic may use `float` after stable transformation.
- `Element`: atomic number, symbol, default display color, default sphere radius.
- `Atom`: element ID, Cartesian position (Å), optionally fractional position and stable atom index.
- `Structure`: three **row/column convention explicitly documented** lattice basis vectors (Å), atom count, contiguous atom array, source-format ID, ionic-frame index and optional source metadata.
- `BondRule`: normalized unordered element pair `(min(Z1,Z2), max(Z1,Z2)) -> cutoff_A`, stored in a compact lookup table. Missing built-in/override pairs produce no bond (section 18).
- `AtomInstance`: source atom index, lattice translation (three integers), generated Cartesian coordinate, stable instance ID.
- `BondInstance`: endpoints referring to atom instances or periodic images; clipping semantics specified below.
- `Camera`: normalized view direction, orthogonal up/right basis, orthographic scale, 2D translation, near/far bounds.
- `Scene`: sorted or rasterized sphere, finite-cylinder, lattice-line and overlay primitives; contains stable IDs for deterministic tie-breaking.
- `Image`: width, height, pixel format, stride, pixels; optional depth buffer for Z-buffer backend.

Use bounded dynamic arrays, checked integer multiplication, and explicit limits on atoms, expanded instances, bonds, frame number, resolution, and input line length.

## 5. File parsing

### POSCAR and CONTCAR (required)

One finite-state parser supports: title; universal scale (including VASP volume-scaling conventions and, where applicable, three-component scaling); lattice vectors; species names and population counts; optional selective-dynamics line; direct or Cartesian coordinates; optional trailing velocities ignored if not needed. Preserve species-to-atom ordering. Validate count agreement, finite numbers, nondegenerate lattice, and coordinate conversion. VASP 4-style inputs lacking element symbols require an explicit or reliably derivable species mapping; **never guess chemical elements from arbitrary comments**.

### OUTCAR (required)

A line-oriented streaming state machine scans for reliable species/count metadata, actual lattice vectors, and complete `POSITION` / `TOTAL-FORCE` ionic coordinate tables. Record the lattice that applies to each snapshot; this is essential for cell relaxation. Maintain only the current candidate and last **complete, internally consistent** snapshot, plus metadata. Exclude SCF iterations from frame counting. For `--frame n`, retain the requested zero-based geometry frame while continuing file validation under the chosen policy. An incomplete trailing position table must never replace the last complete snapshot. Distinguish (a) a recognized completed run, (b) an unfinished file with a recoverable complete snapshot, and (c) malformed geometry. Reject (b) by default; `--recover` explicitly accepts it with a warning. See section 18 for the precise supported footer and recovery rules.

Important: OUTCAR often includes multiple coordinate tables and evolving lattice descriptions; do not determine the final structure simply by taking the last numeric-looking three-column block. Treat parsing as a dedicated correctness-sensitive subsystem.

### vasprun.xml (optional, separate milestone)

Use libxml2 streaming reader, selecting complete `<structure>` elements with positions and corresponding basis, honoring final structure semantics. Retain bounded geometry state. Disable external entity/network processing and enforce size/depth limits. XML support is not a release-blocking requirement for the initial version.

### Format selection

Auto-detect by recognizable content with explicit `--format poscar|contcar|outcar|xml` override. Do not rely solely on filename. Report ambiguity and unrecognized formats clearly.

## 6. Periodic geometry and bonding

Normalize positions and lattice transformations into a consistent internal Cartesian convention. A repeated cell contains lattice translations `(i,j,k)` with `0 <= i < Nx`, etc. Cell outlines use the repeated supercell basis. Do not duplicate whole atom records until scene construction requires visible instances.

Bond existence is determined by **element-pair-specific cutoff distances in Å**. Evaluate eligible periodic image displacements, not merely naive Cartesian distances inside the base cell. For a skewed lattice, searching only translations `-1,0,1` in fractional axes is not universally sufficient; enumerate candidates using conservative distance bounds from the lattice metric / reciprocal basis, or a proven periodic neighbor-search algorithm. Use a distance lower bound to eliminate candidate images early. Deduplicate bonds via ordered `(source atom IDs, integer image displacement)` keys; exclude zero-displacement self-bonds and impose an explicit policy for same-atom periodic-image bonds.

For the repeated scene, show bonds crossing a supercell boundary up to the cell boundary, without drawing disconnected neighbor atoms beyond the requested repetition. Clip against the skewed supercell parallelepiped, not its Cartesian bounding box. Ensure the unit-cell outline does not incorrectly obscure depth-tested geometry.

For ~100–1,000 atoms, test direct pair search against cell lists / spatial hashing. Avoid implementing neighbor-list complexity until benchmarks show it helps; `O(N^2)` may suffice at small N but can become costly after repetition.

## 7. Camera and framing

Orthographic view only. Default orientation is a deterministic, documented vector (proposal: `(1,1,1)` in Cartesian axes), with `--view` and `--up` overriding orientation. Normalize and validate inputs; reject zero vectors and collinear view/up pairs. Use a stable right-handed camera basis. Compute projected extents **after scene construction**, including atom radii, cylinder widths and optionally cell-line thickness, then calculate a uniform scale and centered offset to fit within a configurable pixel margin. Apply the same algorithm across both backends. No automatic viewpoint optimization or perspective rendering.

## 8. Rendering backend experiments

### A. Cairo painter's algorithm

Project and sort primitives back-to-front with deterministic tie-breaking. Approximate shaded spheres with cached or procedural gradient sprites / radial shading, and bonds with gradient-filled polygons or layered strokes. Cairo handles anti-aliased shapes and image output. Document unavoidable failures for crossing cylinders, interpenetrating geometry, and depth changes along long bonds.

### B. Software Z-buffer

Rasterize projected spheres and finite cylinders using per-pixel ray/implicit-surface intersections, or an equivalent well-tested rasterization method, computing surface depth and directional diffuse/ambient lighting. Test sphere/cylinder occlusion at the pixel level. Clip cylinder endpoints at sphere surfaces where visually appropriate. Maintain depth per sample or depth-aware edge coverage to avoid background halos. Use tile-based spatial bins if it reduces pixel-primitive tests. A depth buffer is the correctness baseline for general overlapping geometry.

### Backend choice

Correctness gates first, then end-to-end latency on defined representative cases. Do not select the painter path solely because it is faster on scenes where it renders incorrect overlap. Retain both as benchmark prototypes until a release choice is made.

## 9. Anti-aliasing experiments

Compare (1) native-resolution edge coverage; (2) 2× linear-axis supersampling, rendering at 4× source pixel count and downsampling; (3) adaptive edge sampling. For adaptive AA, define which silhouette/occlusion pixels receive additional samples and how depth is resolved at each sub-sample. The same scene and camera must be used across comparison runs. Evaluate silhouette quality, depth-edge halos, consistency of bond widths, CPU cycles, and wall time. Use a higher-quality reference output for toleranced similarity measures; avoid treating a particular backend's image as unquestionable ground truth.

## 10. PNG encoding and I/O

Compare Cairo PNG writing, libpng with multiple compression/filter configurations, and a separately evaluated fast encoder. Use identical RGBA input buffers where possible to isolate codec performance; also measure actual integration overhead. Preserve alpha if transparency is selected. Benchmark output file bytes as secondary information, not the primary metric.

Write to a unique temporary file in the target directory and rename only after successful encode/close, cleaning up on error. Consider filesystem-dependent atomic-rename behavior and cross-filesystem limitations. Report I/O errors clearly. Make optional `--output -` / stdout support a future decision rather than silently requiring it.

## 11. CLI contract (proposed)

```text
vasp-render INPUT -o OUTPUT.png [options]
  --format auto|poscar|contcar|outcar|xml
  --frame final|N
  --width 512 --height 512
  --repeat NX,NY,NZ
  --view X,Y,Z --up X,Y,Z
  --config preset.json
  --sphere-scale F --bond-radius ANGSTROM
  --background COLOR
  --cell on|off
  --labels none|element|index
  --axes on|off --legend on|off --scale-bar on|off
  --threads auto|N
  --benchmark-stages
```

`N` is zero-based; `final` is the default, and negative indices are rejected. POSCAR/CONTCAR accept only `final` or `0`. M1 additionally exposes `--inspect`, `--species Si,O`, and `--recover`. M7 adds `--format xml` and content detection, ionic-frame selection and finalpos precedence; XML requires a complete document and does not support recovery. Section 25 records the exact selection rules. Runtime backend/AA/encoder overrides support the documented experiments.

### JSON configuration

```json
{
  "image": {"width": 512, "height": 512, "background": "#ffffff"},
  "camera": {"projection": "orthographic", "view": [1, 1, 1], "up": [0, 0, 1], "margin": 0.06},
  "geometry": {"repeat": [1, 1, 1], "sphere_scale": 1.0, "bond_radius_A": 0.12},
  "elements": {"Si": {"color": "#e7bd71", "radius_A": 1.1}},
  "bond_cutoffs_A": {"Si-O": 2.0, "C-H": 1.3},
  "overlays": {"cell": true, "labels": "none", "axes": false, "legend": false, "scale_bar": false}
}
```

The schema above is implemented in M1; optional top-level `"species": ["Si", "O"]` supplies an ordered element mapping. Section 18 defines the initial visualization preset. Validate all keys, types, finite values, positive dimensions, element names, conflicting overrides, and integer overflow. Duplicate keys, including reversed duplicates such as `Si-O` and `O-Si`, are errors.

## 12. Reliability and reproducibility

Parsing errors and invalid settings use nonzero exit codes and structured, concise stderr diagnostics (file, format, line/frame, reason when known). Do not silently switch formats or omit atoms. Pixel reproducibility is required for identical inputs/options with fixed compiler, platform, dependencies, and thread configuration. Preserve deterministic iteration order, stable bond ordering, explicit rounding/raster tie rules, and fixed palette/shading defaults. Avoid nondeterministic concurrent writes to image/depth buffers.

Proposed error categories: CLI/config error; unsupported format; invalid file structure; missing/ambiguous chemical species; invalid geometry; rendering/overflow error; output I/O error; internal error. Exact integer codes remain a design detail.

## 13. Performance and benchmarks

**Primary metric:** elapsed wall-clock time from process launch to successful closure of the PNG, including parsing, startup, scene build, rasterization, encoding, and file output. Report median and p95, sample count, hardware, CPU governor, filesystem, compiler flags, dependency versions, and input file hash. Keep isolated stage timings in addition to full-process timing. Separate warm-cache from cold-cache measurements; do not claim OS-cold-cache results unless cache state is controlled. Preserve images for correctness review before accepting speed measurements.

Benchmark matrix:

- Typical atom counts: 100, 500, 1,000. Optional stress cases beyond 1,000 and supercell expansion.
- File formats: POSCAR/CONTCAR; static, relaxing, and very large OUTCAR; optional XML when implemented.
- Geometries: cubic and skewed lattices, slabs/vacuum, low/high coordination, overlapping projected atoms, crossing bonds, periodic boundary cases.
- Repeats: 1×1×1, 2×2×1, and 2×2×2 as feasible.
- Rendering: painter vs Z-buffer; native coverage vs 2× supersampling vs adaptive sampling; codec variants; 1, 2, 4, 8 threads on compatible backend.
- Collect size/RSS memory, major allocations, encoder output size, and correctness score where practical.

Begin with an end-to-end reference baseline; only then set numeric latency objectives. Threading may be harmful for small 512×512 renders, so enable adaptive or fixed thread counts only if measurements justify them. Avoid benchmarking every Cartesian product of settings initially: use staged experiments to eliminate weak candidates.

## 14. Test and validation suite

CTest or equivalent automated testing framework, including:

1. **Parser fixtures:** POSCAR/CONTCAR with direct/Cartesian, scaling variants, selective dynamics, mixed species, malformed headers and atom counts; OUTCAR static/relaxation/MD, changing cells, incomplete final tables, different known VASP versions, requested frame indexing.
2. **Geometry property tests:** coordinate conversion, non-orthogonal lattices, periodic image distances, bond-cutoff threshold boundaries, pair-rule symmetry, no duplicate bond segments, repeated-cell counts and bounds.
3. **Rendering correctness:** spheres/bonds and unit-cell outlines; occlusion scenes designed to make painter ordering fail; alpha edges, anti-aliasing, orientation and auto-fit. Use fixed-environment golden PNGs or decoded pixel hashes; cross-backend comparisons use reference/tolerance metrics.
4. **Reliability:** missing input, invalid JSON/flags, NaN/overflow, unreasonable image/repetition sizes, truncated inputs, unwritable destinations, cleanup, deterministic runs, memory/undefined-behavior sanitizers.
5. **Performance regression:** non-gating or statistically justified CI benchmarks; separate stable lab benchmarks from noisy shared CI runners.

Test all optional overlays even if their implementation is deferred; a feature becomes release-ready only after it passes relevant tests.

## 15. Proposed implementation milestones

- **M0 — Specification freeze:** finalize outstanding items below, assemble representative real VASP fixtures and reference structures, choose precise in-memory conventions.
- **M1 — Input and normalized geometry:** CLI/config validation, POSCAR/CONTCAR parser, OUTCAR streaming parser, frame selection, species mapping, unit tests.
- **M2 — Periodic geometry:** repetition, pair-specific cutoffs, accurate skewed-cell neighbor logic, deduplication, cell outline, coordinate camera/auto-fit.
- **M3 — Reference renderer:** single-threaded accurate depth-buffer spheres/cylinders, base lighting, PNG output, fixed-env golden tests; measure initial latency.
- **M4 — Rendering experiments:** Cairo painter prototype; native/adaptive/2× AA, compare correctness and timing.
- **M5 — Encoder and threading experiments:** optimized PNG encoder candidates and optional tile parallelism; select best end-to-end combination.
- **M6 — Production hardening:** overlays, robust error cleanup, representative deployment builds, documentation, performance report and targets. The approved deployment-validation environment is the WSL workstation (see `performance.md`).
- **M7 — Optional XML:** libxml2 streaming `vasprun.xml` support if prioritized; matching input/geometry fixtures and performance tests.
- **M8 — Periodic context:** centered xy neighbor cells, full primary-cell palette/outline and muted/greyscale image atoms, with skew-cell clipping and unchanged repeat defaults.
- **M9 — Proposed next iteration:** JSON user defaults, typed per-atom selectors and a discoverable machine contract; see `next-iteration.md`. Design only, not yet implemented.
- **M10 — Optional follow-on:** benchmark-driven batch/caching or a thin MCP adapter if repeated-input or agent integration workflows need it; no server requirement for the existing CLI.

Each milestone has explicit test evidence and benchmark output; do not postpone correctness gates until the end.

## 16. M0 decision record — 2026-10-29

1. **OUTCAR completeness:** Strict by default; explicit `--recover`, with a stderr warning and inspection metadata, accepts a complete snapshot from unfinished output. Malformed recognized geometry remains fatal.
2. **Frame numbering:** Zero-based nonnegative integers plus `final`; numeric selection still scans the whole file. Frame completeness requires the closing POSITION table separator.
3. **Element identity:** VASP 4-style POSCAR requires `--species` or JSON `species`. Explicit mappings must agree with existing named species/OUTCAR metadata. POTCAR-file loading is deferred; no comment-based guessing.
4. **Bond cutoff fallback:** M2 supersedes provisional `visual-v1` with the user-requested Atom Architect snapshot `atom-architect-9e73e510`, overlaid by explicit pair cutoffs. Its pair policy covers all elements; display radii do not infer connectivity. See section 19.
5. **Periodic edges:** Clip boundary-crossing bonds to the repeated skewed cell; no outside neighbor atoms. Allow same-source periodic-image bonds at nonzero displacements; canonicalize the first nonzero displacement component positive for deduplication. Exclude zero-displacement self-bonds.
6. **Defaults:** Camera, colors, display radii and lighting are specified in section 18. These are visualization choices rather than chemical accuracy guarantees.
7. **OUTCAR compatibility:** Initial fixture-backed versions are 5.2.11 and 5.2.12 (relaxation/static) and 6.2.0 (cell relaxation). The parser targets the common six-column POSITION tables in VASP 5/6. A synthetic MD stream tests frame handling; real MD and additional 5.4/6.x fixtures remain required before claiming those modes/versions as verified.
8. **Limits/portability:** Linux x86-64, C11 plus POSIX.1-2008, CMake 3.16+, GCC/Clang. See exact limits below. Dynamically linked system runtime is acceptable; portable static distribution and stdout PNG are deferred.
9. **XML:** M7 follow-on; excluded from initial release and M1.
10. **Performance environment:** Initial development measurements use the current Intel Core Ultra 7 258V / WSL2 Linux x86-64 host. Final speed selection requires controlled native Linux/HPC measurements at M3–M5, including filesystem/compiler/governor details. No latency target or winning backend is declared in M0/M1.

These choices permit implementation to proceed; benchmark outcomes and visual acceptance are still gates in their respective milestones.

## 17. Source references

- VASP Wiki, POSCAR: https://vasp.at/wiki/POSCAR
- VASP Wiki, OUTCAR: https://vasp.at/wiki/OUTCAR
- VASP Wiki, VASP Output: https://vasp.at/wiki/Output
- VASP Wiki, vasprun.xml: https://vasp.at/wiki/Vasprun.xml

These references describe file semantics; proposed renderer behavior, choices, and benchmarks above are design decisions rather than claims about VASP's own renderer.

## 18. Frozen conventions and implementation status

### Geometry and ownership

Store three lattice **row vectors** in angstroms, with `Cartesian[j] = sum(fractional[i] * lattice[i][j])`. M1 preserves source Cartesian positions without wrapping; M2 wraps fractional positions into `[0,1)` for periodic instance generation. Array offsets are stable zero-based source atom IDs. `Structure` owns its contiguous atoms; parsers transfer ownership only on success and leave an empty result on failure. No public library ABI is installed. M1 keeps at most two atom arrays for OUTCAR regardless of its frame count.

### Input policy

- POSCAR recognizes one nonzero scalar (negative means target volume) or three positive Cartesian-component scale factors. Cartesian positions use those same scale factors. Direct positions use the scaled basis. Selective flags must be T/F when present; trailing velocities/predictor data is ignored, while text resource limits still apply through EOF.
- Named POSCAR species must be exact chemical symbols. Coordinate mode uses the VASP first-character convention for D/d and C/c/K/k; arbitrary other modes are rejected. Fortran D/d exponents are supported. Hexadecimal floats, NaN and infinities are rejected.
- OUTCAR uses `TITEL`, `VRHFIN`, and/or repeated `POTCAR:` descriptors, checking agreement and counts. Known potential suffixes after `_` retain the element identity. Missing metadata may be supplied explicitly; conflicting metadata may not be overridden. Changing species populations and concatenated runs are unsupported errors.
- The latest complete lattice before a POSITION table belongs to that snapshot. A complete table has an opening rule, exactly the declared number of finite six-column position/force rows, and a closing rule. Other coordinate tables and SCF lines never count as ionic frames.
- For fixture-backed strict completeness, require `General timing and accounting informations for this job:` followed by a valid `Voluntary context switches:` field. This identifies completion of the recognized output layout, not scientific convergence. Unsupported footer variants require a fixture before relaxing this rule.
- Recovery permits missing footer or EOF within a recognized lattice/table after an earlier complete snapshot. A short numeric row without a terminating newline at EOF is incomplete. An invalid numeric token, wrong row count, or malformed recognized table remains fatal. A partial exponent token is rejected rather than guessed. Explicit frame selection must refer to a complete snapshot.
- Content detection recognizes a numeric POSCAR scale/basis prefix or a VASP version banner in the first 64 lines. Filename is irrelevant. Auto detection requires seekable input; `--format` supports streams through filesystem paths without rewinding. Unknown formats are rejected with no fallback parser attempt.

### Provisional visualization preset `visual-v1` (superseded in M2)

The palette, radii and pair table below record the original M0 choice. The user
subsequently selected Atom Architect as the parameter reference; section 19
supersedes those data. Camera, margins, lighting, bond radius and line width
remain as specified here.

Default view points from scene center toward the camera along `(1,1,1)`; up is `(0,0,1)`, projected perpendicular to view. Use 6% margin on each image side, white opaque background, sphere scale 1, bond radius 0.12 Å, and depth-tested cell outline. Lighting in camera coordinates: unit direction proportional to `(-1,-1,2)`, ambient 0.25 plus diffuse 0.75, no specular term. Initial cell line width is 1 output pixel. These preset details are specified now; M1 validates overrides without generating a scene.

| Element | RGB | Display radius (Å) |
| --- | --- | ---: |
| H | #eeeeee | 0.25 |
| C | #444444 | 0.45 |
| N | #3050f8 | 0.45 |
| O | #ff3030 | 0.40 |
| Si | #e7bd71 | 0.65 |
| Al | #bfa6a6 | 0.65 |
| Fe | #e06633 | 0.60 |
| Li | #cc80ff | 0.60 |
| Ni | #50d050 | 0.60 |
| S | #ffff30 | 0.55 |
| Other named elements | #a0a0a0 | 0.60 |

The selected display radii are deliberately independent of covalent radii. Versioned cutoff pairs in Å: H-H 0.9; C-H 1.3; C-C 1.9; C-N 1.8; C-O 1.8; N-H 1.3; N-N 1.7; N-O 1.7; O-H 1.2; O-O 1.7; Si-Si 2.8; Si-O 2.0; Al-O 2.3; Fe-O 2.4; Ni-O 2.3; Li-O 2.5; S-H 1.6; C-S 2.0; S-S 2.3. Bond acceptance is `0 < distance <= cutoff`, with finite-precision threshold tests in M2. This small preset is an explicit visualization policy, not a chemically validated universal bond model.

### Resource limits and errors

M1 enforces: 1,000,000 source atoms; 2,000,000 planned repeated instances; each repeat axis 1..1024; 10,000,000 ionic frames; 118 species groups; 64 KiB line content; 64 GiB input; 1 MiB config; JSON nesting 32; image axes 1..16384 and total pixels at most 67,108,864; explicit threads 1..256 (`auto` stored as 0). Zero-population species groups are allowed if total atoms is positive. Raw and scaled lattice row norms must lie within `[1e-12,1e9]` Å, with absolute normalized determinant above `1e-12`; Cartesian coordinate magnitude is at most `1e12` Å. Checked products precede allocations and repeat expansion. M2/M3 additionally enforce a 1 GiB aggregate scene/render allocation budget, including AA buffers and a PNG working-space allowance, and a 10,000,000 bond limit before allocation. Candidate and expansion work each have a 200,000,000 limit; reciprocal image bounds must fit within ±1,000,000 translations. Display/bond radii must be in `[1e-9,1e6]` Å, cutoffs at most `1e6` Å, and fractional coordinates at most `1e12` in magnitude before wrapping. Work limits may reject a render even when its input passed the parser's larger atom-count limit.

Exit codes: 0 success; 2 CLI/config; 3 unsupported format/capability; 4 invalid/truncated file or frame; 5 element identity/geometry; 6 allocation/expansion limit; 7 I/O; 8 internal error. Diagnostics go to stderr. `--inspect` streams normalized input JSON to stdout without scene construction. `--inspect-scene` exposes wrapped instances, clipped periodic bonds, cell edges and the camera. `-o` writes the M3 PNG atomically after successful encoding/close. Input inspection settings describe requested rendering options; element/pair maps show explicit overrides. Scene inspection exposes effective colors/radii and distances.

### M0/M1 evidence (historical milestone snapshot)

The repository now contains the CMake build, strict CLI/JSON configuration, shared POSCAR/CONTCAR parser, streaming OUTCAR parser, and black-box CTest integration suite. `tests/fixtures/README.md` records upstream licenses, pinned provenance, hashes and scope. Debug and sanitizer builds are the M1 validation configurations. Full rendering, periodic neighbors, benchmark latency targets, real MD fixtures, additional VASP versions, and native HPC builds remain later release gates. No all-version OUTCAR compatibility or performance claim follows from passing M1.

Validation on 2026-10-29, GCC 13.3.0 / WSL2 x86-64:

- Debug: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`, `cmake --build build`, `ctest --test-dir build --output-on-failure`: passed all 22 Python test groups, including parameterized invalid-input and randomized geometry cases, through one CTest entry.
- Sanitizers: equivalent `build-sanitize` configuration with `-DENABLE_SANITIZERS=ON`, followed by build and CTest: passed with address, undefined-behavior and leak checks. LeakSanitizer hit a ptrace restriction inside the sandbox; the full leak-enabled suite passed outside that restriction. The address/undefined-behavior suite also passed inside the sandbox with `ASAN_OPTIONS=detect_leaks=0`.
- Release: `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF`, followed by build and inspection of the real static fixture: passed, with no test/Python dependency in that build configuration.
- No PNG images or renderer latency benchmarks exist at this milestone. Test output verifies destination preservation when PNG rendering is requested before M3.

Additional M1 validation on 2026-10-29: the user supplied 20 real CONTCAR slab
snapshots from `snellius-mcp/examples/structures`. Copies and their original
manifest are in `tests/fixtures/slabs`. All 20 parse successfully (49–249 atoms
each, 1,687 total). The regression suite now has 23 groups and verifies their
hashes, ordered species, lattice rows and every unwrapped Cartesian position.
The collection includes skewed cells, negative vector components, selective
dynamics and trailing CONTCAR sections. It adds no real OUTCAR MD coverage.

M2 preset acceptance must include these user structures. The small `visual-v1`
pair table above currently omits Co, Rh, Ru, Pd, Ce and Ag connectivity. Extend
and version the preset with explicit element-pair cutoffs for the intended
surface/adsorbate systems before treating their default bonding as complete;
verify the choices visually and with periodic-neighbor tests. Parser success
alone does not establish useful bond defaults.

## 19. M2/M3 implementation and validation — 2026-10-29

### Parameter reference

The user selected [Atom Architect](https://github.com/ifilot/atom-architect) as
the source for interatomic distances and other display parameters. The snapshot
is pinned to commit `9e73e51055df8eb8b893a5895e7faadc39f46b25`; original JSON,
bond-setting source, notices, licenses and a policy explanation are preserved
in `vendor/atom-architect`. `element_defaults.inc` contains its generated
118-element color/radius table. This supersedes provisional `visual-v1`.

The source's Euclidean-distance thresholds are equivalent to: H/light 1.2 Å;
H/heavy 2.0 Å; other light/light 2.0 Å; other light/heavy 2.5 Å; heavy/heavy
3.0 Å, where light means atomic number ≤20; Al-C has a 3.5 Å exception.
Explicit unordered pair overrides win. This covers Co, Rh, Ru, Pd, Ce and Ag
without inferring bonds from their display radii. Retain this project's
inclusive threshold with tolerance `32*DBL_EPSILON*max(1 Å,cutoff)`; upstream
uses strict `<`. Display parameters are visualization policies, not a claim
that cutoff connectivity determines chemical bond order.

### M2 scene construction

`elements` owns display/cutoff defaults, `scene` owns periodic construction and
camera fitting, `render` owns raster buffers, and `png_io` owns atomic output.
These are internal modules linked into the standalone CLI.

Positions are converted to fractional coordinates and wrapped into `[0,1)`
before deterministic atom-instance generation. The parsed Structure remains
unchanged. Enumerate unique base pairs `i<=j`; for equal IDs keep only image
displacements whose first nonzero component is positive. Zero-distance
connectivity is excluded. If `g[d]` is a reciprocal dual-basis vector and
`df` the fractional displacement, every eligible image satisfies
`|df[d]+image[d]| <= cutoff*|g[d]|`. Conservative integer bounds enumerate
those candidates; Cartesian distance removes the false positives. This
supports image offsets outside `[-1,1]` in skewed cells.

Each base bond is translated over every cell placement whose fractional
segment can intersect the repeated supercell. Liang-Barsky clipping operates
in its fractional coordinates. Upper-face-only translated segments are omitted
to avoid disconnected duplicates from outside atoms; lower faces are retained.
The centerline is clipped, then rasterized as a flat-capped finite cylinder;
its circular width may extend beyond the boundary just as atom spheres do.
Scene order and `(source pair,image displacement,start translation)` keys are
stable. The repeated cell contributes exactly 12 depth-tested edges.

The camera uses a right-handed orthonormal basis, uniform pixels-per-angstrom
scale, projected primitive endpoints/radii and a one-pixel-per-side line guard.
Auto-fit preserves aspect ratio with the configured margin. All finite scene
depths are retained; raster bounds provide image clipping.

### M3 raster and output

The reference renderer is single-threaded with fixed 2× linear supersampling.
Each sample computes the front sphere surface or the frontmost valid finite
cylinder intersection, including both caps. Lighting follows the M0
ambient/diffuse coefficients. Numerically equal depths use a relative tolerance
of `32*DBL_EPSILON*max(1,|depths|)` in sample-space units and retain the first
primitive; this avoids unstable ownership on coincident cylinder surfaces.
Cell edges use a one-output-pixel width and the same depth test.

Downsampling averages premultiplied RGBA and returns straight-alpha pixels,
preserving transparent-background edges without white halos. Geometry is
opaque by default. An element alpha override blends the nearest surface over
the background, not over deeper geometry; layered transparent surfaces are
outside this reference implementation. Fully transparent primitives are skipped.

libpng RGBA8 output uses compression level 1 as a reference setting, not a
benchmark-selected encoder winner. A unique neighboring temporary file is
encoded and closed before rename. Failures clean it up and preserve an existing
destination. Optional labels/axes/legend/scale bar remain M6 work, and threads
above one remain M5 work; requesting these for PNG rendering fails explicitly.

### Validation and baseline

34 Python test groups run through two CTest entries. They pass in Debug, against
the Release executable, and in the address/undefined-behavior/leak sanitizer build, with leak checking run
outside the sandbox's known ptrace restriction. Evidence includes:

- Imported colors and display radii checked for all 118 elements; representative
  cutoff classes, Al-C exception, pair symmetry and threshold boundaries.
- A brute-force periodic-image oracle for randomized skewed cells and an
  explicit neighbor at displacement `(3,-1,0)`; clipping bounds, duplicate keys,
  nonzero-image self bonds, instance counts, wrapping and camera fitting.
- An independent per-ray oracle for crossing tilted cylinders whose depth order
  exchanges inside their overlap, axial caps, near-parallel axes, side walls,
  sphere occlusion and cell-line depth. Pixel channel differences are at most
  one quantization level in these small opaque oracle scenes.
- Fixed-environment decoded-pixel golden hashes for a skewed cell, repeated Co
  slab and Pd/ceria; deterministic runs, alpha-edge filtering, memory/work
  limits, destination preservation, rename failure and temporary-file cleanup.
- Successful PNG rendering of all 20 user slab examples. Representative PNGs
  were visually inspected; the full gallery is in `artifacts/m3/slabs`.

Release builds use GCC 13.3.0 with `-O3 -DNDEBUG`. Initial process-lifetime
measurements and optimization targets are in `artifacts/m3/README.md`, with
raw samples, stage medians and hashes in `baseline.json`. The seven-case
baseline covers 100/500/1,000 atoms, a repeated slab, Pd/ceria, and static/large
relaxation OUTCAR inputs. It uses 20 warm-cache samples after three warmups,
with no concurrent test/gallery job and outside sandbox tracing. CPU frequency, other system activity and
OS-cold-cache state were not controlled, so native Linux/HPC performance
remains unverified. Direct pair search remains the reference baseline; AA,
backend, codec and threading winners are M4/M5 decisions.

## 20. Illustrated style and canonical slab top views — 2026-10-29

The user requested the supplied reference's flat illustration style and top
views of all 20 CONTCAR examples, then requested black atom/bond contours.
`--style shaded|illustrated` and JSON `image.style` now select smooth lighting
or layered flat disks with darker colored rims and pale circular highlights.
Illustrated bonds have a solid gray fill. Both modes retain the same exact
sphere/capped-cylinder depth intersections, fixed 2× AA, and PNG output path.

Illustrated contours are black, drawn inside the projected silhouette and
depth-tested with the underlying primitive. Atom outlines use radial inset;
bond outlines probe the projected finite-cylinder silhouette in eight
directions, including end caps. Width is measured in final output pixels,
independent of camera scale: `--outline-width` or JSON `image.outline_width`
accept 0..8, default 1. Zero disables outlines; shaded mode does not use them.

`--view top` and JSON `camera.view: "top"` use the normal to the first two
lattice vectors, choosing the sign toward the third. By default the first
vector points right; explicit up settings retain precedence and collinear
choices are rejected after the surface normal is known. Numeric view vectors
remain supported and explicit CLI choices override the JSON preset.

`presets/illustrated.json` is the supplied slab collection's canonical display
preset: exact top view, 768×768, white background, visible cell outline, 1.5-pixel
black contours, and a curated palette/display radii. Colors and radii are
explicit visual overrides; bond cutoffs still use Atom Architect. Optional
`image.depth_fade` in [0,1] whitens more distant primitive fills according to
their center depths, without changing geometry or visibility. The preset uses
0.9 and keeps black contours crisp. General defaults retain zero fading and
the M3 shaded appearance, so existing shaded decoded-pixel goldens still pass.

`tools/render_examples.py` regenerates all 20 images and a Markdown gallery in
`artifacts/illustrated-top`, with hashes, scene counts and actual camera vectors
in `manifest.json`. Additional tests cover surface normals for skewed/tilted
and left-handed cells, camera override precedence, flat color bands,
configurable contour presence on atoms and bonds, depth fading without scene
changes, and illustrated PNG output for every supplied slab. There are now
40 test groups across two CTest entries. The M3 performance report remains a
historical baseline for the earlier shaded executable; it does not measure
the illustrated/contour mode or the current executable hash.

Validation: both CTest entries pass in Debug and with AddressSanitizer,
UndefinedBehaviorSanitizer and leak detection (outside the known sandbox
ptrace restriction). All 20 final PNG/preset hashes and top-view camera
directions were checked. The regenerated Ru/water and Pd/ceria views were
visually inspected for layered colors, surface orientation and contours.

Following the request for thicker unit-cell boundaries, `--cell-width PIXELS`
and JSON `overlays.cell_width` now accept positive widths up to 32 output pixels.
The general default is 2 pixels; the illustrated preset uses 3. Camera framing
reserves the chosen width plus an AA guard, and scene inspection reports the
corresponding world-space radius. Raster tests measure the visible width and
check CLI override precedence/invalid values. Historical M3 golden hashes
retain their original explicit 1-pixel setting. All 20 illustrated top views
are regenerated with the thicker boundaries; artifacts remain Git-ignored.

## 21. v0.1.0 release boundary — 2026-10-29

The user selected v0.1.0 as the checkpoint before workstation performance work
and selected GNU GPL version 3 for the project. Original project material is
GPL-3.0-only, with upstream MIT/LGPL notices retained. CMake's project version
generates executable metadata and package versions; milestone numbers are not
release version numbers.

The release includes M0–M3, the illustrated/top-view refinements, enabled cell
boundaries, and adjustable atom/bond/cell line widths. Installation, release
source/binary packages, fixture provenance and correctness tests are included.
Generated artifacts/builds, Git metadata and local agent/credential settings
are excluded from source packaging and ignored by Git.

No M4/M5 performance selection or M6 workstation/HPC performance acceptance is
claimed. M6 also retains its overlays and production-hardening scope. Start
those experiments with a fresh workstation baseline covering the current
illustrated and shaded styles; historical laptop observations do not define
release guarantees or the new optimization targets. See `performance.md` and
`release-v0.1.0.md` for the handoff and release validation record.

## 22. M4 decision record

The user accepts the current WSL workstation for all performance and deployment
validation. A fresh 20-case shaded/illustrated baseline, a six-case filesystem
comparison and raw timing/image artifacts are recorded in `performance.md`.
The baseline binary and its source archive are retained under
`artifacts/m4-m6/reference/`. Native/2×/4× sampling controls are implemented;
the default 2× path exactly preserves the 20 baseline images and the three
sample grids pass the independent ray oracle. Point sampling is a control,
not analytic edge coverage. The optional Cairo prototype supplies native edge
coverage, but fails explicit interpenetrating-sphere and colored crossing-cylinder
depth gates at retained witness pixels. Accurate Z-buffer rendering is selected.
The adaptive mask uses conservative surface-depth intervals and preserves four
depth samples at ambiguous overlaps and edges. It is bit-exact to uniform 2×
on the measured illustrated cases; shaded interior approximation differs by at
most two channel levels on those cases and passes higher-quality comparisons.
It improves the illustrated Co-100 median by 19.1%, with scene-dependent gains
elsewhere. Uniform 2× remains the default while both AA candidates enter M5.

The experimental build passes all five CTest entries (51 checks) in Release and
with address, undefined-behavior and leak sanitizers. `performance.md` records
the algorithms, negative results, raw report paths, cycles/RSS/image evidence
and M4 decision. Encoder/thread/AA combination selection, overlays, deployment
validation, final targets and hardening remain M5–M6 work.

## 23. M5 decision record

The renderer caches projected bounds and cylinder intersection coefficients,
then renders disjoint 32×32 tiles in stable primitive order. OpenMP trials at
1/2/4/8 workers preserve every tested pixel. PNG profiles use identical RGBA
buffers across libpng settings, Cairo PNG and a libdeflate-backed encoder.
Cairo's partial-alpha RGB quantization fails the lossless encoder gate.

The production defaults are the accurate Z-buffer with uniform 2× sampling,
automatic workers (up to eight for at least 512×512 pixels; one below that area),
and libdeflate/no filtering. Interleaved trials select compression 0 on native
Linux storage and compression 1 on the measured WSL 9p Windows-mounted output
path; `image.png_compression` / `--png-compression auto` resolve accordingly.
Explicit levels and thread counts remain available. libpng and serial builds
remain supported through configure-time feature switches. Adaptive AA remains
available; its benefit is scene dependent after threading.

The selected defaults exactly preserve all 20 original baseline RGBA images and
the six Windows-path comparison images. Native-storage medians are 15.71–61.22
ms shaded and 30.99–127.74 ms illustrated. The experimental build's seven CTest
entries cover 58 checks, including aggregate encoder budgets and atomic write
failures, and pass in Release and with address/undefined-behavior/leak sanitizers.
The default Cairo-free build passes its five CTest entries. Raw reports, source
snapshots, images, binaries, interleaved orders and decisions are documented in
`performance.md`. M6 overlays, final deployment/package validation, hardening
and scoped workstation targets remain.


## 24. M6 decision and acceptance record

All requested optional annotations are implemented in the selected accurate
Z-buffer, without external font assets. Element/zero-based instance labels use
one center and eight off-center front-surface depth probes; stable foreground
priority filters colliding label plates. This handles periodic bond caps while
suppressing hidden atoms. The legend counts scene atom instances by element;
Cartesian axes and a nice 1/2/5 angstrom scale bar use the fitted camera. Legend
and footer space are reserved before framing, with layout/visibility work and
allocation limits. All 20 supplied slabs have plain and annotated galleries.

The final default images exactly match all 20 original baseline RGBA references.
The final native/Windows/annotated runs pass 34 scoped performance checks from
`performance-targets.json`; raw results and pixel validation are retained in
`artifacts/m4-m6/m6/`. Default Release, all-feature Release and leak-enabled
address/undefined-behavior suites pass, covering 63/67 checks. Source/binary
archives, private-prefix installation, runtime dependencies, clean offline
source builds/tests and serial/libpng-only fallback output are checked by
`tools/validate_deployment.py`. See `m6-validation.md` and `performance.md` for
commands, evidence paths, numeric targets and limits. WSL fully satisfies the
user-approved deployment-validation scope; M4–M6 are complete. M7 XML and broader
OUTCAR version/real-MD claims remain separate from the v0.2.0 release.

## 25. M7 XML decision and validation record

M7 follows the published v0.2.0 tag in a separate development change. The
optional libxml2 reader is enabled by default (`ENABLE_XML=OFF` removes its
dependency). It streams the entire document while retaining only metadata,
the current structure and the selected snapshot. Per-ion/type identities and
populations are cross-checked; positions use each structure's own basis.

Numeric frames count unnamed ionic structures directly under calculation or
modeling, excluding initialpos, primitive cells and nested SCF structures.
Default final selection prefers finalpos, otherwise the last ionic structure;
finalpos does not duplicate the ionic frame count. A final-only document has
one logical frame 0; an initial-only document has no result frame. Numeric
selection still validates all later geometry and the complete closing root.
Malformed/truncated XML is rejected and `--recover` remains OUTCAR-only.

DTDs/entity-reference nodes are rejected, external loading/substitution and
XInclude are disabled, NONET is set, and libxml2's normal limits remain active.
Application byte/line, depth/node/text/name/attribute, geometry and frame limits
bound work and state. File input is application-owned; compressed/UTF-16 and
namespaced XML are outside the contract. Failed parses preserve output files.

Nineteen XML test groups cover synthetic limits/security/frame semantics and
three pinned MIT pymatgen fixtures: VASP 5.2.2 relaxation, 6.3.2 Si MD and
6.0.8 dielectric output. Independent ElementTree geometry and equivalent-POSCAR
RGBA comparisons pass, with/without annotations. Release and leak-enabled
address/undefined-behavior checks include these tests; XML-disabled source builds
retain the existing formats and explicit capability errors. Thirty-sample
timings and a 100-versus-10,000-frame RSS comparison are retained under
`artifacts/m7/`. See `m7-xml.md` for measurements, commands and exact scope.

## 26. M8 periodic context — 2026-10-29

The user selected centered neighboring cells. `--expand-xy NX,NY` adds NX/NY
cells on both sides of the primary cell along lattice a/b, with no z expansion.
`1,1` generates a 3×3×1 patch with origins -1..1 on a/b. Each source atom is
wrapped once as before and copied at integer lattice translations; primary-cell
identity is origin [0,0,0], independent of Cartesian axes or input wrapping.

`--image-colors normal|muted|greyscale` affects only nonprimary atom RGB,
preserving element alpha and radii. Expansion defaults to muted; no expansion
retains the old normal-color defaults. It can also color existing positive
`--repeat` copies. Context mode outlines the primary cell; plain legacy repeat
keeps its repeated-supercell outline. Bond endpoints are clipped against the
centered skewed region, connectivity and gray bond colors remain unchanged,
and the camera fits all instances. Legend counts still include all instances,
but swatches prefer the primary element palette.

CLI/strict JSON, inspection metadata, bounds/instance limits, zero expansion
and override behavior are documented in `periodic-context.md`. Nonzero centered
expansion and nontrivial repeat are mutually exclusive. Seven focused groups
check counts, primary identity, colors/alpha, translated-bond equivalence,
wrapping, cell corners, limits, legacy pixels, threading and legend colors,
including all 20 supplied slabs. Muted and greyscale 20-image galleries are
retained under `artifacts/m8/`; full Release and sanitizer suites cover M8.
