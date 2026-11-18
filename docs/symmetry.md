# Point-group symmetry elements

QASR can draw where one symmetry operation of a molecule lies: the axis of a
rotation, the plane of a reflection, both for an improper rotation, or the
centre of an inversion. One image shows one operation. The element is real
scene geometry, so atoms hide the part of an axis behind them and a translucent
plane tints only what lies behind it.

See the [gallery](../examples/symmetry/README.md) for fourteen rendered recipes.

## Quick start

From the repository root, with a newly built QASR:

```sh
build/qasr examples/symmetry/nh3.xyz --style illustrated --cell off \
  --view 3,-5,2 --symmetry C3 --symmetry-axis 0,0,1 -o ammonia-c3.png

build/qasr examples/symmetry/benzene.xyz --style illustrated --cell off \
  --view 3,-5,3 --symmetry sigma_h --symmetry-axis 0,0,1 -o benzene-sigma-h.png

build/qasr examples/symmetry/ethane.xyz --style illustrated --cell off \
  --view 3,-5,1.5 --symmetry i -o ethane-inversion.png
```

| Option | Meaning |
| --- | --- |
| `--symmetry OPERATION` | `E`, `i`, `Cn`, `Sn`, `sigma`, `sigma_h`, `sigma_v` or `sigma_d` |
| `--symmetry-axis X,Y,Z` | Rotation axis, or the normal of a mirror plane |
| `--symmetry-origin X,Y,Z` | A point on the element in angstroms; default is the centroid of the atoms |

## Operations

| Operation | Drawn element | Direction required |
| --- | --- | --- |
| `E` (also `C1`) | Label only | No |
| `Cn`, n = 2..99 | Rod along the axis | Yes |
| `sigma`, `sigma_h`, `sigma_v`, `sigma_d` (also `S1`) | Translucent plane | Yes, the plane normal |
| `Sn`, n = 3..99 | Rod plus the perpendicular plane | Yes |
| `i` (also `S2`) | Ringed dot at the centre | No |

The suffix of a mirror plane only selects its label. `S1`, `S2` and `C1` keep
the label you wrote but draw as the equivalent mirror plane, inversion centre
and identity. A UTF-8 `σ` is accepted in place of `sigma`.

## Recipes

Use a root-level `symmetry` object in a render configuration or job. Only
`operation` is always required.

```json
{
  "symmetry": {
    "operation": "C2",
    "axis_atoms": [3, 0],
    "axis_style": "dashed",
    "label": "C_2'"
  }
}
```

Give the direction in exactly one way:

- `axis` (or its alias `normal`): a vector in Cartesian coordinates.
- `axis_atoms`: two zero-based source atom indices; the axis runs through both.
- `plane_atoms`: three atom indices; the direction is normal to their plane.
  A mirror plane or the plane of an improper rotation passes through them.

Give the position in at most one way: `origin_A`, a point in angstroms, or
`origin_atoms`, 1 to 64 indices whose centroid is used. Without either, the
element passes through the unweighted centroid of all atoms; for `axis_atoms`
and atom-defined planes it is the point on that line or plane nearest to the
centroid. Atom indices refer to the selected frame, as for polygon faces.

| Field | Default | Meaning |
| --- | --- | --- |
| `operation` | Required | See the table above |
| `axis`, `normal`, `axis_atoms`, `plane_atoms` | None | Direction; exactly one for rotations, improper rotations and mirrors |
| `origin_A`, `origin_atoms` | Centroid | Position; at most one |
| `verify` | `true` | Fail unless the operation maps the structure onto itself |
| `tolerance_A` | `0.1` | Largest accepted atom-to-image distance, in (0, 10] |
| `label` | Automatic | Replacement text of at most 31 bytes; `_` starts a subscript |
| `show_label` | `true` | Draw the label |
| `label_scale` | `1` | Multiplies the label size, 0.25 to 8 |
| `label_font` | `italic` | `italic` (serif italic, the textbook convention) or `sans` (upright) |
| `label_color` | `#181c20` | Opaque `#RRGGBB` |
| `axis_style` | `solid` | `solid` or `dashed` |
| `axis_radius_A` | `0.045` | Rod radius in angstroms |
| `axis_color` | `#7a2f8f` | Rod and inversion-marker color |
| `plane_shape` | `auto` | `auto`, `rectangle` or `disc` |
| `plane_color` | `#f0a63a59` | Fill, `#RRGGBB` or `#RRGGBBAA` |
| `plane_outline_color` | `#b8741a` | Boundary color |
| `plane_outline_width` | `1.5` | Boundary width in output pixels, 0 to 8 |
| `padding_A` | `0.7` | How far the axis and plane extend beyond the atoms |

A higher configuration layer replaces the whole object, and `"symmetry": null`
clears an inherited one. The three CLI options then override individual fields;
`--symmetry-axis` also replaces an atom-defined direction. Like faces, a
symmetry operation belongs to a specific structure and cannot be stored in
personal defaults. `qasr --schema config` documents the object.

## Verification

Before drawing, QASR applies the operation to every atom and finds the nearest
atom of the same element. If the largest distance exceeds `tolerance_A` the
render fails with exit code 5 and names the worst atom:

```text
qasr: symmetry: C4 does not map the structure onto itself: atom 2 (H) lands
0.485 A from the nearest like atom (tolerance_A 0.1). Check the axis/origin,
raise tolerance_A, or set verify false
```

This catches a wrong axis, origin or order before it reaches a figure. Raise
`tolerance_A` for an imperfectly relaxed geometry, or set `verify` to `false`
to draw the element regardless. Use `--validate-config` with an input to check
an operation without rendering. Reports and `--inspect-scene` include a
`symmetry` object with the resolved `origin_A`, unit `axis` and
`max_deviation_A`. Verification compares all atom pairs and is refused above
roughly 14 000 atoms.

## Appearance

The axis extends `padding_A` beyond the outermost atoms along it, and at least
three quarters of the molecular radius on each side so that the axis of a flat
molecule stays visible. It keeps its flat color in every drawing style and
gains the ink contour of the illustrated, ORTEP and BALSAC styles. A dashed
axis uses 0.32 Å dashes with 0.20 Å gaps.

The plane is a rectangle aligned with the principal directions of the atoms
projected onto it, or a disc when that projection is nearly isotropic, as for
benzene or BF₃. `plane_shape` overrides the choice. Planes are ordinary
[polygon layers](polygon-faces.md): two-sided, flat-colored and blended in depth
order with the nearest atom or bond surface.

Labels are drawn from outline glyphs embedded in the executable, with a thin
white surround: STIX General Italic by default and DejaVu Sans for
`label_font: "sans"`. They cover printable ASCII and `σ`; other characters
appear as `?`. No font file or library is needed at run time.

The label continues beyond the upper tip of an axis, or sits outside the
upper-right boundary of a plane; the automatic camera fit makes room for it.
Identity is labelled in the upper-left corner. The inversion marker and its
label are drawn on top of the image so that they remain visible when an atom
occupies the centre.

## Animation

With FFmpeg support (`-DENABLE_FFMPEG=ON`), `--movie symmetry` animates the
operation instead of drawing a still image:

```sh
build/qasr examples/symmetry/ethane.xyz --style illustrated --cell off \
  --view 3,-5,1.5 --symmetry S6 --symmetry-axis 0,0,1 --labels index \
  --movie symmetry --movie-frames 150 -o ethane-s6.mp4

build/qasr --job examples/symmetry/s6-movie-job.json
```

The atoms and bonds move while the axis, plane, marker and label stay in place.
The camera is fixed and fitted to every pose, the element and its label.

| Operation | Motion |
| --- | --- |
| `Cn` | Rotation by 360°/n about the axis |
| `sigma` | Each atom moves straight through the plane to its mirror image |
| `Sn` | The rotation, a short pause, then the reflection |
| `i` | Each atom moves straight through the centre to the opposite side |
| `E` | No motion |

Every movie holds the first and last pose for a moment and eases the motion in
and out. `--movie-frames` sets the total length (default 120) and
`--movie-fps` the playback rate. Because the operation maps the molecule onto
itself, the last frame looks like the first; add `--labels index` to follow
individual atoms.

Bonds are found once, in the starting geometry, and then follow their atoms, so
they neither break nor form while atoms pass through a plane or the centre.
During a reflection or inversion the molecule is flattened or shrunk at the
halfway point; that is the straight-line path, not a physical motion. The
operation is verified before the first frame, exactly as for a still image.
The orbit-only `--movie-axis` and `--movie-angle` and the trajectory-only
`--movie-interpolate` are rejected. See [movies](movies.md) for codecs, sizes
and jobs.

## Limitations

- Operations are specified, not detected: QASR does not determine the point
  group of a structure.
- The operation acts on the atoms of the primary cell as displayed. Periodic
  images are neither matched during verification nor used to size the element.
  For a molecule in a simulation box, add `--cell off`.
- Fixed framing with `camera.extent_A`, and orbit or trajectory movies, do not
  reserve room for the label; symmetry movies do.
- An animation shows the operation once (for example C₃, not C₃²).
- Symmetry elements require the Z-buffer backend.
