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
| pymatgen XML fixtures | `tests/fixtures/vasprun.*.xml.gz` | MIT; original notice in `tests/fixtures/PYMATGEN-LICENSE`, pinned sources/hashes in the fixture README. |

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

Optional `ENABLE_XML` builds link system libxml2 (MIT), not a vendored copy.
The system's shared libxml2 and its transitive dependencies must be available
at runtime; they are not copied into binary archives.

The supplied slab CONTCAR snapshots are original user-provided fixture data
included under the project's GPL-3.0-only license. Their original byte content
and calculation provenance remain preserved in `tests/fixtures/slabs`.

Optional `ENABLE_JPEG` builds link the system libjpeg API implementation,
commonly libjpeg-turbo (IJG, BSD-style and zlib licenses; consult the distribution's
package notices for the selected implementation). Optional `ENABLE_WEBP` builds
link system libwebp (BSD-3-Clause). Neither library nor its transitive shared
libraries are vendored or copied into binary archives; install the corresponding
runtime packages when distributing a build with these options enabled.

Optional `ENABLE_FFMPEG` builds dynamically link the system libavcodec,
libavformat, libavutil and libswscale libraries. FFmpeg is LGPL-2.1-or-later by
default; GPL components such as libx264 make the selected distribution's build
GPL. Consult that system package's license/configuration and notices. QASR does
not vendor FFmpeg, libx264 or their runtime dependencies. Enabled binaries need
the matching shared libraries installed; builds with this option OFF link none
of these libraries. QASR's own code remains GPL-3.0-only.

The idealized LTA coordinate data in `examples/zeolite/LTA.cif` come from the
IZA Structure Commission's Database of Zeolite Structures, credited to
Ch. Baerlocher and L. B. McCusker. The original source header is preserved.
Converted geometry and illustrative ring-face selections are documented in
`examples/zeolite/README.md`, with source links and access date. The idealized
geometry is not a new experimental structure determination by this project.
