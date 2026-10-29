# SPDX-License-Identifier: GPL-3.0-only
"""Compare schema-2 reports, checking scene equivalence and decoded pixels."""
import argparse
import json
import math
from pathlib import Path
import random
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from png_support import decode_png


def median_ratio_interval(reference, candidate):
    """Independent resampling; samples from separate runs are not paired."""
    rng = random.Random(20261029)
    ratios = []
    for _ in range(2000):
        a = statistics.median(rng.choices(reference, k=len(reference)))
        b = statistics.median(rng.choices(candidate, k=len(candidate)))
        ratios.append(b / a)
    ratios.sort()
    return [ratios[49], ratios[1949]]


def pixel_metrics(reference, candidate):
    w, h, a = decode_png(Path(reference))
    cw, ch, b = decode_png(Path(candidate))
    if (w, h) != (cw, ch):
        raise ValueError("image dimensions differ")
    # Premultiplied RGBA makes transparent RGB irrelevant. Alpha contributes
    # equally to the error; do not compare straight RGB at transparent edges.
    total = squared = worst = changed = 0
    for i in range(0, len(a), 4):
        differences = [abs(a[i+d]*a[i+3] - b[i+d]*b[i+3])/255 for d in range(3)]
        differences.append(abs(a[i+3] - b[i+3]))
        total += sum(differences)
        squared += sum(v*v for v in differences)
        worst = max(worst, *differences)
        changed += any(differences)
    return {"premultiplied_rgba_mae": total/len(a),
            "premultiplied_rgba_rmse": math.sqrt(squared/len(a)),
            "max_channel_error": worst, "changed_pixel_fraction": changed/(w*h)}


def compare(reference, candidate, allow_pixel_differences=False):
    if reference["schema"] != 2 or candidate["schema"] != 2:
        raise ValueError("schema-2 reports required")
    if "finished_utc" not in reference or "finished_utc" not in candidate:
        raise ValueError("completed benchmark reports required")
    refs = {case["name"]: case for case in reference["cases"]}
    if len(refs) != len(reference["cases"]):
        raise ValueError("duplicate reference case names")
    if len({case["name"] for case in candidate["cases"]}) != len(candidate["cases"]):
        raise ValueError("duplicate candidate case names")
    results = []
    for case in candidate["cases"]:
        if case["name"] not in refs:
            raise ValueError(f"{case['name']}: case absent from reference report")
        ref = refs[case["name"]]
        for key in ("input_sha256", "instances", "bonds", "camera", "width", "height"):
            if case[key] != ref[key]:
                raise ValueError(f"{case['name']}: incomparable {key}")
        # Image algorithm settings may differ intentionally. Everything else
        # (including physical radii, connectivity and overlays) must agree.
        for key in set(case["settings"]) | set(ref["settings"]):
            if key not in ("image", "threads") and case["settings"].get(key) != ref["settings"].get(key):
                raise ValueError(f"{case['name']}: incomparable settings.{key}")
        for key in ("style", "background", "depth_fade", "outline_width"):
            if case["settings"]["image"][key] != ref["settings"]["image"][key]:
                raise ValueError(f"{case['name']}: incomparable image.{key}")
        exact = case["decoded_rgba_sha256"] == ref["decoded_rgba_sha256"]
        interval = median_ratio_interval(ref["elapsed_ms"], case["elapsed_ms"])
        results.append({
            "name": case["name"], "identical_rgba": exact,
            "reference_median_ms": ref["latency_ms"]["median"],
            "candidate_median_ms": case["latency_ms"]["median"],
            "candidate_over_reference": case["latency_ms"]["median"]/ref["latency_ms"]["median"],
            "median_ratio_bootstrap_95pct": interval,
            "faster_with_interval_below_one": interval[1] < 1,
            "lossless_gate_passed": exact,
            "pixel_metrics": None if exact else pixel_metrics(ref["png"], case["png"]),
        })
    if not results:
        raise ValueError("no candidate cases")
    return {"reference": reference["label"], "candidate": candidate["label"],
            "pixel_differences_allowed_for_experiment": allow_pixel_differences,
            "accepted_lossless_comparison": all(row["identical_rgba"] for row in results),
            "requires_visual_review": any(not row["identical_rgba"] for row in results),
            "cases": results}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--allow-pixel-differences", action="store_true",
                        help="record lossy experimental metrics; this does not pass visual correctness")
    args = parser.parse_args()
    try:
        report = compare(json.loads(args.reference.read_text()),
                         json.loads(args.candidate.read_text()), args.allow_pixel_differences)
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    if not report["accepted_lossless_comparison"] and not args.allow_pixel_differences:
        raise SystemExit("decoded pixels differ; lossless comparison rejected (see output)")


if __name__ == "__main__":
    main()
