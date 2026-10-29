# Third-party notices

The project's original code, documentation, presets, tooling and original test
fixtures are licensed under GPL-3.0-only; see `LICENSE`.
Copyright (C) 2026 Ivo Filot and contributors.
Upstream materials retain their own copyright and license notices.

| Component | Location | License and provenance |
| --- | --- | --- |
| cJSON 1.7.19 | `vendor/cjson/` | MIT; Dave Gamble and cJSON contributors. Full notice in `vendor/cjson/LICENSE`. |
| Atom Architect parameter snapshot | `vendor/atom-architect/` | LGPL-3.0-or-later; Ivo Filot. Pinned source and notices in that directory, including LGPL and GPL texts. |
| Generated Atom Architect color/radius table | `src/element_defaults.inc` | Derived from that LGPL-3.0-or-later snapshot; provenance retained in its header and vendor README. |
| pymatgen OUTCAR fixtures | `tests/fixtures/OUTCAR.static`, `OUTCAR.vasp5.gz`, `OUTCAR.vasp6.gz` | MIT; original notice in `tests/fixtures/PYMATGEN-LICENSE`, pinned sources/hashes in the fixture README. |

Atom Architect's C++ source is retained as parameter-policy reference and is
not compiled. The C renderer and its periodic/raster algorithms are implemented
in this repository. cJSON is compiled into the executable. libpng, zlib and the
system C/math runtimes are system dependencies, not vendored copies; binary
packages use the system's shared libraries.

Optional `ENABLE_CAIRO_EXPERIMENTS` builds also link system Cairo (LGPL-2.1
or MPL-1.1, with its upstream per-file notices). Cairo and its transitive
shared libraries are not vendored or included in binary archives. The painter
prototype is original project code and remains subject to the project's GPL.

Optional `ENABLE_LIBDEFLATE` builds link system libdeflate (MIT), not a vendored
copy. Optional `ENABLE_OPENMP` builds link the compiler's OpenMP runtime (GCC
uses libgomp under GPL-3.0 with the GCC Runtime Library Exception). These shared
libraries are system dependencies and are not copied into binary archives.

The supplied slab CONTCAR snapshots are original user-provided fixture data
included under the project's GPL-3.0-only license. Their original byte content
and calculation provenance remain preserved in `tests/fixtures/slabs`.
