# M9 implementation and acceptance — 2026-10-29

> Historical record. Commands, limits and validation results describe the original
> development/release context. Use the [current documentation index](../README.md)
> for supported behavior and setup.

M9a–M9c are implemented on master after the immutable v0.3.0 release checkpoint.
The implementation guide is [configuration.md](../configuration.md); the standalone
website includes a development guide, two actual styling images, downloadable
recipes/jobs, matching schemas and a separate development source archive.

## Checks

- Default Release: all 10 CTest entries passed, covering existing parser,
  rendering, overlays, encoders, XML and periodic context plus configuration and
  SHA-256 provenance. All-feature Debug: all 12 entries passed with AddressSanitizer,
  UndefinedBehaviorSanitizer and leak detection, including Cairo experiments.
- The configuration suite has 22 tests. After the full suites, two added
  work-budget/malformed-job cases also passed on both builds. Tests isolate HOME
  and XDG state and cover layered inheritance/array replacement, initialization
  preservation, opt-out, source/copy precedence, selectors, errors, job paths,
  no-render validation, report hashes and conflicting report destinations.
- SHA-256 known vectors include empty input, padding/block boundaries and one
  million bytes. Report input/output hashes agree with Python hashlib.
- Draft 2020-12 meta-validation passed for all three schemas. Resolved defaults,
  existing presets and eight accepted/rejected structural examples agreed with
  the runtime. Cross-field/input/build/work constraints are additionally checked
  by the executable, not expressible entirely through structural schemas.
- All 20 retained M6 default reference images remained exactly equal after
  decoding RGBA. No implicit user preferences were used for comparisons.
- Deployment acceptance passed installation, installed XML/annotations/library
  checks, binary packaging, a fresh offline source build/test/render and the
  serial/libpng/XML-disabled fallback. Installed and packaged images matched.
- Original handbook browser checks passed desktop/mobile navigation, tabs,
  search, command builder, lightbox, downloads, print and direct file URLs.
  The M9 guide passed at widths 1440, 390 and 320, including offline image
  decoding and actual download responses. Orange source highlights and a green
  selected periodic copy were visually inspected. Stable release assets remain
  separate from development assets.

Local evidence is retained under `artifacts/m9/` and
`artifacts/website-validation/`; generated test/build state is not committed.
The versioned source, tests, schemas, guide and example artifacts are committed.

## Performance evidence

WSL, Intel i9-10900K, 1000-source-atom cobalt fixture, shaded 512×512, 2× AA,
one render worker, native temporary output, warm OS cache. Each row has three
warmups and 30 process-launch-through-exit samples; no cold-cache claim.

| Configuration | Median wall, ms | p95 wall, ms | Median config, ms | Median scene, ms |
| --- | ---: | ---: | ---: | ---: |
| v0.3.0 without rules | 174.87 | 184.70 | 0.039 | 23.94 |
| M9 without rules | 175.67 | 190.21 | 0.052 | 24.22 |
| M9, 100 source rules | 175.88 | 186.63 | 0.171 | 24.04 |
| M9, 4096 source rules | 197.13 | 246.41 | 5.731 | 25.05 |
| M9, 64 cell rules | 178.28 | 216.43 | 0.182 | 25.66 |

Source rules each select one atom; the 4096-rule case intentionally exercises the
rule-count ceiling. Cell rules select all cobalt atoms at origin [0,0,0]. The
ordinary no-rule median differs by about 0.5% in this run. High-count input has
visible JSON/validation cost; tails also vary with shared host scheduling. These
sequential workload observations do not establish universal overhead bounds or
prove a server/cache benefit. Raw wall/stage samples, commands, configurations
and executable/input hashes are in `artifacts/m9/style-performance.json`.

Source styles resolve once; cell rules use a dedicated linked list during
expansion, so unrelated source rules are not rescanned for every periodic copy.
Configuration/style/stamp allocations are charged to existing resource budgets.
Selector work-limit tests fail before allocating a large expanded scene.

## Scope

No mandatory packages beyond existing build dependencies were added. JSON Schema
is embedded; ordinary rendering does not invoke Python or a schema engine.
Python jsonschema and Chromium were used as development verification tools.
No new YAML parser, Saucepan string DSL, daemon or transport was introduced.
Optional M10 batch/stdio caching or MCP integration remains conditional on an
actual workflow and measured throughput/memory benefit.
