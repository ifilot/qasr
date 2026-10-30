# Changelog

## [Unreleased] - v0.5.0

- Add independently optional system libjpeg and libwebp output via
  `ENABLE_JPEG` and `ENABLE_WEBP`. Output suffixes select JPEG (quality 90,
  opaque only) or lossless RGBA WebP across CLI, job and worker renders.
- Report image format and codec capabilities; preserve atomic output and
  validate codec dimensions and memory allowances.
- Separate physical cell vectors, per-axis PBC and source metadata; support
  cell-free molecules, wires, slabs and nonperiodic display cells.
- Add input-only reader options and shared validation, native XYZ/Extended XYZ,
  Gaussian Cartesian input/orientation output, and AMS text System readers.
  No ASE or additional runtime library dependency is introduced.
- Respect PBC in wrapping, image bonds and clipping; draw only supplied cell
  vectors and reject replication along nonperiodic axes.
- Report cell/PBC metadata through inspection and render reports, extend CLI/job/
  worker format selection and schemas, and add source-independent regression tests.
- Document supported format subsets and unsupported Gaussian/AMS variants in
  `docs/input-formats.md`. Existing VASP behavior remains covered by regressions.
- Add monocolor and dualtone dithered variants for ORTEP and BALSAC via
  `--tone` / `image.tone`, with output-pixel Bayer shading, matching presets
  and side-by-side sample renders.

- Add `ortep` spherical drawing with visible great-circle arcs, white atoms and
  black bonds, and `balsac` drawing with smooth highlights, element colors,
  pale bonds and black contours. Both use accurate depth-buffer visibility.
- Expose both styles through CLI, layered configuration, jobs and batch requests;
  update embedded/served schemas, presets, handbook examples and render tests.
- Explicitly reject the new styles with the experimental Cairo painter.

## [0.4.0] - 2026-10-29

Close M9–M11 and establish QASR: Quick Atomic Structure Renderer, executable
`qasr`. This release includes all completed work since v0.3.0.

### Application naming

- Name the application QASR (Quick Atomic Structure Renderer) and its native
  executable, CMake target/project and source/binary packages `qasr`.
- Use `qasr` for diagnostics, XDG user preferences and installed data/doc paths.
- Update current guides, schemas, examples and the standalone handbook; retain
  the immutable historical v0.3.0 source archive with its provisional name.

### Batch processing and OpenMP (M10/M11)

- Add original seeded POSCAR/XML datasets and seven workload families, including
  same-path, same-size, timestamp-preserving cache invalidation scenarios.
- Share the CLI render path with a sequential JSONL worker; add bounded
  content-sensitive input/scene caching, cache controls, request schemas,
  structured responses and cooperative cancellation/recovery.
- Compare persistent requests with the CLI and benchmark worker/thread pools.
- Parallelize sufficiently large sample initialization, primitive projection and
  AA resolution with OpenMP; preserve pixels, dynamic tile scheduling and the
  conservative automatic thread policy. Report actual render threads.
- Document measured latency/CPU/memory tradeoffs, rejected scheduling candidates,
  the worker interface and reproducible synthetic/native-WSL benchmarks.

### Configuration and atom styling (M9)

- Load optional XDG user defaults, support explicit initialization/opt-out and
  dump resolved settings with per-field provenance.
- Merge JSON objects by field across layers, replace arrays, and replace
  lower-layer bond cutoffs without accepting duplicates within one file.
- Add bounded per-source/per-cell atom color/radius selectors with explicit
  rule precedence, inspection metadata and stable element legend colors.
- Embed/install strict config, user and job JSON Schemas; add validation,
  structured errors, stdin/file jobs and compact hashed render reports.
- Document the development interface with real highlight images and a verified
  release source download, retaining the v0.3.0 release archive.

## [0.3.0] - 2026-10-29

Complete M7 XML, M8 periodic context and the standalone GitHub Pages handbook.
The M9 configuration/automation design remains subsequent work.

- Add an offline-ready standalone user-handbook website with input/output
  reference, tested render examples, periodic/style comparisons, command builder,
  configuration/CLI tables and downloadable inputs/development source.
- Add centered xy periodic context with `--expand-xy`, full primary-cell
  colors/outline and muted or greyscale periodic-image colors. Preserve alpha,
  skew-cell clipping and the existing repeat defaults; expose JSON controls.
- Complete M7 streaming `vasprun.xml` input with optional system libxml2,
  content detection, zero-based ionic frames and final-position semantics.
- Validate per-ion/type identities, changing cells, complete documents and
  bounded input/state; reject DTDs, entities and truncated XML.
- Add pinned MIT-licensed VASP 5 relaxation and VASP 6 MD/static fixtures,
  independent geometry/pixel comparisons, parser limits/security tests and
  reproducible XML timing/trajectory-memory measurements.

## [0.2.0] - 2026-10-29

Complete design milestones M4–M6 on the user-approved WSL workstation.

- Complete workstation renderer/AA/PNG/thread comparisons, retained raw timing
  and image evidence, and scoped WSL performance targets.
- Cache projected/cylinder geometry and render deterministic 32×32 tiles with
  automatic OpenMP workers; preserve reference pixels across thread counts.
- Add libdeflate PNG encoding, codec/filter/compression controls and measured
  native-storage/WSL Windows-output defaults; retain libpng and serial fallbacks.
- Add surface-visible element/instance labels, Cartesian axes, an element
  legend and an angstrom scale bar with reserved camera layout space.
- Expand allocation/work/atomic-write checks, annotation tests, sanitizer
  coverage, installed/package execution and clean source/fallback build checks.
- Keep Cairo painter/PNG and alternate AA controls as documented experiments;
  reject incorrect depth ordering and lossy alpha/RGB codec paths as defaults.

## [0.1.0] - 2026-10-29

Initial reference-renderer release, completing design milestones M0–M3.
Workstation performance experiments and optimization are deferred to the next
development phase. The earlier milestone-based `0.3.0` identifier was a
development label, not a published release.

### Added

- Standalone C11/Linux `vasp-render` CLI for POSCAR, CONTCAR and OUTCAR inputs.
- Strict CLI/JSON configuration, explicit species mapping, zero-based ionic
  frame selection and opt-in recovery of unfinished OUTCAR files.
- Content-based format detection and normalized input/scene JSON inspection.
- Periodic bonding for skewed cells, same-atom image bonds, cell repetition,
  duplicate prevention and clipping at supercell boundaries.
- Pinned Atom Architect colors, radii and pair-cutoff defaults, with overrides
  and retained upstream licensing/provenance.
- Orthographic camera fitting, configurable view/up vectors and a canonical
  slab top view aligned to the lattice surface normal.
- Accurate single-threaded sphere/capped-cylinder depth-buffer rendering,
  fixed 2× supersampling, shaded and illustrated display styles.
- Illustrated pastel preset, pale highlights, black atom/bond contours,
  optional depth fading and adjustable contour width.
- Visible unit-cell boundaries by default, with configurable width: 2 pixels
  generally and 3 pixels in the illustrated preset.
- Transparent backgrounds and alpha-aware edge filtering; atomic PNG output
  with temporary-file cleanup and destination preservation on failure.
- Resource/work limits, concise diagnostics and optional pipeline stage timings.
- Forty test groups covering parsing, periodic geometry, ray-reference
  rendering, decoded-pixel goldens, error handling and all 20 supplied slabs.
- CMake installation, local binary/source packaging and source checksums.
- GPL-3.0-only project license and third-party notices.

### Release scope

- One rendering thread; `auto` selects that single-threaded path.
- Labels, axes, legends, scale bars, alternate render/AA/encoder strategies,
  parallel rendering and `vasprun.xml` remain future work.
- OUTCAR fixture coverage includes VASP 5.2.11, 5.2.12 and 6.2.0. Broader
  version/mode coverage and real MD fixtures remain open.
- Layered transparent geometry is unsupported; element alpha applies to the
  nearest surface over the background.
- Laptop/WSL measurements are development observations. No workstation/HPC
  performance target or backend/encoder winner is claimed for this release.
- Generated renders, benchmark data, packages and build products are ignored
  by Git and excluded from the source archive.
