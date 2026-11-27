# QASR v0.8.1

The illustrated style supports 1–8 flat shades on atoms and molecular orbital
surfaces. Three shades remain the default, and one shade uses a single midtone.
Contour width is configured independently.

```sh
qasr examples/orbitals/co.xyz --no-user-config \
  --config examples/orbitals/recipes/co-02.json --shade-bands 5 -o orbital.png
```

Set `image.shade_bands` in a recipe to reuse the same count across renders. See the
[molecular orbital guide](molecular-orbitals.md) for details.

Configuration and job schema versions remain 1. The
[v0.8.0 release notes](release-v0.8.0.md) cover point-group symmetry elements.
