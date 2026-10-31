# Sequential JSONL worker

Available in **v0.4.0**. `--build-info` reports `batch_worker: true`.
The original single-job CLI remains supported and shares its rendering path with
the worker. No additional runtime package, network listener or HTTP service is
required. MCP remains an optional integration adapter when a client needs it;
the rendering worker itself needs no integration adapter.

## Start and call

```sh
qasr --batch --cache-mib 64 < requests.jsonl > responses.jsonl
qasr --schema worker > worker.schema.json
```

`--batch` must be the first argument. Its only options are `--cache-mib 0..512`
(default 64, zero disables retention) and `--max-jobs 1..1000000` (optional;
otherwise process until EOF). Options cannot be repeated. Every nonblank input
line is one strict JSON object, at most 1 MiB excluding its terminating newline.
A final valid line without newline is accepted. Blank lines are ignored.
Oversized/NUL/malformed lines produce an error response and are drained through
the next newline so later requests can continue. JSON nesting is limited to 32
including the envelope. Duplicate/unknown keys remain errors.

```json
{"id":"view-1","op":"render","job":{"input":"CONTCAR","output":"view-1.png","threads":4,"config":{"image":{"width":768,"height":768},"camera":{"view":"top"}}}}
{"id":"view-2","op":"render","job":{"input":"CONTCAR","output":"view-2.png","threads":4,"config":{"image":{"width":768,"height":768},"camera":{"view":[1,1,1]}}}}
{"id":"check","op":"validate","job":{"input":"CONTCAR","config":{"geometry":{"expand_xy":[1,1]}}}}
{"id":"memory","op":"stats"}
{"id":"release","op":"clear_cache"}
```

IDs are required printable ASCII strings of 1..128 bytes. Operations are
`render`, `validate`, `stats` and `clear_cache`. Render/validate require an ordinary
render invocation under `job`; control operations reject a `job` field. All paths
are relative to the worker's working directory, equivalent to stdin JSON jobs.
No shell commands or input-data uploads are accepted. Create output parent
directories before submission. Input structures remain files; PNG stdout remains
unsupported. Worker jobs always skip implicit user preferences, even if their
`no_user_config` field is false; put reproducible settings in each request.

Exactly one response is flushed per nonblank line; stdout contains only JSONL.
Successful renders return the normal compact report under `result`, with cache
and actual render-thread information. Malformed/unusable IDs are null when they
cannot be echoed; clients should check `ok` rather than assuming an ID implies
success. IDs need to distinguish outstanding work; the worker does not retain
an ever-growing history of IDs.

```json
{"id":"view-1","ok":true,"result":{"ok":true,"version":"0.4.0","input":{"path":"CONTCAR","sha256":"…"},"output":{"path":"view-1.png","sha256":"…"},"cache":{"source_hit":false,"scene_hit":false,"retained_bytes":12345,"limit_bytes":67108864},"render_threads":4}}
{"id":"bad-selection","ok":false,"error":{"code":"invalid_atom_index","exit_code":2,"path":"atom_overrides[0].select.source_indices[0]","message":"…"}}
```

The success example is abbreviated; actual reports also include dimensions,
counts, settings, timings and cumulative cache counters. `job.report` optionally
writes the result to a separate file; omitted or `"-"` sends no extra stdout
message. PNG and report file commits are individually atomic, not a combined
transaction. A late sidecar-report I/O failure can leave a valid new PNG.
Diagnostics/recovery warnings use stderr. Config/rules/paths are rebuilt per job;
ordinary request errors preserve the session and later requests continue.

`validate` returns `input_validated` and `selectors_validated`. With no input,
these are false and only settings syntax/resolution is checked. Validation evicts
retained input/scene data before parsing, so it shares the normal memory budget.
It never writes PNGs or render reports.

## Cache policy and limits

The worker retains **at most one parsed structure and one matching scene**.
There is no unbounded filename map, trajectory history, image cache or queue.
The default limit is 64 MiB of retained atom/scene/style-metadata allocations.
An object too large for the limit still renders under existing per-job budgets,
without being retained. `stats` reports the limit, retained bytes, lifetime
source/scene hit counters and source misses. `clear_cache` drops objects, while
keeping lifetime counters. Per-job parser/scene/image/work limits still apply;
the cache limit does not represent the worker's total RSS or a global pool limit.

- Input reuse requires matching **content SHA-256**, explicit format, frame/final
  selection, recovery policy and ordered species mapping. Each request rehashes
  the input; timestamp/inode/size alone are insufficient. A miss is hashed again
  after parsing to detect ordinary concurrent modifications during that pass.
- Changing input identity/options evicts the prior entry. Failed parsing does not
  install a partial result. Input content reuse can work across different paths;
  reports still name the current request's path.
- Scene reuse additionally requires matching resolved geometry, element styles,
  bond cutoffs, atom rules and cell visibility. Camera, image dimensions, AA,
  lighting style, PNG settings and annotation layout are recomputed per request.
- Geometry/rule changes rebuild the scene. A different frame currently requires
  another full validated parse; only the selected structure is cached. Alternating
  frames can therefore miss even when the file is unchanged.

Inputs must remain stable while an individual request is running. Hashing twice
does not provide file locking or protect against arbitrary change-and-restore
races. Cached structures/scenes own their allocations and do not borrow the prior
request's configuration strings. All retention is released at normal shutdown.

## Cancellation and lifecycle

The stream is sequential: finish a request before expecting a later control
request to run. Clients can stop queuing by closing stdin; accepted lines are
processed until EOF or the configured job count.

On POSIX/WSL, **SIGINT during an active render/validation requests cooperative
cancellation**. The request returns error code `cancelled` (numeric category 8),
and the worker can accept another request. Polling occurs during text/XML input,
hashing, scene work and raster tiles; encoding/allocation/library calls may finish
before the next check. A cancelled PNG is discarded if cancellation is observed
before its atomic commit. Cancellation arriving after output commit cannot undo
that output. Client hard deadlines may terminate the process instead.

SIGTERM requests cancellation and shutdown after the current response. An idle
interrupt exits with status 130. Normal EOF/job-limit shutdown returns 0; request
failures alone do not make the session exit nonzero. Fatal framing I/O/output
failures return 7, allocation failures can return 6, and invalid launch arguments
return 2. A client must inspect every response, independently of final exit status.

## Benchmark and verify

Generate [the synthetic corpus](synthetic-workloads.md), then compare both
backends using the same executable, corpus, filesystem and thread policy:

```sh
python3 tools/run_m10_workloads.py build-release/qasr /tmp/m10-data \
  --output /tmp/m10-cli --samples 30 --warmups 3
python3 tools/run_m10_workloads.py build-release/qasr /tmp/m10-data \
  --backend worker --cache-mib 64 --output /tmp/m10-worker \
  --samples 30 --warmups 3 --reference /tmp/m10-cli/report.json
```

The worker runner checks IDs, errors, input/output hashes, frames, counts, decoded
pixels and restoration assertions against the CLI. Warmups exclude startup and
initial cache misses from repeated-request latency; raw stages and cache hits
remain in the report. Worker per-request CPU samples are null because child
resource accounting is only available when a child exits. With `--measure-rss`,
the worker's whole-session peak is measured once with GNU time, whereas CLI peaks
are separate per-request runs. Do not compare those as identical memory metrics.

Eight worker test groups cover framing/errors, scene-key dependencies, disabled
and small caches, clear/validate, synthetic CLI equivalence, bounded repeated
use, launch/schema/default isolation and active cancellation/recovery. The broad
release/sanitizer/fallback checks and measured results are documented with M11.
