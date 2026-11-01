# Molecular orbital surfaces

QASR imports triangle meshes exported by [PyTessel](https://github.com/ifilot/pytessel),
which constructs isosurfaces from scalar fields with marching cubes. The
[PyQInt orbital visualization guide](https://ifilot.github.io/pyqint/orbital_visualization.html)
uses PyTessel's `write_ply` to export molecular orbitals. QASR renders those PLY
meshes alongside an ordinary molecular geometry; Python packages are only needed
to calculate and tessellate new orbitals.

See the [CO gallery and generation recipe](../examples/orbitals/README.md).

## Configuration

Add an `orbitals` array to a JSON configuration, then use `--config`:

```json
{
  "image": {"style": "illustrated", "outline_width": 1.5},
  "overlays": {"cell": false, "scale_bar": true},
  "orbitals": [
    {"file": "mo-positive.ply", "units": "bohr", "color": "#4c9cdb"},
    {"file": "mo-negative.ply", "units": "bohr", "color": "#ed9163"}
  ]
}
```

Each layer requires `file` and `units` (`bohr` or `angstrom`). `color` is opaque
`#RRGGBB`, defaulting to blue. Optional `origin` is a three-number translation in
**mesh units**, applied before conversion: `position_A = (vertex + origin) *
unit_scale`. File paths resolve relative to the containing configuration, or to
the working directory for inline worker jobs. Configuration dumps retain absolute
paths. Orbitals belong in invocation configurations rather than user preferences.
Use an empty array to clear layers inherited from an earlier configuration.

Supported PLY files use ASCII or binary little-endian format 1.0, with ordered
vertex properties `x y z`, optionally followed by `nx ny nz`, each `float` or
`double`. Faces must be triangles with `property list uchar uint vertex_indices`
(`int` and `vertex_index` also work). Other elements, vertex attributes, face
sizes and binary encodings are rejected with a diagnostic. Missing normals use
triangle normals. Nonzero supplied normals are normalized and interpolated.
Degenerate zero-area triangles are skipped.

`illustrated` uses flat color bands, pale highlights and dark silhouettes in the
same visual family as the atoms, without outlining internal triangle edges.
Silhouette and phase contours use a screen-space coverage distance transform,
so noisy imported normals affect lighting rather than outline width. Same-phase
self-occlusion contours compare neighboring geometric depth planes, with depth
range and silhouette guards to avoid outlining steep continuous facets. See the
[accepted rim comparison and implementation review](orbital-review.md).
Other styles use smoothly shaded surfaces; atom-specific ORTEP sections and
dither tones apply to atoms and bonds. Surfaces are opaque and use per-sample
depth against atoms, bonds, other surfaces and polygon faces. They participate in
camera fitting and label visibility. The Z-buffer backend is required. Raster
work, allocation and cancellation limits apply; at most 32 mesh layers and
200,000 input triangles across layers are accepted within the 64 MiB configuration
budget. Surfaces stay in world coordinates and are not replicated with unit cells
or recomputed during a trajectory.

`--inspect-scene` exposes converted vertices and normalized normals in
`face_triangles`, plus the separate `orbital_triangles` count. Those triangles
also contribute to the aggregate triangle count in render reports. Batch worker
scene keys include mesh content hashes, so editing a PLY file invalidates its
cached scene.

## Units and sampling

PLY contains no unit metadata. QASR requires explicit units and converts bohr to
ångströms using **1 bohr = 0.529177210903 Å**. Normals are dimensionless unit vectors.
XYZ geometry and QASR radii are in ångströms.

PyQInt evaluates normalized orbital amplitudes in atomic units, with dimensions
bohr⁻³ᐟ². An orbital is a signed amplitude, not an electron density (bohr⁻³).
Tessellate `+isovalue` and `-isovalue` separately and assign phase colors. Changing
a whole orbital's sign swaps the colors without changing its physical meaning.

For the versions used by the CO example, `build_rectgrid3d(-L/2, L/2, N,
endpoint=False)` samples with spacing `L/N`, x fastest and z slowest. PyTessel's
`marching_cubes` uses that spacing and returns centered coordinates for a diagonal
cell of side L. Consequently **the example uses origin zero**. Do not add the
sampling box's lower corner a second time. An endpoint-inclusive grid with
spacing `L/(N-1)` passed to this API would shrink and displace the surface.
For grids centered elsewhere, translate the returned mesh by that grid center.

The example uses a 121³ grid, 4× supersampling and normalized analytic PyQInt
gradients for lighting normals; positions remain the PyTessel isosurface.
The generator verifies all three axes with an asymmetric plane at 0.73 bohr,
checks mesh dimensions and normals, and rejects orbital surfaces that reach the
sampling boundary. These runtime checks guard against library convention changes.
