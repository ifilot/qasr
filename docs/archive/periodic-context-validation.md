# Periodic-context validation history

> Historical implementation and workstation measurements. See the
> [current periodic-context guide](../periodic-context.md) for usage.

Seven regression groups cover independent centered-versus-translated bond
geometry, counts/unique origins, boundary wrapping, primary outline corners,
alpha/palette preservation, strict input/conflict/instance limits, restored
legacy pixels, thread determinism, legend swatches and all 20 real slab inputs.
The complete Release and leak-enabled sanitizer suites include these checks.

Generate the comparison galleries with:

```sh
python3 tools/render_examples.py build/qasr --expand-xy 1,1 \
  --image-colors muted --output artifacts/m8/muted
python3 tools/render_examples.py build/qasr --expand-xy 1,1 \
  --image-colors greyscale --output artifacts/m8/greyscale
```

Each gallery retains PNG/input/preset hashes and fitted-camera metadata. The
original M0–M7 milestone list is complete; M8 is this user-requested addition.
Broader OUTCAR version/real-MD coverage remains a documented validation gap.

The accepted i9-10900K WSL workstation measured a 3×3×1 greyscale patch using
30 warm-cache samples after three warmups, automatic threads and native storage.
Median process times were 36.24/62.94 ms for shaded 512px / illustrated 768px
Co slab views, and 32.08/53.18 ms for Pd/ceria. Raw samples, stages, RSS,
input/binary hashes and PNGs are retained in `artifacts/m8/performance.json`.
These are observed timings, including PNG close/rename but excluding fsync,
under uncontrolled host load/governor; they are not new universal latency gates.
