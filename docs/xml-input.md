# VASP XML input

The optional streaming `vasprun.xml` reader uses system libxml2 2.9 or newer.
It is enabled by default; `-DENABLE_XML=OFF` removes XML parsing and libxml2
linkage while retaining other readers. Check `qasr --build-info` for `xml`.
Decompress compressed files before passing their paths to QASR.

```sh
qasr vasprun.xml --no-user-config --inspect
qasr input.data --no-user-config --format xml --frame 0 -o first.png
qasr vasprun.xml --no-user-config --frame final --style illustrated \
  --labels element --legend on -o final.png
```

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
`--recover` remains OUTCAR-only. The complete document is validated before rendering; parse failures preserve
an existing destination.

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

## Verification and related workflows

In a test-enabled build, run:

```sh
ctest --test-dir build --output-on-failure -R '^xml_input$'
```

The tests cover frame/final-position semantics, metadata, changing/skewed cells,
malformed and bounded input, preserved destinations, and entity/network denial.
The localhost HTTP sentinel test needs permission to bind a local socket.
Pinned fixture origins, hashes and licenses are in the
[fixture README](../tests/fixtures/README.md).

For other readers and per-axis PBC, see [input formats](input-formats.md).
For playback of ionic snapshots, see [movies](movies.md): initial/final position
records do not become extra trajectory frames. Performance tools and current
results are in [performance](performance.md). The
[initial XML validation record](archive/m7-xml.md) retains the original fixture
measurements and release context.
