# Orbital rendering reference

`core-reference.png` is the accepted 480×400 illustrated CO MO 2 render using the
checked-in RHF/STO-3G PyQInt/PyTessel mesh, analytic normals and 4× AA. It verifies
that self-occlusion contours do not add marks to a smooth core surface. The test
compares decoded RGBA pixels, so PNG compression is not part of the contract.

The input recipe is `examples/orbitals/recipes/co-02.json`; molecular coordinates
are in `examples/orbitals/co.xyz`. See that example's manifest for package versions,
energies, isovalue and sampling. Generated in this repository; GPL-3.0-only.
