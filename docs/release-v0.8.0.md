# QASR v0.8.0 release — 2026-11-18

QASR v0.8.0 draws where a point-group symmetry operation of a molecule lies.
One image shows one operation.

## Symmetry elements

| Operation | Drawn element |
| --- | --- |
| `Cn` | Rod along the rotation axis |
| `sigma`, `sigma_h`, `sigma_v`, `sigma_d` | Translucent mirror plane |
| `Sn` | Rod plus the perpendicular plane |
| `i` | Ringed dot at the inversion centre |
| `E` | Label only |

```sh
qasr examples/symmetry/nh3.xyz --style illustrated --cell off \
  --view 3,-5,2 --symmetry C3 --symmetry-axis 0,0,1 -o ammonia-c3.png
```

- `--symmetry`, `--symmetry-axis` and `--symmetry-origin` cover the common
  case. A root-level `symmetry` object in a recipe or job adds axes through two
  atoms, planes through three atoms, origins at the centroid of chosen atoms,
  dashed axes, plane shape, colors and custom labels with subscripts.
- The element is scene geometry: atoms hide the part of an axis behind them and
  a plane tints only what lies behind it. The automatic camera fit makes room
  for the label.
- Before drawing, QASR checks that the operation maps every atom onto an atom
  of the same element within `tolerance_A` (0.1 Å). A wrong axis, origin or
  order fails with exit code 5 and names the worst atom. `verify: false` draws
  the element anyway. Reports and `--inspect-scene` record the resolved origin,
  axis and largest deviation.
- Labels use outline glyphs embedded in the executable, STIX General Italic by
  default and DejaVu Sans on request. No font file or library is needed.

## Animated operations

With FFmpeg support, `--movie symmetry` animates the operation: atoms and bonds
rotate about the axis, pass through the mirror plane or the inversion centre,
or rotate and then reflect for an improper rotation, while the element and its
label stay in place under a fixed camera. Add `--labels index` to follow
individual atoms.

```sh
qasr examples/symmetry/ethane.xyz --style illustrated --cell off \
  --view 3,-5,1.5 --symmetry S6 --symmetry-axis 0,0,1 --labels index \
  --movie symmetry -o ethane-s6.mp4
```

## Smoother annotations

Atom labels, the element legend, axis letters and the scale-bar label now use
the embedded DejaVu Sans outlines instead of the 5×7 bitmap font. Atom labels
no longer sit on a white plate: one lettering size is chosen per image from
the smallest labelled atom (never smaller than before), and each label is dark
or light depending on the pixels beneath it, with a thin surround in the
opposite shade.

Label placement is now continuous. A label sits at the centroid of the part of
its atom that is actually visible, read from the depth buffer, and its opacity
follows how much of the surrounding half-radius is visible. Previously it used
the atom centre or the first visible of eight fixed points, which made labels
jump between frames of a movie and overhang the atom in front. Labels now
glide and fade as atoms pass behind one another, and their size no longer
depends on which atoms happen to be visible. A mostly covered atom, or one
whose bond points at the viewer, loses its label instead of displacing it.
How colliding labels are dropped is unchanged.

See the [symmetry guide](symmetry.md) and the
[fourteen-image gallery](../examples/symmetry/README.md).

## Compatibility

The executable remains `qasr`; configuration and job schema versions remain 1.
Renders without annotations and without a `symmetry` operation are unchanged;
images with atom labels, a legend, axes or a scale bar differ in their lettering. `--inspect-scene` gains
the primitive kind `symmetry_axis` and, with an operation, a `symmetry` object.

## Limitations

- Operations are specified, not detected; QASR does not determine point groups.
- Verification and sizing use the atoms of the primary cell as displayed.
- Fixed framing (`camera.extent_A`) and orbit or trajectory movies do not
  reserve room for the label.
- An animation shows the operation once, not its powers.
- Symmetry elements require the Z-buffer backend.

## Validation

- `tests/test_symmetry.py` checks axis and plane geometry against the input
  coordinates, verification failures, canonical forms (`S1`, `S2`, `C1`),
  atom-defined directions, invalid definitions, CLI layering and dumps, label
  placement, both label fonts, all drawing styles, thread determinism and
  worker scene-cache invalidation.
- `tests/test_movies.py` decodes symmetry movies and checks the held start and
  end, the motion between them, the stationary axis, the two stages of an
  improper rotation and the rejected option combinations.
- `tests/test_overlays.py` slides one atom in front of another under a fixed
  camera and checks that the label moves by small steps, only away from the
  occluder, and fades.
- The orbital reference image and the CO orbital gallery were regenerated; the
  reference differs from its predecessor only inside the scale-bar label.
- The complete CTest suite passes with and without FFmpeg, and the symmetry tests and gallery also run
  under the address and undefined-behaviour sanitizers.
