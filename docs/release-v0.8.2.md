# QASR v0.8.2

Fixed-window render reports now identify clipped atoms, bonds, orbital
triangles and faces, and give the final alpha bounding box in output pixels.
Orbital contour generation uses parallel stripes. PLY layers are loaded and
hashed in one read. Config, job and report schemas remain at version 1.

## Downstream frame command

Use the final frame dimensions and set the camera window explicitly. Keep your
existing orbital layers in the recipe and resolve their PLY paths relative to
the recipe file. This excerpt uses the 640 px setup in the release brief.

```json
{
  "schema_version": 1,
  "image": {
    "width": 640,
    "height": 640,
    "style": "illustrated",
    "aa": "4x",
    "background": "#00000000"
  },
  "camera": {
    "extent_A": 3.224,
    "center_A": [0, 0, 0],
    "margin": 0,
    "clip": "allow"
  },
  "overlays": {"cell": false}
}
```

```sh
qasr geometry.xyz --no-user-config --config frame-000.json --threads 1 \
  --report frame-000.report.json -o frame-000.png
```

Set `camera.clip` to `"error"` when clipping should fail the frame. Leave it
at `"allow"` when the pipeline should write the frame and read `clipping.any`
alongside `ink_bbox_px`. For the adaptive alternative set `image.aa` to
`"adaptive"`. With orbital triangles, adaptive selects the existing uniform
2x sampling path.

On the 640 px adaptive substitute, 4, 8, 12, 16 and 20 concurrent
`--threads 1` workers delivered 37.5, 63.6, 72.4, 75.5 and 73.9 fps,
respectively. One worker with `--threads 20` delivered 13.4 fps. These are
aggregate measurements with PNG output on native WSL storage.

The Linux x86-64 Release build is at `artifacts/v0.8.2/qasr` in the working
tree. Its SHA-256 is
`f7c87dcea74adb7384aa4e94588a960253c85d6de1a8039b691f2980a60d21c9`.

[Benchmarks and worker throughput](performance-v0.8.2.md) record the measured
hardware, timing and compatibility checks.
[Configuration and reports](configuration.md#render-reports) define the report
fields. [The JSONL turntable example](batch-worker.md#orbital-turntable) shows
how to run 180 camera poses against shared geometry and meshes.
