# Example slab structures

20 CONTCAR snapshots copied byte for byte from existing Snellius calculations on 2026-10-29 for visualization development. Each named subdirectory contains a file named `CONTCAR`.

The selection covers Co(0001), Co(11-21), Rh(211), Ru(11-21), Pd on ceria, and an Fe/C slab with Li/O. It includes clean surfaces, molecular and atomic adsorbates, subsurface hydrogen, silver-containing surfaces, and source-labeled transition-state geometries.

All files use VASP 5-style explicit element symbols and Direct coordinates. The cells include orthogonal and skewed in-plane vectors, including negative vector components. Some coordinates lie outside the unit cell; preserve them or wrap them deliberately for display. Selective-dynamics flags and any trailing CONTCAR sections are preserved. Atom counts range from 49 to 249.

`SLAB-MANIFEST.json` records each relative filename, original remote path, SHA-256, element order and counts, lattice vectors, and basic geometry checks. Descriptions follow source directory labels and compositions. Calculation convergence and source-labeled transition states have not been independently verified.

| Directory | Description | Atoms | Element counts | Selective dynamics |
| --- | --- | ---: | --- | --- |
| [01_co0001_clean](CONTCAR.co) | Clean Co(0001), 3x3 cell | 63 | Co: 63 | No |
| [02_co0001_h2_dissociated](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/02_co0001_h2_dissociated/CONTCAR) | Co(0001), dissociated hydrogen | 65 | Co: 63, H: 2 | No |
| [03_co0001_h_hcp](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/03_co0001_h_hcp/CONTCAR) | Co(0001), hydrogen at an hcp site | 64 | Co: 63, H: 1 | No |
| [04_co0001_h_subsurface](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/04_co0001_h_subsurface/CONTCAR) | Co(0001), deep octahedral hydrogen | 64 | Co: 63, H: 1 | No |
| [05_co0001_n2_low_coverage](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/05_co0001_n2_low_coverage/CONTCAR) | Co(0001), lower nitrogen coverage | 49 | Co: 45, N: 4 | Yes |
| [06_co0001_n2_ag](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/06_co0001_n2_ag/CONTCAR) | Co(0001), nitrogen and silver | 51 | Co: 45, N: 4, Ag: 2 | Yes |
| [07_co0001_n2_high_coverage](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/07_co0001_n2_high_coverage/CONTCAR) | Co(0001), higher nitrogen coverage | 81 | Co: 45, N: 36 | Yes |
| [08_co1121_n2](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/08_co1121_n2/CONTCAR) | Co(11-21), nitrogen | 52 | Co: 48, N: 4 | No |
| [09_co1121_n2_ag](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/09_co1121_n2_ag/CONTCAR) | Co(11-21), nitrogen and silver | 54 | Co: 48, N: 4, Ag: 2 | No |
| [10_rh211_co](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/10_rh211_co/CONTCAR) | Rh(211), adsorbed CO | 70 | Rh: 66, C: 2, O: 2 | No |
| [11_rh211_c_o](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/11_rh211_c_o/CONTCAR) | Rh(211), dissociated C and O | 70 | Rh: 66, C: 2, O: 2 | No |
| [12_rh_n2_high_coverage](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/12_rh_n2_high_coverage/CONTCAR) | Rh slab, higher nitrogen coverage | 86 | Rh: 66, N: 20 | Yes |
| [13_ru1121_c](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/13_ru1121_c/CONTCAR) | Ru(11-21), carbon | 50 | Ru: 48, C: 2 | Yes |
| [14_ru1121_ch3](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/14_ru1121_ch3/CONTCAR) | Ru(11-21), methyl | 56 | Ru: 48, C: 2, H: 6 | Yes |
| [15_ru1121_h2o](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/15_ru1121_h2o/CONTCAR) | Ru(11-21), water | 54 | Ru: 48, O: 2, H: 4 | Yes |
| [16_ru1121_oh](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/16_ru1121_oh/CONTCAR) | Ru(11-21), hydroxyl | 52 | Ru: 48, O: 2, H: 2 | Yes |
| [17_pd6_ceria](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/17_pd6_ceria/CONTCAR) | Pd6 cluster on a ceria slab | 150 | Ce: 48, O: 96, Pd: 6 | Yes |
| [18_pd_ceria_co_oxidation_ts](CONTCAR.pd-ceria) | Pd/ceria with C/O adsorbates; source labeled CO oxidation TS | 249 | O: 162, Ce: 80, Pd: 4, C: 3 | Yes |
| [19_pd_ceria_o2_dissociation_ts](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/19_pd_ceria_o2_dissociation_ts/CONTCAR) | Pd/ceria with C/O adsorbates; source labeled O2 dissociation TS | 248 | O: 162, Ce: 80, Pd: 4, C: 2 | Yes |
| [20_fe_c_li2o](https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/20_fe_c_li2o/CONTCAR) | Fe/C slab with Li/O; source directory Li2O | 59 | Li: 2, C: 16, O: 1, Fe: 40 | Yes |

Load the examples in Python with pymatgen:

```python
from pathlib import Path
from pymatgen.io.vasp import Poscar

for path in sorted(Path("examples/structures").glob("*/CONTCAR")):
    structure = Poscar.from_file(path).structure
    print(path.parent.name, len(structure), structure.composition)
```

In the example kit, links to the two bundled slabs open local files; other slab links open the source fixture collection.
