# Performance work after v0.1.0

v0.1.0 is the reference-renderer checkpoint before workstation optimization.
M4 compares renderer/anti-aliasing strategies, M5 compares PNG encoding and
threading, and M6 validates deployment/performance alongside overlays and
production hardening. Execute performance selection on a representative
workstation, then validate on the intended native Linux/HPC deployment.

## Historical development observations

An earlier shaded-renderer build was measured on an Intel Core Ultra 7 258V
laptop under WSL2 Linux x86-64, using a v9fs filesystem, GCC 13.3.0 Release,
libpng 1.6.43 and zlib 1.3. Each case used 20 warm-cache process-lifetime
samples after three warmups, one thread, 512×512 RGBA, fixed 2× supersampling
and PNG compression 1. CPU frequency and other host activity were uncontrolled.

| Case | Median (ms) | p95 (ms) |
| --- | ---: | ---: |
| Synthetic Co, 100 atoms | 106.50 | 118.77 |
| Synthetic Co, 500 atoms | 149.80 | 191.52 |
| Synthetic Co, 1,000 atoms | 175.80 | 186.61 |
| Co slab repeated 2×2×2, 504 atoms | 75.96 | 81.00 |
| Pd/ceria, 249 atoms | 45.00 | 49.12 |
| Static OUTCAR, 2 atoms | 43.94 | 67.60 |
| Relaxation OUTCAR, 6.2 MB / 7 atoms | 266.46 | 289.33 |

These observations predate illustrated contours and adjustable cell widths.
They are not measurements of v0.1.0, release acceptance thresholds or a basis
for selecting a workstation backend. Raw samples/generated images remain local
Git-ignored artifacts. The large OUTCAR suggested parsing as a separate
bottleneck; the 1,000-atom case suggested rasterization and encoding work.

## Workstation procedure

1. Build and run the full correctness suite with the workstation compiler/libs.
2. Characterize CPU, governor, OS, compiler flags, shared-library versions,
   filesystem and memory; record input and executable hashes.
3. Establish a fresh baseline for both shaded and illustrated top views,
   including contours/cell boundaries and representative slabs/repetitions.
4. Compare algorithms in stages, retaining images and rejecting incorrect
   occlusion, changed connectivity, alpha halos or unacceptable silhouettes.
5. Measure complete process latency and isolated stages, reporting median,
   p95, samples, RSS and PNG bytes. Separate warm cache from genuinely
   controlled cold-cache tests.
6. Set workstation latency targets from that baseline. Earlier laptop targets
   in historical artifacts are exploratory and do not govern the next phase.

`tools/benchmark.py` supplies the existing warm-cache cases and raw report;
extend it for illustrated presets and additional workstation comparisons.
`tools/render_examples.py` reproduces the visual slab collection. All generated
results belong under `artifacts/`, which remains excluded from Git/packages.
