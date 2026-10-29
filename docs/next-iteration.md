# Next iteration: configuration, atom selection and automation

Status: **M9a–M9c implemented on master after v0.3.0**. The v0.3.0 tag
remains the release boundary for M7/M8 and the original handbook. The design
rationale below is retained; see [the implemented interface](configuration.md)
for exact flags, limits, precedence and examples. M10 implements the [persistent worker](batch-worker.md) and
[synthetic workload comparisons](synthetic-workloads.md). M11 completes the measured
OpenMP review; see [implementation/validation](m10-m11-validation.md).
MCP/HTTP remain optional integration branches when a client requires them.

## Recommended decisions

1. Keep **JSON** as the canonical configuration format, using the existing
   cJSON parser. Add a versioned, strict JSON Schema and automatic user defaults.
   Keep the CLI for simple calls, paths and final scalar overrides.
2. Add typed **per-source-atom selectors**, with optional periodic-cell selectors,
   rather than encoding selectors and values inside slash-delimited strings.
3. Improve the existing CLI's machine contract before building a persistent
   renderer/server. A later JSONL batch mode or thin MCP adapter can reuse the
   same request/response schemas when measurements or integration needs justify it.

JSON is already used by qasr and Saucepan. It supports numeric types and nested
selectors directly, and can be validated by ordinary tooling. YAML's comments
and hand-editing conveniences do not currently justify another parser, another
syntax to test, and additional scalar/alias interpretation rules. This is an
engineering choice for this program, not a claim that LLMs require JSON or cannot
use YAML. Comments can instead live in the documented template or descriptive
fields explicitly permitted by its schema.

## Three conceptual levels of defaults

| Level | Source | Precedence |
| --- | --- | --- |
| 1 | Compiled defaults | Lowest; complete, versioned fallback |
| 2 | User preferences | Automatically loaded if present |
| 3 | Current invocation | Explicit job JSON, then explicit CLI overrides |

The call-level job file preserves today's `--config job.json` workflow. CLI
values remain the final override within level 3. Thus the conceptual three-tier
model has four concrete merge steps: built-ins → user file → job file → CLI.
Removing the existing job file would lose reproducible project recipes.

Use `$XDG_CONFIG_HOME/qasr/config.json` when XDG_CONFIG_HOME is a nonempty
absolute path; otherwise use `$HOME/.config/qasr/config.json` when HOME is
an absolute path. Follow the [XDG base-directory specification](https://specifications.freedesktop.org/basedir/latest/).
If neither is available, use built-ins without inventing a relative search path.
Do not implicitly search the current working directory or parent directories.

Proposed controls:

- `--init-config`: explicitly create an annotated-by-documentation starter
  JSON file at the resolved user path; refuse to overwrite an existing file.
  Ordinary rendering never creates/rewrites preference files.
- `--no-user-config`: disable automatic user preferences for reproducible
  scripts, CI, benchmarks and agent requests.
- `--dump-config`: return the resolved configuration as JSON, including loaded
  file paths and per-field provenance. A separate resolution map keeps this
  report inspectable without putting report-only keys into render-job JSON.
- Preserve `--config FILE`; `--config -` can read one bounded job from
  stdin, while the structure itself remains a path.

Merge objects field by field. Omitted fields inherit; unknown or duplicate keys
remain errors. A higher layer may replace a lower layer's `Co-O` cutoff, but
providing both `Co-O` and `O-Co` in the same layer remains an error. Today the
cutoff parser treats any preexisting cutoff as a duplicate; implementation must
separate within-file duplicate detection from cross-layer replacement.

Arrays replace the corresponding lower-level array; they do not silently append.
User preferences should carry general rendering/element defaults. Source-index
and cell-specific atom rules belong in explicit job files, not global user
preferences, so a highlight cannot accidentally apply to an unrelated structure.
Consider input identity/atom-count guards for reused job recipes.

Validate the resolved configuration once after all layers have been applied.
Settings such as centered expansion must retain their requested values until
resolution, rather than repeatedly rewriting repeat counts while loading each
file. Invalid present user files fail with their path and a useful diagnostic;
missing optional files are normal. A job with --no-user-config must remain
independent of the caller's machine preferences.

## What Saucepan's custom format actually does

Inspected local sources:

- `/mnt/d/PROGRAMMING/CPP/saucepan/assets/tooltips/custom_json_example.txt`
- `/mnt/d/PROGRAMMING/CPP/saucepan/assets/blender/render_image.py`, especially
  `build_atoms` and the per-endpoint settings used by `build_bonds`.

Saucepan uses JSON arrays containing strings such as:

```json
{
  "atom_colors": ["H/1/1/#ff0000", "H/3/6/#00ff00"],
  "atom_radii": ["C/0/0/1.0"]
}
```

The Blender reader splits each string on `/`, checks the element, and compares
`counter + 1` with the inclusive first/last bounds. These are **one-based indices
in the full atom list**, not ordinal positions among atoms of that element.
Bounds `0/0` select all atoms of the named element. Later matching rules overwrite
earlier values. The GUI's validation checks JSON syntax; field semantics are
handled later in the Blender script.

The selection idea is useful. Retain element filtering, inclusive source ranges
and per-property overrides. Prefer named JSON fields because units, selector
meaning and types can then be validated without parsing a second mini-language.
Keep qasr's existing zero-based source indices; silently reinterpreting Saucepan's
one-based strings would cause off-by-one highlights.

If migration is needed, add an explicit Saucepan importer/converter with tests:

- `H/1/1/#ff0000` → `{"select":{"element":"H","source_indices":[0]},"color":"#ff0000"}`
- `H/3/6/#00ff00` → `{"select":{"element":"H","source_range":[2,5]},"color":"#00ff00"}`
- `C/0/0/1.0` → existing `elements.C.radius_A = 1.0`.

Do not accept slash strings as an automatically detected alternate meaning of
the canonical atom-rule schema. An explicit converter can preserve Saucepan's
later-rule behavior without inheriting its sentinel/index ambiguities.

## Proposed typed atom rules

```json
{
  "schema_version": 1,
  "elements": {"O": {"color": "#e88069", "radius_A": 0.5}},
  "atom_overrides": [
    {
      "select": {"source_indices": [0, 4]},
      "color": "#ff6644", "radius_A": 0.8
    },
    {
      "select": {"element": "O", "source_range": [10, 20]},
      "color": "#8844cc"
    },
    {
      "select": {"source_indices": [4], "cell": [1, 0, 0]},
      "color": "#33aa99"
    }
  ]
}
```

Rules and semantics:

- `source_indices` are zero-based offsets into the selected input geometry,
  matching --inspect atom indices. They do not change when repetition changes.
- `source_range` is inclusive. Exactly one of indices/range may be present.
  Element-only selection is permitted; supplied element filters combine with
  an index/range using AND, as in Saucepan.
- Omitting `cell` affects that source atom and all of its periodic copies.
  `cell` is an integer lattice origin, matching --inspect-scene; [0,0,0]
  selects only the primary copy and negative origins support centered contexts.
- Validate selectors against the chosen frame and requested expansion. Reject
  out-of-range indices, invalid cells, empty selections and unknown fields with
  the offending JSON path. Do not quietly render an unchanged image after a
  mistyped selection.
- Each rule requires color and/or radius_A. Later matching rules override only
  properties they actually specify. Radius_A is the display radius before the
  global sphere_scale multiplier, consistent with existing per-element radii.
  Geometry/radius/work bounds still apply; changing a display radius does not
  change bond connectivity.
- Resolve element settings, then source-atom overrides, then periodic context
  RGB, then explicitly cell-scoped overrides. This lets a source highlight
  propagate into muted neighbors, while an explicit selected-copy highlight
  can stay colored. Copy-specific alpha/radius changes remain local to that copy.
- Scene inspection should report source index, cell origin, resolved radius/RGBA
  and the applied rule IDs. Rendered-instance labels retain their current meaning;
  they are not the primary selector because their numbering depends on expansion.
- Legend counts remain per element. Use the element palette for swatches rather
  than an arbitrary highlighted atom, or explicitly identify a special-atom key.

Implement by resolving a bounded style table once per source atom, then applying
the much smaller cell-scoped rule set during expansion. Do not scan every rule
for every pixel or primitive intersection. Track/fail selector expansion work
within the existing allocation/work-budget discipline.

## LLM and scripted use

The current program is already usable by an agent with shell access:

- Noninteractive calls and stable numeric exit categories.
- --help and --build-info for discovery/capability checking.
- Strict JSON configuration and --inspect/--inspect-scene JSON output.
- stdout and stderr separation, deterministic rendering and atomic outputs.
- A standalone handbook with tested commands and real sample inputs.

There is no universal LLM-specific interface requirement. The useful target is
a small, discoverable and validated machine contract that is equally good for
scripts. Current gaps are schema discovery, structured errors, compact success
reports, input-bound selection validation and control over implicit user defaults.

Prioritize:

1. Versioned JSON Schema with explicit required fields, enums, limits and
   additionalProperties=false. Serve the same schema in the handbook and through
   a proposed --schema command. [JSON Schema object semantics](https://json-schema.org/understanding-json-schema/reference/object)
   support this validation directly; C runtime validation must agree with it.
2. A proposed --validate-config mode that checks a job and, when an input is
   supplied, validates selectors without rendering.
3. A proposed --json-errors mode with stable codes and JSON paths, retaining
   current human-readable stderr by default. For example: error code
   invalid_atom_index, path atom_overrides[0].select.source_indices[0], limit 249.
4. A proposed --report FILE compact success manifest: output path, dimensions,
   selected frame, counts, hashes, resolved settings and stage timings. Avoid
   requiring the model to read a potentially huge primitive list just to locate
   its output or verify what was rendered.
5. Job request JSON for runtime fields such as input path, output path and frame,
   with the existing rendering settings nested under config. Runtime job fields
   should not accidentally become persistent user defaults. Keep the existing
   CLI as a wrapper over the same validated request.

These flags/modes are available in the post-v0.3.0 development build. Check
--build-info capabilities before using them with an older executable.

## Batch/server/MCP tradeoff

Do not require a network server for ordinary rendering. A fresh process has a
simple lifetime, reliable error/cleanup isolation and works directly in shell,
CI and HPC batch environments. A model can already call it this way.

There is a credible persistent-process use case: render many views/styles of
the same large input without repeatedly parsing it. Retained M7 measurements
show about 80 ms to parse/inspect a 12.7 MB XML relaxation versus about 5 ms for
a small MD fixture, so caching may matter more than avoiding process startup.
These are workload observations, not measurements of a future server's speedup.

If that use case is prioritized, first benchmark a bounded JSONL batch/stdio
mode: one job per line, request IDs, one result/error per line, diagnostics on
stderr, explicit cancellation/limits, complete state reset between jobs and
bounded input/scene caches with invalidation by input identity and parsing
options. A failure in one job must not leak settings into the next. Measure
memory and throughput against independent CLI calls before selecting caching.

For agent-platform integration, a thin MCP adapter can expose inspect, validate
and render tools backed by the same core/CLI. MCP defines discoverable tools
with JSON Schema input and optional output schemas; it does not require moving
the rasterizer into an HTTP service. See the [MCP tools specification](https://modelcontextprotocol.io/specification/2025-11-25/server/tools).
Treat an MCP adapter as an integration layer, not a second renderer or an
automatic performance optimization.

Only add HTTP/server transport when another application actually needs remote
or concurrent calls. Then design authentication, file-access boundaries, queues,
timeouts/cancellation, cache limits and resource isolation for that deployment.
Those are additional product requirements, not prerequisites for LLM use.

## Suggested implementation and acceptance order

- **M9a — User defaults:** XDG discovery, explicit initialization/opt-out,
  field-level merge/provenance, unchanged no-preferences baseline pixels, and
  tests with isolated temporary HOME/XDG environments.
- **M9b — Atom rules:** schema plus selector validation, radius/color rules,
  source/copy identity, context interaction and Saucepan migration examples.
  Test repeated/skewed cells, frame changes, overlapping rules, alpha, limits,
  unchanged bonding and determinism. Include visually checked highlights.
- **M9c — Machine contract:** schema/validation discovery, structured errors,
  compact render reports and invocation JSON; update the handbook/examples.
- **M10 — Optional batch/MCP:** only if a repeated-input or integration workflow
  is prioritized, with measured caching/throughput and bounded-state tests.

All stages preserve the existing CLI for simple jobs. Do not make a new DSL,
YAML dependency or server mandatory just to change one atom's appearance.

## M11 — Review OpenMP opportunities and thread policy

Requested and completed on 2026-10-29 after M10. The investigation and retained
changes are documented in [the validation report](m10-m11-validation.md). The
accepted scope and criteria were:

- Profile representative synthetic and retained real workloads, separating
  parsing, selector/style resolution, scene/bond construction, rendering and PNG
  encoding. Identify substantial parallel work before changing implementation.
- Compare serial execution, existing tile parallelism and candidate OpenMP
  regions across small/large structures, image sizes and repeated-input jobs.
- Sweep thread counts and scheduling/granularity choices. Measure latency,
  throughput, CPU time, peak memory and run-to-run variability; assess affinity
  and the interaction between concurrent batch workers and render threads.
- Preserve deterministic output, resource limits, failure handling and the
  OpenMP-disabled build. Check races and reductions/order dependence where
  applicable. Account for setup/synchronization overhead and oversubscription.
- Retain changes only when representative measurements justify them; document
  unhelpful candidates and update automatic thread policy if evidence warrants it.

The synthetic M10 workloads provide reproducible inputs for this investigation;
real fixtures remain necessary to check that conclusions generalize.
