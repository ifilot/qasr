# QASR v0.7.0 release — 2026-11-01

QASR v0.7.0 adds illustrated molecular orbital surfaces from PyTessel PLY files.
The executable remains `qasr`; configuration and job schema versions remain 1.
The preceding tag is v0.6.0. Before this release the checkout was one documentation
commit ahead of that tag, with the new orbital implementation uncommitted.

## Molecular orbitals

- ASCII and binary little-endian PLY triangle layers with optional vertex normals,
  explicit bohr/angstrom units, mesh-unit origins and opaque phase colors.
- Smooth or illustrated shading, antialiased silhouettes and phase boundaries,
  and depth-aware contours that preserve folds within one phase. The accepted CO
  HOMO rim is restored without adding marks to the smooth core control.
- Independent geometry in XYZ or any existing supported structure format.
  Orbital surfaces participate in camera fitting, depth compositing and label
  visibility. Worker scene keys include mesh content hashes.
- Ten CO RHF/STO-3G orbitals calculated with PyQInt and tessellated with PyTessel,
  including analytic wavefunction-gradient normals, physical scale bars, energies,
  occupations, sampling checks, final galleries and reproducible generation.

See the [format/unit guide](molecular-orbitals.md), the
[CO example](../examples/orbitals/README.md), and the
[architecture, commenting and performance review](orbital-review.md).

## Cleanup and optimization

World-space triangles have a shared value header independent of configuration
and scene APIs. Geometric contour policy is separate from polygon composition;
orbital cleanup is centralized. API contracts cover ownership, partial failures,
units, projected coordinate systems and caller-owned scratch storage.

Packed PLY records use one bounded read per vertex/face. Finite contour distances
use an ordered minimum, and normal lengths are reused. Unoutlined/shaded meshes
skip contour allocation and propagation. Nine interleaved baseline/candidate
workflows require identical encoded PNGs; timings and limitations are in the review.

The orbital example is organized into `meshes/`, `recipes/` and `renders/`, with
one final gallery. Experimental snapshots are excluded from release packages;
regression reference data lives in test fixtures. Earlier release records retain
their original validation scope, and the legacy v0.3.0 source archive is unchanged.

## Compatibility and limits

Existing atomic geometry, styles, polygons, cell edges, image codecs and optional
movies remain supported. Orbital meshes require the Z-buffer backend and are
opaque. They remain in world coordinates, are not repeated with cells and are
not recomputed along a trajectory. The supported PLY subset, 32-layer/200,000-input-
triangle limits and 64 MiB configuration budget are explicit in the guide.

Python dependencies are optional and only needed to generate new orbitals or
reassemble galleries. The included PLY example renders directly with QASR.

## Release validation

- Default Release suite: **22/22 passed**.
- ASan/UBSan/LeakSanitizer suite: **22/22 passed**.
- Release with JPEG, WebP, shared FFmpeg and experimental Cairo enabled:
  **24/24 passed**, including unsupported-orbital-backend rejection.
- All nine interleaved optimization workflows produced byte-identical PNGs.
- Scoped GCC analyzer checks and standalone-header compilation passed.
- A private install reported v0.7.0 and rendered its installed CO HOMO recipe.

The [review](orbital-review.md) records exact scope, measured timings and remaining
limits. This is local release verification; no remote publication is implied.

The minimal source archive build (without OpenMP, XML or libdeflate) and the
standalone CO download also rendered the HOMO successfully. Decoded source-build
and installed pixels matched the accepted image exactly. The handbook check
verified 323 local links, 28 image records, current source hashes and the unchanged
legacy archive. The source archive's minimal build/render check supplements the
full configured suites above.
