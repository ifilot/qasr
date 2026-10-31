# Architecture and contract polish

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../README.md)
> for supported behavior and setup.

This review applies `architecture-simplifier` in Refactor mode and
`commenting-auditor` in Fix mode to the feature-complete optimization revision.
The changes preserve the CLI, JSON schemas, rendering arithmetic and encoding
settings. They clarify responsibility and caller contracts rather than introduce
another rendering abstraction.

**Scope and confidence.** Inspected `src/`, CMake targets, representative tests,
benchmark drivers and current design/render/movie documentation. QASR uses C11
with POSIX APIs, an internal `geometry` static library, vendored `cjson`, and a
standalone executable; it has no installed C ABI. Vendor/generated code, the
raster/geometry algorithms, and unrelated historical milestone prose were not
rewritten. GCC 13.3 and CMake are available; clang-tidy and include-what-you-use
are not. Doxygen output was not generated. Contract statements were checked
against implementations and existing tests; tag/signature checks alone do not
prove semantic correctness.

**Architecture.** `main.c` and `worker.c` resolve invocations; `job.c` owns the
single-entry cache and still-image pipeline. `movie.c` owns frame scheduling and
its private FFmpeg encoder. Configuration owns movie validation and serialization
without depending on rendering. Input readers take `InputOptions`, scene building
produces geometry, and renderers produce independently owned `Image` pixels.
`image_io.h` exposes encoding; `sha256.h` exposes provenance hashing without
scene, renderer or JSON types. Optional library handles remain private to their
implementation files. These concrete value types and the existing input-only
reader boundary are useful and remain in place.

**Findings and applied fixes**, in priority order:

| Finding | Evidence and impact | Applied change and tradeoff |
| --- | --- | --- |
| Medium architecture: movie policy lived in the renderer | Previously `config.c` and `config_json.c` included `movie.h`, while `movie.c` included `job.h` and `job.c` invoked movies. A settings-only change crossed the renderer/job boundary. | Validation now lives at `src/config.c:14`; serialization at `src/config_json.c:7`. `src/movie.h:17` exposes rendering only. Existing config units own the policy; no new settings manager. Named movie codec/field constants replace numeric policy values. |
| Medium architecture: image encoders depended on the raster interface | `png_io.c`, `image_io.c` and the PNG benchmark used `render.h` for image data and output functions. Hashing similarly included the broad automation interface. | Added `image.h`, `image_io.h` and `sha256.h`; moved `image_free` unchanged to `image.c`. The small extra translation unit prevents codec-only consumers from pulling raster code solely to release pixels. The shared allocation limit now lives in `common.h`. |
| Major documentation: wrong function contract attachment | The CLI parsing comment was immediately above `config_json_text`, with unrelated parameter names and return semantics. | Corrected and separated the JSON, worker-request and CLI contracts at `src/config.h:95`. Owned JSON, borrowed argv and failure cleanup are explicit. |
| Major documentation: workspace and callback lifetimes were incomplete | Movie reuse retains sample allocations after some failures; frame observers can run before a later input failure. Neither should be treated as ownership transfer or a completed transaction. | `src/render.h:6` documents exclusive sequential workspace ownership, independent output pixels and failure cleanup. `src/input.h:14` documents synchronous borrowed frames, callback aborts, format-specific sequences and effects that are not rolled back. |
| Major documentation: output/report and cache contracts were incomplete | A successful file replacement can precede a report/hash allocation error; movies always return reports. Cache clearing preserves counters and its limit. | `src/movie.h:6`, `src/job.h:17` and `src/image_io.h:7` describe actual commit boundaries, ownership and failure behavior. Image format selection and validation remain distinct. Atomic replacement does not promise filesystem durability. |
| Minor documentation: historical or overly narrow comments | Element defaults referenced an internal milestone; uniform bonds do not use the color midpoint; the renderer also supports polygon layers. | Corrected the comments in `config.h`, `scene.h` and `render.h`. Added a current implementation map to `design.md`, labeling the older design as historical. |

The first two changes are small internal refactors; call sites and includes were
updated explicitly. Keeping all image functions in `render.h` would avoid three
headers, but retain unrelated renderer dependencies. Splitting every movie phase
into a new interface or making all value structs opaque would add machinery
without a current ABI, plugin or concurrency requirement, so neither was done.
The main risk was an omitted include/build source or an accidental change while
moving policy; build variants, byte comparisons and existing failure tests cover
those changes.

**Measured dependency evidence.** A script scanned direct quoted internal
includes in all `src/*.c` and `src/*.h`; generated/vendor/system leaves are
excluded. Transitive counts below include reachable internal headers, not the
origin file. This is a syntactic dependency measure, not measured build time,
runtime coupling, or a function-call graph.

| Consumer | Before | After |
| --- | ---: | ---: |
| `config.c` | 4 | 3 |
| `config_json.c` | 4 | 3 |
| `job.h` | 7 | 6 |
| `png_io.c` | 7 | 5 |
| `image_io.c` | 7 | 5 |
| `sha256.c` | 7 | 2 |
| `parse.h` | 4 | 3 |
| `movie.c` | 13 | 14 |

The source/header population changed from 37 files (23 translation units and 14
headers) to 41 (24 and 17); direct internal include edges increased from 64 to
75 because consumers now name their actual interfaces. The movie orchestrator
still needs many subsystems. Raw include count is not a health score.

Coalescing `.c`/`.h` by basename, ignoring self edges, reduced the largest
strongly connected include component from ten modules to two. The earlier
component contained `atom_style`, `automation`, `config`, `elements`, `job`,
`movie`, `overlay`, `parse`, `render`, and `scene`. The remaining components are
`scene`/`overlay` (camera layout calls and shared scene types) and
`structure`/`vec` (vector helpers using structure value types). Actual header-only
include graphs were acyclic before and after. Basename components are a locality
heuristic, not proof of recursive compilation or runtime cycles.

**Change locality.** Three representative future edits illustrate the boundaries:

- Changing a movie validation rule now touches `config.c` and its validation tests;
  it no longer requires editing/linking the FFmpeg renderer for that rule.
- Replacing a still-image codec touches `image_io.c`/`png_io.c`, optional CMake
  linkage and codec tests. Renderer/scene headers need not change. Adding a new
  selectable codec still legitimately needs configuration/schema/help changes.
- Adding a movie field touches `config.h`, config parsing/serialization,
  schema/help/docs, and the movie implementation if it consumes that field. That
  serialization boundary remains a real change hotspot; it is not hidden behind
  forwarding layers.

**Validation.** The following builds completed without compiler warnings:

```sh
cmake -S . -B /tmp/qasr-polish -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FFMPEG=ON -DENABLE_JPEG=ON -DENABLE_WEBP=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /tmp/qasr-polish -j 8
ctest --test-dir /tmp/qasr-polish --output-on-failure -j 4
ctest --test-dir /tmp/qasr-polish --output-on-failure -R '^xml_input$'

cmake -S . -B /tmp/qasr-polish-disabled -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_FFMPEG=OFF -DENABLE_OPENMP=OFF -DENABLE_XML=OFF \
  -DENABLE_LIBDEFLATE=OFF
cmake --build /tmp/qasr-polish-disabled -j 8
ctest --test-dir /tmp/qasr-polish-disabled --output-on-failure -j 4

cmake -S . -B /tmp/qasr-polish-sanitize -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DENABLE_SANITIZERS=ON -DENABLE_FFMPEG=ON -DENABLE_JPEG=ON \
  -DENABLE_WEBP=ON -DENABLE_CAIRO_EXPERIMENTS=ON
cmake --build /tmp/qasr-polish-sanitize -j 8
ctest --test-dir /tmp/qasr-polish-sanitize --output-on-failure -j 4
```

Release: all 21 groups passed across the full run and XML rerun; the initial XML
failure was a sandbox denial when its test opened a localhost HTTP server.
The rerun and sanitizer suite ran with localhost access. Optional-features-off:
21/21; ASan/UBSan with experimental Cairo: 23/23. Conditional tests skip features
that are absent in a build; these counts do not imply all optional code ran in
the disabled build. All 17 headers compiled individually with C11 and
`-Wall -Wextra -Wpedantic -Werror`, both with and without optional API macros.
A lightweight checker matched `@brief`, parameter directions/names and return
tags to 34 scoped declarations. Local evidence is in
`artifacts/code-polish/{before,after,interface-checks}.json` and
`artifacts/code-polish/check_interfaces.py`.

**Output and performance parity.** Against the preserved pre-polish binary,
all 41 workflows passed 12 balanced, shuffled AB/BA pairs after two warmups per
binary. Both executables ran from native Linux storage, using the same temporary
output path. All 30 still-image outputs (including JPEG/WebP) and 11 movie outputs
matched byte-for-byte; scene inspections matched, and movie reports (excluding
measured durations), ffprobe metadata and decoded frame hashes matched. The
geometric mean of per-case candidate/baseline median wall-time ratios was
1.002; individual median ratios ranged from 0.954 to 1.082. Every
per-case paired-bootstrap 95% ratio interval included 1.0. This round establishes
output parity with no resolved timing change; it does not claim a speedup or
prove a tight performance bound. Host load/frequency were uncontrolled. One RSS
sample per binary/case was recorded, insufficient for a memory improvement claim.

```sh
python3 tools/benchmark_features.py /tmp/qasr-features-final/qasr \
  /tmp/qasr-polish/qasr --output artifacts/code-polish/comparison.json \
  --samples 12 --warmups 2 --memory-samples 1
```

The complete local measurements, commands, source/binary hashes, outputs and
movie oracles are in `artifacts/code-polish/comparison.json`; the compact result
is `artifacts/code-polish/summary.json`. Diff review also verified that 14 existing
production bodies were unchanged apart from includes and moving `image_free`.

**Distribution checks.** A clean extraction of the source archive configured and
built with FFmpeg/JPEG/WebP enabled; its executable was byte-identical to the
benchmark candidate. A final whitespace-only source correction also rebuilt to
the same binary hash. The downloadable archive and website provenance were
refreshed, and `tools/check_documentation.py /tmp/qasr-polish/qasr` verified 289
local links, 26 rendered images, all 61 CLI flags and current/legacy source
archives. `git diff --check` and scoped whitespace checks passed. Source archives
include the new headers and `image.c`; no release tag or online deployment was
created by this cleanup.

**Remaining scope.** `Config` still collects many validated settings, the worker
and movie orchestration units remain substantial, and process-global cancellation
and FFmpeg logging require serial job execution. These are current constraints,
not claims of thread safety. Future concurrent workers would need an explicit
cancellation/logging design. Older reader/overlay helper declarations were not
converted wholesale to Doxygen. No material contract ambiguity remains in the
polished interfaces; this is a scoped review, not a repository-wide certification.
