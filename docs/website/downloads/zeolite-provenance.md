# LTA example geometry

Source: IZA Structure Commission Database of Zeolite Structures.
Credit: Ch. Baerlocher and L. B. McCusker / IZA-SC.
Accessed: 2026-10-31.

Framework: https://europe.iza-structure.org/IZA-SC/framework_coord.php?ID=138
Original CIF: https://europe.iza-structure.org/IZA-SC/download_cif.php?ID=138
The supplied LTA.cif preserves the original source header.

The source is DLS76-optimized idealized pure-silica geometry, with a cubic
cell length of 11.919 Å. It is not a measured sample or simulation result.

cage.xyz includes the periodic images needed for one complete alpha-cage
boundary: 48 Si, 72 bridging O and 144 Si–O bonds. tiles.json defines its
26 faces. Every face follows the actual alternating Si–O boundary.

ring.xyz isolates one eight-membered Si ring and its eight bridging O atoms,
followed by a rigid translation and rotation. single-face.json follows all
16 atom positions. The ring is a cell-free molecule in the rendered example.

The renderer fills the supplied polygons; it does not infer zeolite tilings
or import CIF directly. The original CIF is included as source attribution.
