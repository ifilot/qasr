# v0.1.0 release — 2026-10-29

## Scope

Reference-renderer checkpoint covering M0–M3, illustrated top views, black
atom/bond contours, depth fading and adjustable visible cell boundaries.
The version is `0.1.0` throughout CMake, generated executable metadata,
changelog and package filenames. Original project material is GPL-3.0-only;
third-party files retain their notices. Generated artifacts remain untracked.

The release targets Linux x86-64 with a C11 compiler, CMake 3.16+, libpng/zlib
development packages and shared system runtimes. Python 3 is needed for tests
and developer tools, not for the installed executable.

## Validation procedure

```sh
cmake -S . -B build-v0.1.0-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-v0.1.0-release
ctest --test-dir build-v0.1.0-release --output-on-failure
cmake --install build-v0.1.0-release --prefix "$PWD/artifacts/v0.1.0-install"
python3 tools/package_source.py
cpack --config build-v0.1.0-release/CPackConfig.cmake -B artifacts/releases
```

Release acceptance additionally requires the Debug and address/undefined-
behavior/leak sanitizer suites, the installed executable/preset smoke test,
binary package inventory, a clean build/test from the unpacked source archive,
SHA-256 verification and exclusion of generated/local files from Git/packages.
Leak checking needs execution outside this session's ptrace sandbox restriction.
No performance experiments are part of these release checks.

## Architecture review scope

The local `architecture-simplifier` release-gate review covers CMake targets,
the internal C headers/implementations, allocation/cleanup boundaries, tests
and packaging. Vendored cJSON internals and upstream reference C++ are outside
the original-code review scope. The executable has eight geometry/config/
render/output C units in one internal static target and a CLI translation unit;
cJSON is a separate static target, with shared libpng/zlib and C/math runtimes.
The source scan counted 22 direct quoted include edges across 17 original C/
header files; generated/vendor include leaves were not expanded. No before/
after coupling metric is claimed. The read-only review itself made no
architectural edits; version, license and packaging changes are release work.

There is no installed C API. Structure owns atoms, Scene owns primitives,
Image owns pixels, and png_io owns its temporary stream/path. The CLI releases
all of these on success/failure. Headers follow a value/config-to-scene-to-
render direction; no original-code include cycle was found in this scope.

Representative change locality:

- Bond-default changes affect elements plus parameter tests; parser ownership
  and the PNG path do not depend on the cutoff policy.
- Replacing PNG encoding affects png_io/build linkage and I/O tests; it does
  not require changing lattice or neighbor construction.
- A new display setting touches config, CLI inspection/help and its renderer
  consumer/tests. Config is deliberately shared within this small CLI; splitting
  it into adapters is not justified for the current scope.

Potential future maintenance work includes separating periodic construction
from camera code if either grows, profiling before adding neighbor-list
complexity, and evolving experimental settings without expanding the stable
CLI unnecessarily. No architectural refactor is required for this release.

## Known limitations and workstation handoff

- One renderer/thread, fixed 2× AA and libpng compression level 1. Backend/AA/
  codec/threading choices are reference settings pending workstation comparison.
- Optional labels, axes, legend and scale bar are unavailable and fail explicitly.
- XML input, stdout PNG, layered transparent surfaces and all-version OUTCAR
  compatibility are outside v0.1.0. Real MD fixtures remain a validation gap.
- Explicit memory/work limits can reject very large or highly skewed scenes;
  these are documented bounds, not throughput guarantees.
- Pixel goldens target a fixed software environment. Native workstation/HPC
  builds and performance have not been verified by the laptop/WSL checks.

Start the next phase using [the workstation procedure](performance.md).
M4/M5 are performance experiments; M6 also contains feature completion and
production hardening. Fresh workstation baselines replace exploratory laptop
targets before performance decisions are made.

## Release result

**Architecture verdict: Ready with caveats**, within the v0.1.0 reference-
renderer scope. No Critical/High architectural finding was identified in the
reviewed original code. The documented native-platform/real-MD/performance
gaps remain later validation work; no workstation claim is implied.

Validation completed on 2026-10-29 with GCC 13.3.0, libpng 1.6.43, zlib 1.3,
Python 3.12.3 and CMake 3.28.3 in WSL2 Linux x86-64:

- Clean Release build and CTest: both entries / all 40 groups passed.
- Debug build and CTest: both entries / all 40 groups passed.
- Address/undefined-behavior/leak sanitizer build and CTest: both entries /
  all 40 groups passed, with leak checking outside the ptrace restriction.
- `cmake --install` to `artifacts/v0.1.0-install`: installed executable reports
  exactly `vasp-render 0.1.0`; rendering with the installed illustrated preset
  succeeds, including visible 3-pixel cell boundaries.
- CPack Linux TGZ: executable, preset, documentation and upstream notices are
  present; the executable extracted into `/tmp` reports the correct version.
- Source TGZ: 76 explicitly allowlisted files; SHA-256 and every per-file hash
  verified. No artifacts/builds, Git metadata, local agent/credential settings
  or Python caches are included.
- A separate Release configure/build/test from the extracted source archive
  in `/tmp` passes both CTest entries / all 40 groups, including the real slab
  fixtures and unchanged shaded pixel goldens.
- Git ignore checks exclude generated release packages and local settings.
  Version/licensing/package work did not change the renderer's algorithms.

Local package files and checksums are generated under `artifacts/releases`;
they are deliberately not committed. The source archive and its manifest are
regenerated from the final release files before the local release commit/tag.
No remote publication or push is part of this release preparation.
