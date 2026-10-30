# v0.5.0 PR compatibility check

This integration branch combines the three PR heads below without changing
master or any of the individual PR branches.

| PR | Head checked |
| --- | --- |
| #1 — optional JPEG/WebP output | cd752eeab7a1e0a09d982a8d07249cb573bc817b |
| #2 — generalized inputs and partial PBC | f3ee9a119f356fe5dd8153d531ce625c0e0326c5 |
| #3 — ORTEP/BALSAC and dithered tones | cd4cf4692c6f5a0aef39329c6bbc49ac29793970 |

Common base: 44fc8ba8372ecd77a2e83c5eba8f7348a014de5c.

## Merge compatibility

None of the three pairs merges cleanly as written. Git's three-way merge was
checked using exact common-base and PR-head contents for every touched text
path. Added image paths do not overlap with another PR.

| Pair | Conflicting files |
| --- | --- |
| #1 + #2 | CHANGELOG.md, CMakeLists.txt, README.md, src/automation.c, src/main.c |
| #1 + #3 | CHANGELOG.md |
| #2 + #3 | CHANGELOG.md, docs/website/configuration-manifest.json |

For the combined build, integrate #1, then #2, then #3. This branch resolves:

- CMake's source list by including both image_io.c and input_readers.c.
- Build-info by preserving JPEG/WebP and nonperiodic/partial-PBC capabilities.
- Reports by preserving #2's input format/frame/cell/PBC metadata and #1's
  selected output format.
- README naming as atomic geometry-to-image rendering, with all three feature
  sections retained under one v0.5.0 changelog section.
- Handbook checksums by regenerating them from the combined schemas, keeping
  both the new input enums and style/tone fields. Source/served schema bytes match.

## Runtime compatibility

The combined C11/OpenMP build passes locally with optional codecs and XML
disabled: 17 input-reader, 10 style, 23 CLI/parser and available output groups.
The cross-feature test verifies 36 PNG combinations through CLI, stdin jobs and
the cached worker, plus partial-PBC rendering and transparency.

The integration-only CI workflow builds four configurations: neither codec,
JPEG only, WebP only, and both codecs with Cairo. It runs the full combined
CMake/CTest suite with XML, libdeflate and OpenMP enabled.

The additional release_compatibility test covers:

- Six input readers (POSCAR, XYZ, Extended XYZ, Gaussian input/output, AMS)
  × six ORTEP/BALSAC style/tone variants × every enabled output codec.
  The all-codec build checks 108 combinations through CLI, stdin jobs and a
  persistent cached worker, and checks their reports.
- Identical decoded pixels across equivalent input geometries; exact PNG/WebP
  RGBA equality and independent JPEG decoding with a lossy-error bound.
- Partial-PBC slabs with every style/tone/codec combination.
- PNG/WebP transparency preservation and atomic JPEG transparency rejection.

Pillow is used only by this additional validation test, not by QASR itself.
The latest CI results for this branch are the source of truth for enabled-codec
and full-suite results.
