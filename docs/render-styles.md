# ORTEP spherical and BALSAC-inspired drawing

For polygon faces, configurable bond thickness and atom-colored half-bonds,
see the [feature guide](polygon-faces.md) and [zeolite examples](../examples/zeolite/README.md).
Dashed unit-cell lines and white surrounds are described in the
[cell-line guide](cell-lines.md).

Use `--style ortep` or `--style balsac`, or set JSON `image.style`.
Available since **v0.5.0**. See the [handbook](https://ifilot.github.io/qasr/#rendering)
for behavior, presets and backend limitations. Both styles use the Z-buffer
renderer; the experimental Cairo backend rejects them. ORTEP draws spheres
with great-circle sections, not thermal displacement ellipsoids.

Add `--tone monocolor` for black/white dithering or `--tone dualtone` for
navy/gold dithering. The shared atom/bond palette ignores element RGB and retains
alpha; `--tone color` restores the normal appearance. Dots use an output-pixel
8×8 Bayer matrix, shared across supersamples. Cell edges, backgrounds and
annotations use their configured appearance. Antialiasing blends edge colors.

| Style | Color | Monocolor | Dualtone |
| --- | --- | --- | --- |
| ORTEP | ![ORTEP color](website/assets/co-ortep.png) | ![ORTEP monocolor](website/assets/co-ortep-monocolor.png) | ![ORTEP dualtone](website/assets/co-ortep-dualtone.png) |
| BALSAC | ![BALSAC color](website/assets/co-balsac.png) | ![BALSAC monocolor](website/assets/co-balsac-monocolor.png) | ![BALSAC dualtone](website/assets/co-balsac-dualtone.png) |

## Render style comparisons

A style flag selects the drawing treatment; a preset can also set colors,
camera and dimensions. Run from the source root with a built executable:

```sh
mkdir -p artifacts/style-previews
build/qasr tests/fixtures/slabs/01_co0001_clean/CONTCAR --no-user-config \
  --config presets/ortep.json --width 640 --height 480 \
  -o artifacts/style-previews/ortep.png
build/qasr tests/fixtures/slabs/01_co0001_clean/CONTCAR --no-user-config \
  --config presets/balsac-dualtone.json --width 640 --height 480 \
  -o artifacts/style-previews/balsac-dualtone.png
```

To regenerate the handbook figures and their provenance, follow the
[handbook maintenance procedure](website/README.md). Writing individual preview
renders into the website assets would bypass its recipe/hash manifests.

## Validation

In a test-enabled build, run the style and related geometry suites:

```sh
ctest --test-dir build --output-on-failure \
  -R '^(render_styles|faces_bonds|cell_lines)$'
```

These cover style/tone combinations, sampling, alpha, depth, schema/config/job
interfaces and thread determinism. The [v0.6.0 release record](release-v0.6.0.md)
contains the complete build matrix; the
[initial style-validation account](archive/render-style-validation.md) retains
its original environment and limitations.
