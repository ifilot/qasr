# Atom Architect parameter snapshot

Source: https://github.com/ifilot/atom-architect

Pinned commit: `9e73e51055df8eb8b893a5895e7faadc39f46b25` (retrieved 2026-10-29).
Copyright (C) 2020–2026 Ivo Filot. The upstream project specifies
LGPL-3.0-or-later; the original source notice is retained in `atom_settings.cpp`.
`LICENSE` is the upstream LGPL text, and `COPYING.GPL` supplies its incorporated
GPL text. The renderer implements its own geometry/rasterization algorithms.

- `atoms.json`: unmodified `assets/configuration/atoms.json`.
- `atom_settings.cpp`: unmodified `src/data/atom_settings.cpp`, retained to make
  the numeric bond policy reproducible. It is reference material, not compiled.
- `src/element_defaults.inc`: generated colors and display radii from that JSON.
- `src/elements.c`: equivalent unordered pair policy derived from the constructor.

The effective default distances in angstroms are:

| Pair | Cutoff |
| --- | ---: |
| H with atomic number ≤20 (including H-H) | 1.2 |
| H with atomic number >20 | 2.0 |
| Other pairs, both atomic numbers ≤20 | 2.0 |
| Other pairs, exactly one atomic number ≤20 | 2.5 |
| Both atomic numbers >20 | 3.0 |
| Al-C exception, either order | 3.5 |

The upstream `Atom::dist()` returns Euclidean distance; the constructor's
thresholds are not squared distances. Display radii are not used to infer bonds.
JSON/CLI overrides retain precedence. Unlike upstream's strict `<` comparison,
this renderer retains the specified inclusive cutoff, with a tolerance of
`32 * DBL_EPSILON * max(1 Å, cutoff)` after coordinate conversion. Periodic
neighbor enumeration and boundary clipping are implemented independently.

This preset, named `atom-architect-9e73e510`, supersedes the provisional
`visual-v1` palette and pair table in the M0 design. Camera, lighting and bond
radius remain this project's documented defaults.
