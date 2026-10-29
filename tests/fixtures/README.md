# Input fixtures

`POSCAR.skew` and `OUTCAR.relax` are original synthetic fixtures. Expected
skew-cell Cartesian coordinates are independently checked. The test driver
also generates scaling/volume/Cartesian variants, malformed inputs, truncated
tables, a 1,000-frame MD-style stream, and randomized nonorthogonal coordinate
conversions. The synthetic version banner does not establish version support.

Three real OUTCAR files come from the MIT-licensed pymatgen project, tag
`v2025.6.14`, commit `682bfd855dd89264b0ff8d6bee4815f8834cc5ac`.
The upstream license is preserved in `PYMATGEN-LICENSE`. No POTCAR files or
pseudopotential datasets are included. Tests decompress the gzip files into
temporary directories; the CLI itself consumes plain text.

| Local file | Upstream path within `tests/files/io/vasp/` | Version and scope | Expected atoms / complete frames |
| --- | --- | --- | --- |
| OUTCAR.vasp5.gz | outputs/OUTCAR.gz | 5.2.11, Li/Ni/O cell relaxation | 7 / 5 |
| OUTCAR.vasp6.gz | outputs/OUTCAR.vasp.6.2.0.gz | 6.2.0, Fe cell relaxation | 1 / 4 |
| OUTCAR.static | fixtures/static_silicon/OUTCAR | 5.2.12, static Si | 2 / 1 |

Pinned source root:
https://github.com/materialsproject/pymatgen/tree/682bfd855dd89264b0ff8d6bee4815f8834cc5ac/tests/files/io/vasp

SHA-256 of local files (compressed bytes where applicable):

```text
94f4bcd2b859a053b077ee3454925a2846b30e0cc084fd45b6778c6701be5257  OUTCAR.vasp5.gz
607152ca8605ca2e4620f5b99812ba79d43b29e56f2e64f411859f74c941d7c0  OUTCAR.vasp6.gz
04b23098e34e2d32d35b24d2396f0366325c063bf09cb82927874154aede439c  OUTCAR.static
```

The tests check expected species/counts/frame counts and independently extract
the last POSITION table for coordinate comparison. Changing-cell selection is
checked against explicit expected bases in the synthetic fixture. Real MD,
VASP 5.4, newer 6.x, older footer variants and additional NWRITE layouts need
their own real fixtures before receiving verified compatibility claims.

## User-provided slab collection

`slabs/` contains byte-for-byte copies of all 20 CONTCAR examples supplied from
`/mnt/c/PROGRAMMING/CPP/snellius-mcp/examples/structures` on 2026-10-29.
The source README and manifest are preserved alongside them, including their
original calculation provenance and SHA-256 hashes. These are user-provided
calculation data, separate from the MIT-licensed pymatgen fixtures above.
The user included this collection in the project's GPL-3.0-only v0.1.0 release;
its source bytes, provenance and hashes remain unchanged. The separate pymatgen
fixtures above retain their upstream MIT license.

The 1,687 atoms across these structures cover Co, Rh, Ru, Pd/ceria, Ag-containing
surfaces, and Fe/C/Li/O, with 49–249 atoms per file. The regression suite checks
every file hash, atom count, ordered species, lattice vector, stable index,
and Cartesian position against the manifest and original Direct coordinates.
The tests preserve coordinates outside the primary cell. These CONTCAR files
extend real slab coverage; they do not add OUTCAR/MD version coverage or verify
calculation convergence or transition-state assignments.
