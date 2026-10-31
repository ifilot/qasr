# QASR v0.5.0 release — 2026-10-30

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../../README.md)
> for supported behavior and setup.

QASR v0.5.0 adds more geometry inputs, optional JPEG/WebP output and new drawing
styles. The executable remains `qasr`; configuration and job schema versions
remain 1. See the [quick start](../../../README.md) for installation and a first render.

## Included since v0.4.0

- **Geometry input:** cell-free molecules, wires, slabs and explicit per-axis
  periodicity. Native XYZ/Extended XYZ, Gaussian Cartesian input and orientation
  output, and AMS text System readers join the existing VASP readers. No ASE or
  additional reader library is required. Wrapping, bonds, cell outlines and
  replication respect the supplied cell and periodic axes. Inspection and
  reports expose input format, frames, cell vectors and periodicity.
- **Image output:** independently optional system libjpeg and libwebp support.
  Enable `ENABLE_JPEG` and/or `ENABLE_WEBP` at build time; the output suffix
  selects the codec for CLI renders, JSON jobs and cached workers. JPEG uses
  quality 90 and 4:4:4 sampling; WebP preserves RGBA losslessly. Encoding errors
  preserve an existing destination and remove temporary output.
- **Drawing:** ORTEP-style spherical atoms with visible great-circle sections,
  and BALSAC-inspired smooth spheres and silhouettes. Each supports color,
  black/white monocolor and navy/gold dualtone variants through `--tone` and
  `image.tone`. Output-pixel ordered dithering retains visible dots under
  supersampling and deterministic output across thread counts. Six presets and
  sample images are included.
- **Documentation:** a shorter README for users, updated format/style guides,
  and a regenerated offline handbook with v0.5.0 source, schemas, examples and
  checksums. The legacy v0.3.0 source archive remains unchanged.

## Compatibility and limits

Default builds retain PNG output and require the existing system dependencies.
JPEG and WebP default to OFF and require their respective development packages
when enabled. JPEG rejects any nonopaque rendered pixel; use an opaque
background. WebP has a 16383-pixel limit per axis and a conservative workspace
allowance within the existing allocation budget. PNG tuning applies to PNG only.

The new input readers support documented geometry subsets, not all files from
Gaussian or AMS. Gaussian Z-matrices, ONIOM and checkpoint files, and AMS/ADF
output/RKF files remain unsupported. See [input formats](../../input-formats.md).

ORTEP depicts spheres, not thermal displacement ellipsoids. ORTEP and BALSAC
require the default Z-buffer backend; the experimental Cairo painter rejects
them. Dither tones apply to atoms and bonds, preserving alpha; antialiasing may
blend palette colors at edges. See [drawing styles](../../render-styles.md).

## Release validation

Validation uses a complete Linux/WSL checkout with all pinned XML fixtures:

- Default Release build: 16 CTest entries.
- JPEG, WebP and Cairo enabled: 18 CTest entries.
- Deployment checks: private installation, shared libraries, binary archive and
  checksums, source manifest, fresh source rebuild/tests, and a serial build
  with XML/libdeflate disabled.
- Cross-feature checks: 108 input/style/codec combinations through CLI, JSON jobs
  and the cached worker, plus partial periodicity and transparency behavior.
- Regenerated handbook: local links, CLI reference, served/embedded schemas,
  rendered examples, source checksums and the unchanged legacy archive.

Reproduce the main release checks:

```sh
cmake -S . -B /tmp/qasr-v0.5.0-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qasr-v0.5.0-release --parallel
ctest --test-dir /tmp/qasr-v0.5.0-release --output-on-failure
python3 tools/validate_deployment.py /tmp/qasr-v0.5.0-release \
  --output artifacts/v0.5.0/deployment/report.json
python3 tools/check_documentation.py /tmp/qasr-v0.5.0-release/qasr
```

The handbook source snapshot omits its own website to avoid recursive archives.
Full source packages include the standalone handbook. Local release logs,
packages and deployment evidence live under `artifacts/v0.5.0/`.
