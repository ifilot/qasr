# v0.2.0 release — 2026-10-29

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../../README.md)
> for supported behavior and setup.

This release completes design milestones M4–M6: renderer/AA/encoder/thread
experiments, selected production defaults, annotations and hardening. The
user-approved deployment and performance environment is WSL2 Linux x86-64
on the i9-10900K workstation. M7 XML is subsequent work and is excluded from
the v0.2.0 tag.

The accurate Z-buffer uses cached projected geometry and deterministic tiles,
automatic OpenMP workers, uniform 2× AA and libdeflate PNG output. Automatic
compression selects level 0 on native storage and level 1 on WSL 9p mounts.
Optional adaptive AA, alternate encoders and Cairo experiments retain explicit
controls; incorrect painter depth and alpha-quantizing paths were rejected.
Visible-surface element/index labels, Cartesian axes, element counts and a
physical scale bar reserve their layout before camera fitting.

All 20 unannotated baseline images retain exact decoded RGBA. Thirty-sample
warm-cache measurements pass 34 scoped performance targets, with up to 9.9×
median speedup over the reference. See [performance evidence](../performance-v0.1-v0.2.md)
and [operating limits](../m6-validation.md). Generated evidence remains under
Git-ignored `artifacts/m4-m6/`; archives and benchmark data are not committed.

Release validation uses the versioned executable and packages:

```sh
cmake -S . -B /tmp/qasr-v0.2.0-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qasr-v0.2.0-release -j 10
ctest --test-dir /tmp/qasr-v0.2.0-release --output-on-failure
python3 tools/validate_deployment.py /tmp/qasr-v0.2.0-release \
  --output artifacts/v0.2.0/deployment/report.json
cmake -S . -B /tmp/qasr-v0.2.0-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_SANITIZERS=ON -DENABLE_CAIRO_EXPERIMENTS=ON
cmake --build /tmp/qasr-v0.2.0-sanitize -j 10
ctest --test-dir /tmp/qasr-v0.2.0-sanitize --output-on-failure
```

Acceptance covers all 63 base / 67 all-feature test groups, leak-enabled
address/undefined-behavior checks, installation to a private prefix with spaces,
runtime dependency resolution, binary/source archive hashes, offline source
build/tests and exact annotated output in a serial/libpng-only fallback build.
Leak checking requires execution outside the ptrace sandbox restriction.
The version-only release update does not change rendering or pixel references.

The default build needs a C11 compiler, CMake 3.16+, libpng/zlib, libdeflate,
pkg-config and OpenMP; Python 3 is used by tests/tools. Cairo is optional and
disabled by default. Installed binaries use shared system runtimes; archives
are Linux packages, not portable static binaries. GPL-3.0-only and upstream
notices accompany both package forms.

XML, broader OUTCAR version/real-MD coverage, layered transparency and M7 are
outside this release. Measurements do not imply cold-cache, fsync durability
or all-host performance guarantees. The user authorized a release commit,
push and annotated `v0.2.0` tag; no hosted release asset upload is required.
