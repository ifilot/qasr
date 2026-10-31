# QASR architecture

QASR is a standalone C11/POSIX command-line application. It reads atomic geometry,
builds a displayed scene, and produces orthographic images or optional MP4 movies.
The supported interfaces are the CLI, JSON jobs and a sequential JSONL worker;
there is no installed C library ABI or network service.

For usage, start with the [documentation index](README.md). The
[original design and milestone record](archive/design-history.md) explains earlier
decisions, including superseded behavior; this page describes the implementation.

## Build and module boundaries

CMake builds the internal `geometry` static library, vendored `cjson`, and the
`qasr` executable. Default dependencies are libpng/zlib, libdeflate, libxml2 and
OpenMP. JPEG, WebP, FFmpeg and experimental Cairo support are separate build
options. `qasr --build-info` reports the compiled capabilities.

| Responsibility | Main source/interface |
| --- | --- |
| CLI inspection, dispatch and diagnostics | `src/main.c`, `src/common.h` |
| Layered settings, validation and serialization | `src/config.c`, `src/config_json.c`, `src/config.h` |
| Input-only reading and normalized geometry | `src/parse.c`, `src/input_readers.c`, `src/xml_parse.c`, `src/input.h`, `src/structure.h` |
| Atom rules, periodic instances, bonds, faces and camera fit | `src/atom_style.c`, `src/scene.c`, `src/faces.c`, `src/scene.h` |
| Depth-buffer rendering, polygon composition and annotations | `src/render.c`, `src/face_render.c`, `src/overlay.c`, `src/render.h` |
| RGBA ownership and image encoding | `src/image.h`, `src/image.c`, `src/image_io.h`, `src/png_io.c`, `src/image_io.c` |
| Still-image jobs, content-sensitive cache and JSONL lifecycle | `src/job.c`, `src/worker.c`, `src/job.h` |
| Movie scheduling and private FFmpeg encoder | `src/movie.c`, `src/movie.h` |
| Provenance hashing and report/file utilities | `src/sha256.h`, `src/automation.c` |

Configuration owns movie settings validation and JSON serialization; it does not
depend on the movie renderer. Image codecs depend on the image/configuration
interfaces rather than rasterization. FFmpeg handles stay private to `movie.c`.
Concrete `Structure`, `Scene`, `Camera` and `Image` structs are intentional internal
value types. Their lifecycle contracts are documented at header declarations.

## Data flow and geometry

The CLI and worker share the job rendering path. Invocation settings are adapted
to `InputOptions`; readers do not consume render configuration. Readers preserve
source atom order and Cartesian positions in angstroms. Lattice vectors are rows,
with `Cartesian[j] = sum(fractional[i] * lattice[i][j])`. Physical cell presence
and per-axis periodicity are separate flags; completion vectors used internally
for lower-dimensional geometry are not reported as physical cell vectors.

Scene construction resolves atom styles, wraps periodic coordinates, creates
requested instances, and enumerates periodic connectivity under pair-specific
cutoffs. A displayed bond requires both endpoint instances to exist. Expanding
the context restores connections to included neighbors; fragments toward absent
atoms are omitted. This rule has no override. Nonperiodic axes do not generate
image bonds or permit replication.

Faces are supplied ordered loops, not inferred cages. Scene construction validates
their vertices/indices and triangulates boundaries. Camera fitting includes the
displayed atoms, bonds, supplied faces and enabled cell edges. See
[input geometry](input-formats.md), [faces](polygon-faces.md), and
[endpoint/cell behavior](cell-lines.md) for the caller-facing rules.

The Z-buffer renderer resolves atom/bond surfaces per sample. Polygon composition
orders face layers with the nearest atom/bond surface; it does not introduce
transparent-atom volume rendering. Opaque faces can hide label anchors. Sampling,
tone, annotation and optional-backend constraints are described in the
[drawing guide](render-styles.md) and handbook.

## Ownership and retained state

Readers transfer atom ownership on success; failure paths release partial input.
Streaming observers borrow a snapshot only during their synchronous callback.
A callback may run before a later file-validation error, so observing a frame is
not a successful transaction for the whole input.

Scenes own their primitives, triangles and optional rule stamps. Rendered image
pixels have independent ownership and are released with `image_free`. A
`RenderWorkspace` exclusively owns reusable sampling buffers; initialize it to
zero, reuse it sequentially and free it even after failure. Retained movie
buffers are charged before subsequent scene allocations.

Worker caching is bounded and content-sensitive. Source and scene keys depend
on the input content and relevant resolved settings, not filenames/timestamps
alone. Camera/image changes can reuse geometry. Movie requests evict the
still-image cache and run streaming validation/framing/rendering passes. Camera
orbits keep one fit; trajectory playback fits the complete path before rendering.
See [worker behavior](batch-worker.md) and [movies](movies.md).

## Errors, resources and concurrency

Internal boolean/pointer failure paths fill `Error`; the CLI can serialize it
with `--json-errors`. Image and movie destinations are replaced after successful
encoding/close. Reports are separate commits: hashing/report failure can follow
a successfully written output. Atomic rename does not promise filesystem
durability or protection against concurrent input changes.

Source/frame/instance/work limits and an aggregate 1 GiB allocation budget bound
registered scene/render allocations. This is not a universal process-RSS cap;
libraries and runtime state also use memory. Retained allocations remain part
of the budget during buffer reuse.

OpenMP parallelizes work inside an individual render. Job execution within one
worker remains sequential. Process-global cancellation and scoped FFmpeg logging
also require serial in-process job use; separate workers need separate output
paths and an appropriate CPU budget.

Existing correctness tests cover input subsets, independent geometry/pixel
oracles, styles, faces, cells, codecs, movies, worker state and allocation/error
paths. Build/test commands are in the [quick start](../README.md), and comparison
methods are in [performance](performance.md). The dated
[architecture/contract review](archive/code-polish.md) records the cleanup that
established the narrower image/hash/configuration boundaries.
