# Changelog

## Unreleased

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
