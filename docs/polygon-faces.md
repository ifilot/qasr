# Polygon faces and atom-colored bonds

QASR fills user-supplied polygon faces inside atomic structures. Each face is an
independent ordered loop, so it need not close a cage. A zeolite can use POSCAR
or Extended XYZ geometry plus a JSON face file. CIF is not a new input format;
the included example was converted from the cited IZA CIF to POSCAR and XYZ.

## Quick start

From the repository root, with a newly built QASR:

```sh
build/qasr examples/zeolite/cage.xyz \
  --faces examples/zeolite/tiles.json --bond-colors atoms --bond-radius 0.075 \
  --sphere-scale 0.45 --cell off --width 900 --height 900 -o zeolite.png

# Use the complete example recipe for its exact palette and camera:
build/qasr examples/zeolite/ring.xyz \
  --config examples/zeolite/renders/03-single-face-translucent.json \
  -o single-face.png
```

See the [comparison gallery](../examples/zeolite/README.md) for images, inputs,
provenance and regeneration commands.

## Face files

`--faces FILE` loads an object containing a `faces` array. `schema_version: 1`
and `$schema` are optional. Discover the schema with `qasr --schema faces`.

```json
{
  "schema_version": 1,
  "faces": [{
    "vertices": [[0, 0, 2], [4, 0, 2], [4, 4, 2], [0, 4, 2]],
    "coordinates": "cartesian",
    "color": "#54b6d880",
    "outline_color": "#257895",
    "outline_width": 0.8,
    "repeat": true
  }]
}
```

Vertices follow the boundary in either winding. Do not repeat the first vertex
at the end. Cartesian coordinates are angstroms; `coordinates: "fractional"`
uses the three lattice vectors and requires a complete cell. Explicit vertices
are not automatically wrapped.

Alternatively reference zero-based source atom indices in the selected frame:

```json
{
  "faces": [{
    "atom_indices": [0, 3, 7, 4],
    "cell_offsets": [[0, 0, 0], [1, 0, 0], [1, 0, 0], [0, 0, 0]],
    "color": "#e7b34c70"
  }]
}
```

Index references use the renderer's wrapped primary-cell coordinates. Optional
`cell_offsets` identify each vertex's periodic image, allowing boundary-crossing
rings without spanning the wrong side of a cell. There must be one offset per
index; omitted offsets are zero. Nonzero offsets require periodicity in that
direction. Positions follow frame selection; indices remain source indices
during repetition. Atom radius/color overrides do not move vertices.

| Field | Default | Meaning |
| --- | --- | --- |
| `vertices` or `atom_indices` | Required | Exactly one representation; 3–64 entries |
| `coordinates` | `cartesian` | `cartesian` or `fractional`, only for explicit vertices |
| `cell_offsets` | All zero | Lattice offsets, only for atom indices |
| `color` | `#43a6cf80` | Flat fill; `#RRGGBB` opaque or `#RRGGBBAA` with alpha |
| `outline_width` | `0` | Inset boundary width in output pixels, 0–8 |
| `outline_color` | `#23556e` | Independent outline RGBA |
| `repeat` | `true` | Translate with `--repeat`/`--expand-xy`; false draws once |

Concave simple loops are supported. Triangulation produces no internal outlines
or doubled-alpha seams. For nonplanar loops, it uses the dominant normal
projection and retains the original 3D vertices, yielding a piecewise planar
spanning surface. Degenerate or self-intersecting projected boundaries fail;
supply separate faces for folded shapes.

Faces render on both sides with flat supplied colors, independently of atom
drawing style and dither tone. Transparent faces blend in per-sample depth order
with each other and the nearest atom/bond surface. Existing atom rendering still
retains only the nearest atomic surface; this does not add transparent-atom
volume rendering. Opaque faces also occlude label anchors; translucent faces
allow labels. Outlines follow only the original polygon boundary.

Repetition translates complete polygons by lattice vectors. It does not infer
faces, automatically recolor periodic face copies, or clip polygons at supercell
boundaries. Supply the intended loops/image offsets and sufficient surrounding
atom copies for boundary-spanning faces. Camera fitting includes face vertices.
Adaptive AA conservatively uses ordinary 2× sampling for scenes with faces;
native, 2× and 4× modes are also available.

## Configurations and workers

Use inline `faces: [...]` at the root of a render configuration, or
`faces_file: "polygons.json"`. Relative paths resolve against the containing
config/job file, or the current directory for stdin and worker requests. One
layer cannot specify both. Higher layers replace the array; CLI `--faces FILE`
is the final override. An empty array clears faces. Faces belong in explicit
jobs and cannot be stored in personal defaults.

Resolved dumps contain inline faces, independent of the external file. Workers
reload referenced face files for every request and include resolved definitions
in scene-cache keys. Face changes, selected frames, bond colors and thickness
invalidate the appropriate scene; camera/image style can reuse it. Reports
include face/triangle counts. `--inspect-scene` exposes coordinates, colors,
boundary-edge flags, source face IDs and instance origins. Validation with an
input also checks indices and triangulation.

Strict JSON rejects unknown/duplicate fields and nonfinite numbers. Limits are
1 MiB per file, 4096 faces, 65536 input vertices, 262144 expanded triangles,
64 overlapping face layers per sample and 200 million tile/sample candidates.
Allocations share the existing 1 GiB budget. Cancellation/errors preserve an
existing destination. Faces and atom-colored bonds require the Z-buffer backend;
the experimental Cairo painter rejects these features.

## Bond thickness and half-bond colors

`--bond-radius ANGSTROM` already controls global thickness. Diameter is twice
the radius; the default radius is 0.12 Å. JSON uses `geometry.bond_radius_A`.
Thickness changes appearance without changing connectivity or cutoffs.

```sh
build/qasr examples/zeolite/ring.xyz --bond-colors atoms --bond-radius 0.06 \
  --cell off -o colored-bonds.png
```

`--bond-colors uniform` preserves grey bonds. `--bond-colors atoms` colors each
half from its endpoint atom, with the color change at the bond midpoint. Both
endpoint atoms must be in the displayed cells, including any included xy
neighbors; see [bond endpoints and cell-line styles](cell-lines.md). Endpoint colors
include element settings, source overrides, periodic-copy transforms and
cell-specific overrides. Alpha follows the endpoint too. One uninterrupted
capped cylinder supplies both halves, avoiding an internal cap, gap or outline.

```json
{"geometry": {"bond_radius_A": 0.06, "bond_colors": "atoms"}}
```

All four styles support colored halves in color mode. ORTEP's default uniform
bonds remain black; atom-colored mode uses shaded endpoint colors. Monocolor
and dualtone modes continue to apply their dither palette instead of atom RGB.
