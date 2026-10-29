# M7: streaming vasprun.xml input (v0.3.0)

M7 is included in v0.3.0, following the v0.2.0 M4–M6 checkpoint. It adds a system-libxml2
streaming reader without changing the normalized scene or rendering pipeline.
XML is enabled by default; `-DENABLE_XML=OFF` keeps POSCAR/OUTCAR support and
removes libxml2 linkage. `--build-info` reports `xml: true|false`. The v0.2.0 tag excludes these changes; v0.3.0 includes them.

```sh
cmake -S . -B /tmp/qasr-m7-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qasr-m7-release -j 10
/tmp/qasr-m7-release/qasr vasprun.xml --inspect
/tmp/qasr-m7-release/qasr input.data --format xml --frame 0 -o first.png
/tmp/qasr-m7-release/qasr vasprun.xml --frame final \
  --config presets/illustrated.json --labels element --legend on \
  --axes on --scale-bar on -o final.png
```

System libxml2 2.9+ development headers are needed for the default build. They
are already installed on the accepted WSL workstation (2.9.14). The reader uses
[libxml2's pull-reader API](https://gnome.pages.gitlab.gnome.org/libxml2/html/xmlreader_8h.html),
and recognizes geometry/metadata containers described by the
[VASP XML format documentation](https://vasp.at/wiki/Vasprun.xml).

## Selection and normalization

The document must have a single unnamespaced `modeling` root and complete
`atominfo` before simulation-cell structures. The canonical `atoms` per-ion
table provides the element and one-based potential-type index. Declared atom
and type counts must match; `atomtypes`, when present, is cross-checked for
population and element agreement. Potential types may share an element.
Original per-ion order is preserved, including non-grouped element ordering.
An explicit `--species` list must match the potential types; it never silently
overrides a conflicting identity. Unrelated output, including energies, forces,
DOS/eigenvalues and velocities, is streamed past without interpretation.

Each accepted structure has its own three-vector Cartesian basis in angstroms
and exactly the declared number of fractional position vectors. The reader
converts positions with that basis, validates finite values, coordinate ranges
and lattice conditioning, and passes Cartesian atoms to the existing pipeline.
Fortran D exponents are supported in geometry numbers. Wrapped cell positions
are still a scene-construction concern; source coordinates are preserved.

- `--frame N` selects a zero-based unnamed ionic structure directly under
  `calculation` or `modeling`. The entire document is still validated to EOF.
- `initialpos`, named `primitive_cell` structures and nested SCF structures
  do not enter the ionic count. Unsupported named simulation-cell structures
  are rejected rather than guessed.
- `--frame final` (the default) selects `finalpos` when present, otherwise the
  last complete ionic structure. A distinct final position does not add a
  duplicate frame; its reported frame index is the last ionic index.
- A final-only document supplies logical frame 0 and frame_count 1; numeric
  frame 0 also selects it. An initial-only document has no result frame and
  fails. If numeric selection is used in a trajectory, a subsequent finalpos
  is validated but does not override the explicitly selected ionic geometry.

A well-formed closing `</modeling>` is required. Incomplete/malformed XML is
always rejected, including after a requested frame was already found.
`--recover` remains OUTCAR-only. Validation completes before any output file
is created/replaced, so failures preserve an existing destination.

## Limits and external input

The reader retains one current structure plus one selected snapshot: O(atoms),
independent of ionic-frame count. No DOM, subtree expansion or output-table
accumulation is used. Existing limits apply: 1,000,000 atoms, 118 potential
types, 10,000,000 ionic frames, 64 GiB input and 64 KiB physical lines. XML
adds maximum depth 64, 200,000,000 visited nodes, 32 attributes per element,
256-byte element/attribute names and 64 KiB node-text/attribute/scalar values.
libxml2's normal parser limits also remain enabled. Allocation failures have
bounded diagnostics and release both geometry buffers and the reader.

The application owns the binary file stream and enforces byte/line/NUL limits;
libxml2 does not open the input as a URI or implicitly decompress it. Decompress
`.gz` files first. UTF-16/NUL-containing files and XML namespaces are unsupported.
DTD loading, entity substitution, validation, XInclude, recovery and HUGE mode
are disabled. NONET is enabled, and DTD/entity-reference nodes are rejected;
NONET alone is not relied on to prevent local-file entity access. Predefined
XML escapes and numeric character references remain normal XML text.

## Evidence and reproduction

`tests/test_xml.py` adds 19 groups: capability/fallback detection, format/BOM
handling, final/numeric semantics, per-ion/type metadata, ignored primitive/SCF
blocks, changing/skewed geometry, malformed/truncated/oversized input, preserved
destinations, and external entity/network denial. The network test runs a local
HTTP sentinel and verifies no requests occur; it needs localhost socket binding.
Release and leak-enabled address/undefined-behavior checks cover these cases.

Three unmodified MIT-licensed pymatgen fixtures are pinned to commit
`682bfd855dd89264b0ff8d6bee4815f8834cc5ac`; source names, compressed hashes and
licensing are in [the fixture README](../tests/fixtures/README.md). They cover
VASP 5.2.2 relaxation (25 atoms, 29 frames), VASP 6.3.2 Si MD (64 atoms, 10 frames)
and VASP 6.0.8 dielectric output (30 atoms, one frame). An independent ElementTree
oracle checks metadata and first/last/final geometry. Equivalent POSCARs produce
exactly matching decoded RGBA both with and without annotations. These checks
establish those fixture-backed XML modes, not universal VASP version coverage
or new real-MD coverage for the separate OUTCAR parser.

```sh
ctest --test-dir /tmp/qasr-m7-release --output-on-failure
python3 tools/benchmark_xml.py /tmp/qasr-m7-release/qasr \
  --samples 30 --output artifacts/m7/benchmark.json
python3 tools/validate_deployment.py /tmp/qasr-m7-release \
  --output artifacts/m7/deployment/report.json
```

Benchmark evidence retains 30 raw warm-cache samples after three warmups,
process median/p95, parse-stage timing, input/binary hashes, build/library/host
provenance, separate RSS probes and render PNGs. The i9-10900K WSL/GCC 13.3
run used libxml2 2.9.14 and native temporary storage. These are observations,
not new shared-CI latency gates; process timings include inspection JSON or PNG
output, and PNG completion excludes fsync. Governor/host load remain uncontrolled.

| Input | Expanded bytes | First/final inspection median | Shaded 512px median | Illustrated 768px median |
| --- | ---: | ---: | ---: | ---: |
| Relaxation, 29 frames | 12,685,837 | 80.44 / 80.28 ms | 97.24 ms | 116.28 ms |
| MD, 10 frames | 195,000 | 4.63 / 4.90 ms | 18.66 ms | 35.89 ms |
| VASP 6 static | 1,467,238 | 13.10 / 13.09 ms | 30.46 ms | 47.91 ms |

Inspection RSS is about 6.7 MiB. A separate equal-geometry trajectory comparison
with 100 versus 10,000 frames measured 6,796 versus 6,956 KiB peak RSS while
validating to EOF for frame 0, supporting bounded retained state. Raw data and
validation/deployment logs live under ignored `artifacts/m7/`.
