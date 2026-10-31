# Bond endpoints and unit-cell line styling

Bonds always require both endpoint atom instances to be present in the
displayed cells. This removes stubs pointing to periodic
images that were not included. Connections between two displayed cells remain
complete with `--repeat` or `--expand-xy`; the rule applies at the outer boundary
of the requested context. Integer lattice origins identify the same instances
as the spheres, avoiding rounded Cartesian boundary tests. Nonperiodic directions
retain their supplied atom coordinates.

Distance cutoffs and `base_bond_count` still describe periodic connectivity;
`bond_count` counts the displayed bonds. Endpoint membership is mandatory and
has no CLI, configuration or worker override. Clipped fragments to absent atoms
are not supported. Old boundary-mode settings are rejected as unknown options.

For example, a bond crossing the primary cell's left boundary is omitted when
only that cell is displayed. With `--expand-xy 1,0`, the left neighboring atom
exists and the complete bond is drawn. Bonds leading beyond the outermost
included neighboring cells are still omitted.

## Dashed and outlined cell edges

```sh
build/qasr tests/fixtures/slabs/01_co0001_clean/CONTCAR \
  --config presets/illustrated.json \
  --cell-style dashed --cell-color '#000000' --cell-width 2 \
  --cell-outline-width 1.5 --cell-dash-length 8 --cell-dash-gap 5 \
  -o structure.png
```

For a solid line with the white surround, select `--cell-style solid`.
`--cell-outline-width 0` disables the surround. Dimensions are output pixels,
independent of camera scale and antialiasing factor. A two-pixel center with a
1.5-pixel contour has a five-pixel overall width. Dash phase starts at each
original edge's starting vertex and is independent of tiles and threads.
The white contour surrounds sides and ends and shares the line's alpha.
Adjacent white surrounds may meet across a small gap while black dashes remain
separated. End-on edges appear as capped points.

| CLI | JSON field in `overlays` | Default | Range |
| --- | --- | --- | --- |
| `--cell-style` | `cell_style` | `solid` | `solid`, `dashed` |
| `--cell-color` | `cell_color` | `#464646` | `#RRGGBB` or `#RRGGBBAA` |
| `--cell-width` | `cell_width` | `2` | Greater than 0, at most 32 |
| `--cell-outline-width` | `cell_outline_width` | `0` | 0–8 pixels per side |
| `--cell-dash-length` | `cell_dash_length` | `8` | 1–256 pixels |
| `--cell-dash-gap` | `cell_dash_gap` | `5` | 1–256 pixels |

```json
{
  "overlays": {
    "cell": true,
    "cell_style": "dashed",
    "cell_color": "#000000",
    "cell_width": 2,
    "cell_outline_width": 1.5,
    "cell_dash_length": 8,
    "cell_dash_gap": 5
  }
}
```

The fields work in layered configs, personal defaults, jobs, dumps and workers.
Cell-color and expansion changes invalidate the scene cache; stroke styling
can reuse it, with camera fitting and rendering resolving the new pattern.
Cell strokes and white contours remain depth-tested against atoms and bonds.
Adaptive AA uses the full 2× grid for styled cell edges. The experimental Cairo
painter accepts the same options, retaining its existing surface-order limits.

See the [comparison gallery](../examples/cell-lines/README.md) for solid, dashed,
outlined and dark-background examples.
