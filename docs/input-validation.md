# Input expansion validation

The v0.5.0 release validates these readers with the full CMake/CTest suite,
including all pinned XML fixtures. See [release validation](release-v0.5.0.md).
The records below describe the earlier PR environment and its limitations.

## Native builds

The managed environment has a C11 compiler and runtime libraries, but no CMake or
development packages. Builds therefore compiled the CMake source list directly
with GCC, C11, `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`, with generated
version/schema headers. No new package dependency was added to the repository.
Build-only libpng/libdeflate headers were obtained from their upstream sources;
libxml headers came from the installed lxml header distribution. These temporary
headers/build helpers are not part of the change. The CMake path itself was not
executed in this environment.

- Full build: XML, OpenMP and libdeflate enabled. All 11 Python suites (142 test
  groups, including the optional Cairo skip) and both C gates passed, subject to
  the XML fixture limitation below. Existing fixed decoded-pixel hashes, all 20
  slab renders and independent periodic/ray reference comparisons passed.
- Minimal build: XML, OpenMP and libdeflate disabled. The same 13 test entries
  passed with the suites' normal capability-dependent skips. The 17 new input
  test groups passed in this build too.
- Address/undefined-behavior build: new input tests, scene/render tests, periodic
  context and available XML tests all passed (63 test groups). LeakSanitizer cannot inspect processes in this
  sandbox and was disabled with a build-local shim. Sanitizer startup warnings
  also contaminate strict JSON stderr assertions in the configuration/worker
  suites; a full clean sanitizer-suite claim is not made.

## Coverage and limits

The 17 new test groups cover all eight PBC masks against an independent Cartesian
image-distance oracle; lower-dimensional tilted cells; missing/degenerate cells;
nonperiodic display cells; wrapping, bond clipping and replication; changing atom
count/cell/PBC across frames; property-column reordering and validation; malformed
and truncated inputs; unit conversion; Gaussian frame-family selection; AMS
fractional coordinates; identical molecule pixels from XYZ/Gaussian/AMS inputs;
and job schema/report/cache behavior. All fixtures in this new suite are original
synthetic examples. AMS coverage is based on the documented text syntax, not a
real AMS output compatibility claim.

A separate local smoke check read the real Gaussian16 `1615.log` from
[cclib-data](https://github.com/cclib/cclib-data/blob/master/Gaussian/Gaussian16/1615.log),
blob `dc2357f60e7427d74b6ef81e7bb64f04d517f0ec`. Its two atoms matched independently
extracted orientation rows. The matching `1615.gjf`, blob
`47152cee875202f9b7359e020a327281f10f849b`, uses a Z-matrix and was correctly
rejected as outside the Cartesian-input subset. Neither external file is
vendored or downloaded by the regression suite.

The existing `vasprun.relax.xml.gz` fixture exceeds the connector's file-content
limit and could not be materialized. The first unmodified XML run consequently
failed that fixture hash check. A local harness then ran the same 19 XML test
groups with only that fixture omitted from the real-fixture list; the two other
pinned real XML fixtures and all synthetic XML cases passed. The committed XML
tests and all original fixture references are unchanged. A normal checkout should
run the full suite, including that fixture, before merge.

Mirrored/embedded job and worker schemas, updated website manifest hashes, unique
handbook HTML IDs and `git diff --check` were verified. Full release-website archive
regeneration was not run; the handbook explicitly identifies these capabilities
as development additions and retains the immutable v0.4.0 release download.

## Normal checkout commands

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure

cmake -S . -B build-minimal -DENABLE_XML=OFF -DENABLE_OPENMP=OFF -DENABLE_LIBDEFLATE=OFF
cmake --build build-minimal
ctest --test-dir build-minimal --output-on-failure
```
