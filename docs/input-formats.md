# Source-independent geometry input

Available since **v0.5.0**. Check `--build-info` for `nonperiodic: true` and
`partial_pbc: true`. Earlier releases support VASP input only. No ASE, Python runtime,
Gaussian installation, AMS installation or new library is required by the readers.
Python remains a test/tooling dependency only.

## Supported readers

| `--format` | Input | Geometry and selection |
| --- | --- | --- |
| `poscar` / `contcar` | VASP POSCAR/CONTCAR | Existing scaling/species rules; one fully periodic frame. |
| `outcar` | VASP OUTCAR | Existing ionic frames and opt-in recovery policy. |
| `xml` | VASP vasprun.xml | Existing optional libxml2 reader and final-position semantics. |
| `xyz` | Plain XYZ | Element and three Cartesian coordinates in Å; nonperiodic, no cell; concatenated frames. Also recognizes Extended XYZ geometry headers. |
| `extxyz` | Extended XYZ geometry subset | Per-frame cell/PBC and typed property columns; concatenated frames. |
| `gaussian-input` (`gjf` / `com` aliases) | Gaussian Cartesian text input | One geometry; optional freeze codes and `TV` translation vectors. |
| `gaussian-output` | Gaussian text `.log` / `.out` | Complete standard-orientation tables, or input-orientation tables if no standard tables occur. |
| `ams` | AMS text input | One unnamed `System` with explicit `Atoms` and optional `Lattice`. |

Detection uses bounded content probes, not filename extensions. Explicit
`--format` is useful when a recognizable header lies beyond the probe. A valid
POSCAR numeric header retains precedence over an unrestricted title that resembles
another format. AMS inputs may start with comments. Shell run scripts are not AMS
input files and are not interpreted or executed. Compressed files must be
explicitly decompressed first.

All readers normalize atomic numbers and Cartesian coordinates in Å, preserve
source atom order, reject non-finite/out-of-range geometry and enforce existing
line, input-byte, atom and frame limits. Per-frame source index means array order;
it does not assert persistent atom identity across a trajectory.

`--frame N` is zero-based and defaults to `final`. Later recognized frames are
still validated when selecting an earlier one. XYZ/Extended XYZ and Gaussian
orientation sequences may change atom count; Extended XYZ may also change cell
and periodicity. Readers retain selected/current geometry rather than the whole
trajectory. `--species` applies only to VASP readers. `--recover` remains specific
to OUTCAR.

## Cells and periodicity

Geometry has three independent `cell_present` flags and three `pbc` flags.
A periodic direction requires its corresponding nonzero cell vector. Present
vectors must be finite, independent and sufficiently well conditioned. A cell is
optional for molecules; a display cell does not imply periodicity. One or two
present vectors describe a wire or slab without inventing a physical vacuum cell.

Inspection keeps `lattice_A` as three rows, using zeros for absent vectors, and
adds `cell_present` and `pbc`. Render reports expose the same fields under `input`;
scene inspection exposes both flag arrays. Internal completion vectors used for
coordinate calculations are never exposed as physical cell vectors.

Source coordinates remain unwrapped. Scene construction wraps only periodic
axes, generates bond images only along those axes, and clips bonds only at
periodic boundaries. Nonperiodic bonds use ordinary Cartesian distances. Cell
outlines draw only the supplied vectors: no cell, a line, four slab edges or
twelve full-cell edges. `--cell off` hides any supplied display cell.

`--repeat` and `--expand-xy` reject replication along a nonperiodic axis.
`--view top` needs independent a/b vectors; molecules and wires should use an
explicit view such as `--view 0,0,1` or the default oblique view. Cartesian axes,
labels, legends, atom styles and the software renderer work for every input kind.

## XYZ and Extended XYZ

Plain XYZ requires a positive atom count, a comment line (possibly empty), and
exactly one element symbol plus three coordinates per atom. Each subsequent
frame repeats this layout.

```text
3
water
O  0.00 0.00 0.00
H  0.96 0.00 0.00
H -0.24 0.93 0.00
```

Extended XYZ supports `Properties` triples `name:type:width`, with types
`S`, `R`, `I`, `L`. Require `pos:R:3` and `species:S:1` or `Z:I:1`. If both
identity columns occur they must agree. Additional columns are consumed and typed
numeric/logical values validated, but are not retained or used for styling/bonds.
Limits are 64 properties, 256 total columns and 128 header fields.

`Lattice` lists a, b, c consecutively, each with x/y/z components. `pbc` accepts
three `T/F`, `true/false` or `1/0` values. With a lattice and no explicit PBC,
all directions default to periodic, matching common Extended XYZ conventions;
without a lattice they default to nonperiodic. Always specify `pbc` when writing
partial-periodic structures or display cells.

```text
2
Lattice="3 0 0 1 3 0 0 0 0" pbc="T T F" Properties=species:S:1:pos:R:3
Si 0.0 0.0 -2.0
O  1.5 1.5 -1.0
```

Headers support unquoted values or single/double-quoted values; duplicate keys
are errors. Bracket/braced quoting, escaped quotes, bare boolean header keys,
quoted atom string fields and arbitrary serialized objects are outside this
subset. Unknown `key=value` frame metadata are ignored. Positions are always Å;
an arbitrary metadata field does not change coordinate units.

## Gaussian

Cartesian `.gjf`/`.com` inputs support Link 0 lines, route/title sections,
charge/multiplicity, element symbols or atomic numbers, simple numbered element
labels such as `C1`, optional `0`/`-1` freeze codes, and whitespace/comma separators.
Coordinates default to Å; `Units=Bohr` / `Units=AU` convert to Å and
`Units=Angstrom` is explicit. Parenthesized units and whitespace around `=` work.
One to three trailing `TV x y z` rows define periodic lattice directions in the
same units. Connectivity and basis data after the geometry are ignored under
input limits. No checkpoint is opened and Link 0 directives are never executed.

Z-matrices, variable/expression coordinates, ONIOM annotations, ghost/dummy atoms,
checkpoint-only geometry and multi-job `--Link1--` inputs are unsupported and
fail instead of guessing. Binary `.chk` and formatted `.fchk` are not readers in
this increment.

Output tables must declare `Coordinates (Angstroms)` and have complete delimiters
and valid atomic-number/coordinate rows. Frame counts refer to the chosen
orientation-table family, not optimization steps or convergence. A complete table
can be read from an unfinished calculation; a truncated recognized table is an
error. Periodic/ghost centers are rejected. Other output sections, energies,
forces, convergence and normal termination are not interpreted.

## AMS

The geometry subset follows the
[AMS System specification](https://www.scm.com/doc/AMS/System.html):

```text
Task SinglePoint
System
  Atoms
    O  0.00 0.00 0.00
    H  0.96 0.00 0.00
    H -0.24 0.93 0.00
  End
End
Engine DFTB
  Model GFN1-xTB
EndEngine
```

Keywords are case insensitive. `Atoms` and `Lattice` accept `[Angstrom]` (default)
or `[Bohr]`. One/two/three lattice rows enable one/two/three periodic directions.
`FractionalCoords True` converts periodic components through the lattice; the
remaining coordinates keep their Cartesian length units. As in AMS, fractional
chains require x alignment and fractional slabs require the xy plane. Cartesian
input supports arbitrary independent cell vectors.

Trailing `key=value` atom annotations are ignored for geometry. `Charge` is
accepted and ignored for rendering. Other System keywords, duplicate geometry
blocks, named/multiple systems, external `GeometryFile` references and incomplete
blocks are rejected. Task/engine settings outside System are ignored. AMS/ADF
text output and binary RKF files are not supported by this reader; export geometry
to XYZ/Extended XYZ for those sources. This is a geometry reader, not an AMS
input interpreter.

## Extending the input layer

`src/input.h` defines input-only options; format readers do not consume renderer
configuration. `input_read()` dispatches through a built-in reader table and
validates normalized geometry. `parse_structure()` is the invocation wrapper
that applies replication limits. `SourceMetadata` groups format/frame/recovery
information separately from atomic geometry. A new reader should populate this
contract, validate every recognized frame and supply fixture-backed tests.

CIF, XSF/AXSF, PDB, LAMMPS and SDF/MOL remain follow-up readers. In particular,
CIF needs explicit symmetry, occupancy and disorder policies before it is safe
to label as supported; molecular bond-order formats may require optional
source-provided connectivity beyond the current inferred bond model.
