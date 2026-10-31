# Defaults, atom styling and automation

Available in **v0.4.0**. The immutable v0.3.0 tag contains XML input,
periodic context and the original handbook; it does not contain user-preference or atom-override options.
Check `--build-info` for `user_config`, `atom_overrides`, `job_requests` and
`config_schema_version: 1` before using this interface.

## Defaults and reproducibility

Rendering settings resolve in this order:

1. Compiled defaults.
2. Optional user preferences.
3. Explicit `--config FILE` settings, or `--job FILE` embedded `config`.
4. Explicit CLI values.

Use JSON, with optional `"schema_version": 1`. Unknown/duplicate keys and malformed
present files fail. Objects merge by field; arrays replace. A higher layer's
`O-Si` bond cutoff replaces a lower layer's `Si-O`; both spellings within one file
are an error. Validate the final combined settings with the executable: JSON
Schema checks structure, while input identity, build capabilities, vector geometry,
range ordering, cross-field conflicts and resource budgets need runtime checks.
Each JSON input is limited to 1 MiB and 32 nesting levels; NUL strings are rejected.

The user file is `$XDG_CONFIG_HOME/qasr/config.json` for an absolute XDG
path, otherwise `$HOME/.config/qasr/config.json` for an absolute HOME.
Missing optional files are normal. Earlier development builds used the provisional
`vasp-render/config.json` path; move an existing preferences file into the new
`qasr/config.json` location if you want to retain it. No directory search or implicit file creation
occurs. `--init-config` explicitly creates a starter file and any missing parent
directories; existing files and symlinks are never overwritten. Use
`--no-user-config` in CI, benchmarks and reproducible jobs. User preferences may
set element palettes/radii, but cannot contain nonempty `atom_overrides` or
polygon faces. Keep structure-specific rules and faces in explicit recipes/jobs.

```sh
qasr --init-config
qasr --dump-config
qasr --no-user-config --config recipe.json --width 1200 --dump-config
```

The dump contains `config`, `runtime`, `derived`, `sources`, `user_config` and a
flat per-field `provenance` map (`layer`, `file`). Extract only `config` to reload
with `--config`; the complete diagnostic envelope is not a render configuration.
Requested repeat counts and an implicit camera up vector retain their meaning
when reloaded. Derived repeat counts/up are reported separately. Relative CLI and
`--config` paths use the current directory.

## Per-atom styles

```json
{
  "schema_version": 1,
  "geometry": {"expand_xy": [1, 1], "image_colors": "greyscale"},
  "atom_overrides": [
    {"select": {"source_indices": [0, 4]}, "color": "#ff6633", "radius_A": 0.8},
    {"select": {"element": "O", "source_range": [10, 20]}, "color": "#8844cc"},
    {"select": {"source_indices": [4], "cell": [1, 0, 0]}, "color": "#18a88b"}
  ]
}
```

This illustrates syntax; indices/elements must exist in the selected structure.
The downloadable handbook recipes use actual indices from their accompanying input.

Indices refer to the **zero-based full source atom list** printed by `--inspect`,
within the selected frame. `source_range` has inclusive bounds. Supply indices or
range, never both; element-only selectors are valid. An element and index/range
combine with AND. Exact IUPAC symbols are required. Duplicate, negative and
out-of-range indices, reversed ranges, empty matches and cells outside the chosen
expansion fail with a JSON path. Cell-only selectors are rejected.

Omitting `cell` styles the source and all copies. `cell: [0,0,0]` selects the
primary copy; negative xy origins work with centered expansion. Later matches
win **per property**. Resolution is element settings → source rules → periodic
context RGB → cell-specific rules. The cell phase runs after all source rules,
regardless of their positions in the array. Alpha is preserved by context colors.
`radius_A` is an angstrom display radius before `sphere_scale`; changing it does
not change bond connectivity. Legend swatches retain the element palette.

`--inspect-scene` adds `applied_rules: {"color": N|null, "radius": N|null}` to
spheres when rules are present. IDs are zero-based indices in `atom_overrides`.
Source identity and lattice origin remain the selector keys; rendered instance
numbers depend on expansion. Source rules resolve once; a linked list of cell
rules is evaluated per instance. Limits: 4096 rules, 64 MiB for owned configuration
plus source style table, 200 million source checks and 200 million cell-rule
checks, alongside existing scene/image budgets.

Saucepan slash strings are not accepted automatically. Convert explicitly:
`H/1/1/#ff0000` becomes `{"select":{"element":"H","source_indices":[0]},"color":"#ff0000"}`;
`H/3/6/#00ff00` uses `source_range: [2,5]`; `C/0/0/1.0` uses
`elements.C.radius_A: 1.0`. Saucepan's indices are one-based offsets in the full
list, whereas qasr uses zero-based offsets.

## Schemas, validation and jobs

```sh
qasr --schema config > config.schema.json
qasr --schema user > user.schema.json
qasr --schema job > job.schema.json
qasr --schema worker > worker.schema.json
qasr --schema faces > faces.schema.json
qasr --no-user-config --config recipe.json --validate-config
qasr CONTCAR --no-user-config --config recipe.json --validate-config --json-errors
qasr --job render-job.json
cat render-job.json | qasr --job -
```

The five Draft 2020-12 schemas cover config, user preferences, jobs, worker
requests and face files. They are embedded in the executable, installed under
`share/qasr/schemas`, and served by the handbook. `--schema` alone selects
config. Schema discovery runs alone. Validation does not rasterize or write
image/movie/report files. With input and polygon faces, it builds geometry to
check atom references and triangulation. Without input it reports
`input_validated: false` and `selectors_validated: false`; supply input for
selector and face checks.

A job is an invocation, distinct from persistent preferences:

```json
{
  "schema_version": 1,
  "input": "CONTCAR",
  "output": "render.png",
  "report": "render-report.json",
  "frame": "final",
  "threads": "auto",
  "no_user_config": true,
  "json_errors": true,
  "config": {"image": {"width": 768, "height": 768}}
}
```

Optional runtime fields also include `format`, `recover` and `movie`.
Movie mode, frame rate and codec are runtime job fields; image/geometry/face
settings remain in `config`. See [movie jobs and limits](movies.md). Paths in a job file
are relative to that file's directory; stdin jobs use the working directory.
Input structures remain file paths. Image/movie stdout is unsupported. `report: "-"`
uses stdout. CLI input/output/frame/settings override the corresponding job
values. `--job` and `--config` are mutually exclusive. Both accept `-` for one
bounded stdin JSON object. Dump/validate modes can read a full render job without
creating its outputs; explicit CLI `-o`/`--report` are rejected in these modes.
Choose one init/dump/validate/inspect mode per call.

With `--json-errors`, stderr is one JSON error envelope, for example:

```json
{"ok":false,"error":{"code":"invalid_atom_index","exit_code":2,"path":"atom_overrides[0].select.source_indices[0]","message":"…"}}
```

Numeric exit categories remain unchanged. Config and selector errors include
paths; generic parser/I/O errors may have a null path. Use explicit
`--json-errors` when malformed job JSON itself must produce structured errors:
an unreadable job cannot activate its own `json_errors` field. Recovery warnings
and optional benchmark diagnostics remain on stderr; avoid enabling them when a
consumer needs an error-only stream.

## Render reports

`--report FILE` atomically replaces a compact JSON success report; `--report -`
writes it to stdout. It records version, input/output paths and SHA-256 hashes,
selected frame/frame count, recovery state, atom/instance/bond/rule counts,
image dimensions/bytes, compact settings, configuration hash, sources and stage
times. The configuration hash covers the unformatted resolved config JSON
produced by cJSON, excluding runtime paths. Full rules, element tables and cutoff
maps are available in `--dump-config`, not repeated in each report.

Stage times exclude startup, JSON report encoding, output hashing and report
writing. Input hashing is included in `parse` when a report is requested. PNG
compression is resolved for its destination. Report paths must differ from
input/PNG (including existing aliases); common write failures are checked before
rendering. PNG and report are individually atomic, **not one transaction**: a
late report I/O failure can leave a successfully written PNG and an older report.
Consumers must check exit status and output hash. Atomic rename does not imply
fsync durability or concurrent-input protection; do not modify inputs during a job.

See [configuration validation evidence](m9-validation.md) for correctness and performance checks.

For repeated renders, use the [sequential JSONL worker](batch-worker.md). It
supports validate, render, stats and clear-cache requests with bounded
content-sensitive caching. Movie jobs use the same request interface but evict
the still-image cache. No network server or extra service is required.

## Polygon faces, bonds and cell lines

Use root-level `faces: [...]` or `faces_file: "polygons.json"` in a recipe.
Paths resolve against the containing recipe/job; stdin and worker paths use the
working directory. Later layers replace the face array; an empty array clears
it. `--faces FILE` is the final override. Resolved dumps contain inline faces.
See [face file fields, offsets and limits](polygon-faces.md).

`geometry.bond_radius_A` sets bond radius in angstroms; diameter is twice the
radius. `geometry.bond_colors: "atoms"` colors halves from endpoint atoms. Both
endpoint instances must be displayed, including any included neighboring cells;
this rule has no override. Cell dashes, color and white contours are `overlays`
fields; see [cell-line settings](cell-lines.md).

## Supported input formats

Builds with `nonperiodic: true` also accept job `format` values `xyz`, `extxyz`,
`gaussian-input`, `gaussian-output`, `ams` and aliases `gjf`/`com`. The CLI and
worker use the same input layer. Inspection and report `input` include
`cell_present`, `pbc` and `lattice_A`; a cell does not imply periodicity.
See [format subsets and geometry rules](input-formats.md).
