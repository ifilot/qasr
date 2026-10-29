# QASR v0.4.0 release — 2026-10-29

v0.4.0 closes the completed M9–M11 design and the application naming checkpoint.
The application is **QASR: Quick Atomic Structure Renderer**, and its native
executable, CMake project/target and source/binary packages are named `qasr`.

## Included since v0.3.0

- **M9:** built-in → user → explicit JSON → CLI defaults, explicit configuration
  initialization/opt-out, field-level provenance, strict version-1 schemas,
  per-source/per-cell atom styling, validation, JSON jobs/errors and render reports.
- **M10:** the sequential JSONL worker, bounded content-sensitive input/scene
  caching, cache controls, cooperative cancellation and CLI-reference checks.
  Seeded synthetic POSCAR/XML workloads cover independent/repeated inputs,
  frame changes, state restoration and same-path input replacement.
- **M11:** measured OpenMP improvements to sufficiently large buffer setup,
  primitive projection and AA resolution, preserving exact pixels. Dynamic tile
  scheduling and the conservative automatic thread policy remain selected;
  worker/thread throughput and CPU/memory tradeoffs are documented.
- **Naming and documentation:** `qasr` diagnostics, XDG preferences and installed
  data/doc paths; a standalone release handbook with 14 actual rendered examples,
  schemas, example jobs, source download and GitHub Actions Pages publication.

The earlier v0.3.0 tag and its provisional `vasp-render` source archive remain
unchanged. Existing preferences can be moved from the old `vasp-render` directory
into `$XDG_CONFIG_HOME/qasr/config.json`, or `$HOME/.config/qasr/config.json` when
XDG_CONFIG_HOME is not absolute. QASR never moves user files implicitly. Input/job
schema version remains 1; the application version bump does not change units,
zero-based selectors or precedence.

## Release checks

The release gate covers all 12 default Release CTest entries, 14 all-feature
address/undefined-behavior/leak entries and the serial/libpng/XML-disabled
fallback. Installation, library/preset/schema paths, binary packages, checksums,
a fresh offline source rebuild/tests and downloaded handbook commands are checked.
Reference PNG pixels and the handbook's images remain unchanged.

M10/M11 retained evidence includes the 1200-request bounded-cache soak, 53 standard
and nine selected stress requests, exact CLI/worker image comparisons, and
OpenMP scheduling/thread/worker sweeps. Performance observations and their limits
are in [M10/M11 validation](m10-m11-validation.md); the v0.4.0 closeout does not
relabel those historical measurements as new performance experiments.

```sh
cmake -S . -B /tmp/qasr-v0.4.0-release -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/qasr-v0.4.0-release --parallel 10
ctest --test-dir /tmp/qasr-v0.4.0-release --output-on-failure
python3 tools/validate_deployment.py /tmp/qasr-v0.4.0-release \
  --output artifacts/v0.4.0/deployment/report.json
```

Release logs, deployment reports, generated packages/checksums and audits are
retained under `artifacts/v0.4.0/`. The handbook's source snapshot omits its own
website (`package_source.py --without-website`), avoiding recursive archives;
full release source packaging includes the standalone handbook.

## Closeout and scope

M0–M11 are closed. Optional MCP or HTTP integration remains conditional on a
concrete client/workflow; no additional server is required for existing CLI or
stdio use. No mandatory design tasks are left open by this release.

WSL is the user-approved validation/performance environment. Shared system
libraries remain required. Existing input coverage, allocation/work budgets,
opaque-geometry depth behavior, individually atomic PNG/report writes and the
experimental status of Cairo remain as documented; this release introduces no
new platform or universal performance guarantees.
