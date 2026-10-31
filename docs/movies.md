# Movies

QASR can stream rendered frames directly into the **shared FFmpeg libraries**.
No intermediate PNG sequence or `ffmpeg` executable is needed. Movie support is
optional and off in the default build; still-image rendering keeps its existing
interface when this feature is disabled.

## Build with movie support

Install the development libraries alongside the normal build dependencies:

```sh
sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
cmake -S . -B build-movies -DCMAKE_BUILD_TYPE=Release -DENABLE_FFMPEG=ON
cmake --build build-movies --parallel
build-movies/qasr --build-info
```

The capability report includes `"ffmpeg":true`. With `ENABLE_FFMPEG=OFF`, it
reports false, links no FFmpeg libraries, and rejects movie requests with exit
status 3. Enabling the option requires development packages to be found at
configuration time; it does not silently fall back to an image-only build.
For independent decoded-frame and timing checks in the test suite, also install
the `ffmpeg` package, which supplies `ffmpeg` and `ffprobe`. These executables are
optional validation tools and are not called by the renderer.

FFmpeg 4 or newer is supported by the library API used here; validation currently
uses FFmpeg 6. Shared library dependencies must also be installed on the machine
where the enabled executable runs. Static-only FFmpeg packages are rejected.

## Orbit a camera around one structure

```sh
build-movies/qasr examples/zeolite/cage.xyz \
  --config examples/zeolite/renders/02-lta-polygon-tiles.json \
  --no-user-config --movie orbit --movie-frames 120 --movie-fps 24 \
  --movie-axis 0,0,1 --movie-angle 360 -o orbit.mp4
```

The camera orbits the center of the displayed structure. Its initial direction
comes from the usual `--view` and `--up` settings. The axis is a world Cartesian
direction. A sphere enclosing the whole scene fixes the zoom for the complete
orbit, including every orientation between sampled frames. `--cell off` can
remove vacuum cell extents from the fitting. The camera's right/up/view basis
rotates together, so passing a pole does not cause an arbitrary orientation flip.

A complete revolution omits its duplicate final pose for smooth looping. A
partial angle includes both endpoint poses. Negative angles reverse direction.
Movie length is the encoded frame count divided by fps.

## Play a reaction path with a fixed camera

```sh
build-movies/qasr examples/movies/reaction.xyz \
  --config examples/movies/reaction.json --no-user-config \
  --movie trajectory --movie-fps 24 --movie-interpolate 3 \
  --report reaction-report.json -o reaction.mp4
```

Trajectory mode reads every supplied snapshot in source order. Supported series
are multi-frame XYZ, Extended XYZ, OUTCAR and XML ionic snapshots. A static input
can use orbit mode instead. XML playback uses the ionic snapshot series;
`initialpos` and a duplicate `finalpos` are not extra movie frames. Still-image
final-frame selection retains its existing semantics.

Every snapshot must keep the same atom count, source order, element identities,
cell-presence flags and periodic axes. Lattice vectors may vary. The camera
orientation, center and zoom stay fixed: a streaming prepass fits all source and
interpolated poses before rendering starts. This avoids zooming or recentering
as atoms move. Bonds are rebuilt at every output frame using the configured
cutoffs, allowing bonds to appear or disappear along the supplied path.

`--movie-interpolate 3` inserts three linearly interpolated Cartesian geometries
between adjacent snapshots. With N source snapshots and K inserted geometries,
the output contains `(N - 1) * (K + 1) + 1` frames. Cell vectors interpolate
linearly too; every intermediate geometry must remain valid. This provides
visual smoothing, not a calculated reaction path, atom mapping or molecular
dynamics. FPS sets playback speed, without assuming a physical time step.

For a continuous molecular reaction, cell-free XYZ is convenient. Periodic
inputs retain the renderer's wrapping rules; crossing a periodic boundary may
cause a visible jump. Prepare continuous positions and nonperiodic axes when
that is the intended presentation. QASR does not infer periodic unwrapping.

The supplied H + H2 exchange path is **synthetic illustrative geometry**. It
has no calculated energies, forces, transition state or physical time scale.

## Controls

| Option | Values and default |
| --- | --- |
| `--movie` | `orbit` or `trajectory`; no movie by default. |
| `--movie-fps` | Integer 1–120; default 30. |
| `--movie-frames` | Orbit only, 2–100000; default 120. |
| `--movie-axis` | Orbit only, finite nonzero X,Y,Z; default 0,0,1. |
| `--movie-angle` | Orbit only, nonzero degrees from −3600 to 3600; default 360. |
| `--movie-interpolate` | Trajectory only, 0–1000 inserted frames per interval; default 0. |
| `--movie-codec` | `auto` (default), `h264` or `mpeg4`. |

The destination must end in `.mp4`. Width and height must be even and at most
4096 each, subject to the renderer's memory budget. Use an opaque background;
MP4 output has no alpha channel. Drawing styles, element/atom overrides,
atom-colored bonds, polygon faces and overlays use the same renderer as images.
The experimental Cairo painter is unsupported for movies.

`auto` prefers FFmpeg's `libx264` H.264 encoder and otherwise uses native MPEG-4
Part 2. Explicit `h264` requires the linked FFmpeg build to contain `libx264`;
explicit `mpeg4` requests the native encoder. Unavailable encoders fail clearly.
Encoding uses YUV 4:2:0, BT.709 limited-range color; it is not lossless RGB.

Trajectory mode rejects `--frame` and the orbit-only controls. Orbit mode may
select its source snapshot with `--frame` but rejects interpolation controls.
PNG encoding options affect still images, rather than the movie encoder.

## JSON jobs and batch requests

Movie controls belong to an invocation's `movie` object, outside its rendering
`config`. Personal preference files and ordinary rendering recipes contain
render settings rather than movie scheduling options.

```json
{
  "input": "reaction.xyz",
  "output": "reaction.mp4",
  "no_user_config": true,
  "movie": {"mode": "trajectory", "fps": 24, "interpolate": 3},
  "config": {
    "image": {"width": 640, "height": 360},
    "camera": {"view": [0, 0, 1], "up": [0, 1, 0]},
    "overlays": {"cell": false}
  }
}
```

Run `qasr --job movie-job.json`. File paths resolve relative to the job file.
Batch `render` requests accept the same job object. Movie jobs clear retained
still-image caches and stream their own source snapshots. Worker interruption
and normal CLI SIGINT/SIGTERM cancellation remove an unfinished movie.

`--report FILE` records movie mode, requested controls, actual encoder, frame
count, duration, dimensions, input/output hashes, settings, camera and fit versus
render/encode timings. `--benchmark-stages` prints movie timing totals on stderr.
The destination is replaced only after successful frame encoding, encoder flush,
MP4 trailer and file close. As with images, a separately requested report is an
independent file commit and can fail after the movie itself has been written.

The input must remain unchanged while the movie is rendered. Content hashes are
checked between passes and before committing the output. Memory usage retains
only the necessary snapshots, scene, frame and encoding workspace, rather than
a complete movie or image sequence.
