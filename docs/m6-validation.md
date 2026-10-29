# M6 validation and operating scope

M6 completes element/index labels, axes, an element legend and a physical scale
bar, alongside production/deployment checks. The user approved this WSL
workstation for all acceptance testing. M7 XML and broader OUTCAR compatibility
claims remain outside this work.

## Annotation behavior

- `--labels element|index` annotates atoms with an unobscured front-surface
  sample. The center is preferred; eight samples on a 55%-radius ring provide
  alternate anchors when narrow periodic bond caps cover the center. Spheres,
  capped cylinders and cell edges all participate in the depth test. Fully
  transparent atoms do not receive labels. Index labels are zero-based rendered
  atom-instance indices, matching `instance_index` in `--inspect-scene`; repeated
  instances have distinct indices.
- `--legend on` lists element colors and counts of all rendered instances,
  including occluded layers. Entries follow atomic-number order and flow into
  additional columns as needed. Swatches use a white backing for alpha colors.
- `--axes on` draws projected Cartesian X/Y/Z directions in red/green/blue. An
  axis along the line of sight uses a dot toward the viewer or a cross away.
- `--scale-bar on` chooses a 1/2/5 × power-of-ten length in Å from the fitted
  orthographic pixels-per-angstrom scale. Inspection reports the physical and
  pixel length, caption and position.

The legend reserves a right column and axes/scale bar reserve a footer before
camera fitting. Geometry stays inside the remaining viewport; inspection
reports it with a lower-left origin. Text uses original embedded glyphs with
fractional coverage and straight-alpha source-over compositing. Backing plates
are translucent white for contrast, including on transparent backgrounds.
Labels are anchored on visible atom surfaces. Foreground-first, stable
collision filtering omits overlapping label plates; larger images can fit more
labels. Stage diagnostics report both drawn and collision-skipped counts. No annotation is enabled by
default, and disabled annotations preserve all original baseline pixels.

Annotations require dimensions of at least 64 pixels and enough remaining
viewport space for the requested panels. Insufficient layouts fail before
creating output. Label visibility is limited to 200 million potential
primitive tests across all nine samples per atom. Visibility flags and transient rendering/encoder buffers are
included in the existing 1 GiB allocation budget. Optional rejected Cairo
rendering experiments do not provide these annotations; the selected Z-buffer
does, including in the serial/libpng-only build.

```sh
build-release/vasp-render CONTCAR -o annotated.png \
  --config presets/illustrated.json --labels element \
  --legend on --axes on --scale-bar on
```

The same controls are available in JSON `overlays`. All 20 supplied slab inputs
have both plain and annotated 768×768 galleries, hashes and camera metadata in
local `artifacts/m4-m6/m6/plain-top/` and `annotated-top/`. Regenerate with:

```sh
python3 tools/render_examples.py build-release/vasp-render \
  --output artifacts/m4-m6/m6/annotated-top --overlays
```

## Verification surface

| Requirement | Evidence |
| --- | --- |
| Baselines and renderer/AA decisions | `performance.md`; raw `artifacts/m4-m6/reference/`, `aa/`, `m4/` |
| Identical-buffer PNG profiles and 1/2/4/8-thread scaling | `performance.md`; raw `artifacts/m4-m6/m5/` |
| Overlay visibility, colors, units, fitting and determinism | `tests/test_overlays.py`; both 20-image galleries |
| Aggregate memory/work limits and failed-output preservation | `tests/test_png_limits.c`, encoder file-size/rename failure tests, existing CLI/scene limits |
| Default Release and all-feature sanitizer suites | `artifacts/m4-m6/m6/validation/` |
| Installed and packaged execution, source integrity, clean offline builds | `tools/validate_deployment.py`; final `artifacts/m4-m6/m6/deployment/report.json` |
| Lab latency/RSS targets and pixel-preserving defaults | `performance-targets.json`, `tools/check_performance.py`; final `artifacts/m4-m6/m6/acceptance.json` |

The default build has OpenMP/libdeflate enabled and Cairo disabled. The
all-feature sanitizer build additionally exercises Cairo rejection and PNG
quantization gates. Leak checks run outside the tracing sandbox because
LeakSanitizer cannot inspect processes under ptrace.

Deployment validation installs to a private prefix containing spaces, renders
with installed presets from an unrelated working directory, checks shared
library resolution, creates/extracts a checksum-verified binary archive and
compares its output. The source archive's per-file manifest is verified; fresh
source builds run the full suite both with selected defaults and with OpenMP,
libdeflate and Cairo disabled. Their annotated output must match exactly.
An `OMP_NUM_THREADS=1` run checks the automatic worker limit.

```sh
python3 tools/validate_deployment.py /tmp/qasr-m6-release \
  --output artifacts/m4-m6/m6/deployment/report.json
```

## Performance acceptance and limits

The targets are scoped to this i9-10900K WSL workstation, 30 warm-cache process
launches, 512×512 shaded / 768×768 illustrated, selected defaults and close/rename
without fsync. Native-storage targets are median/p95 ≤75/90 ms shaded and
≤150/180 ms illustrated, with representative RSS ≤128 MiB. The six Windows-path
cases target median/p95 ≤350/400 ms. Eight fully annotated representative cases
target median/p95 ≤250/350 ms. These bounds were set from the fresh M3/M5
workstation evidence, not the historical laptop numbers. They are a lab gate,
not timing assertions in shared CI or a guarantee for arbitrary inputs/hosts.

```sh
python3 tools/check_performance.py \
  --baseline artifacts/m4-m6/reference/linux-baseline.json \
  --native artifacts/m4-m6/m6/defaults-native.json \
  --windows artifacts/m4-m6/m6/defaults-windows.json \
  --annotated artifacts/m4-m6/m6/annotated-native.json \
  --output artifacts/m4-m6/m6/acceptance.json
```

CPU governor/frequency and Windows host activity remain uncontrolled. Results
are warm-cache observations; no OS-cold-cache or physical-disk durability claim
is made. Windows-mounted large OUTCAR input remains I/O dominated. The automatic
PNG rule is measured for native Linux storage versus the WSL 9p Windows mount;
other storage may favor explicit compression overrides. Shared runtime libraries
must be present on the deployment host. Fixture-backed OUTCAR coverage remains
5.2.11/5.2.12 and 6.2.0; real MD and other versions are not newly claimed.

These M4–M6 changes form v0.2.0; see `release-v0.2.0.md` for release checks.
Generated images, raw samples, archives,
Git metadata and local settings/credentials remain excluded from source archives.
