# SPDX-License-Identifier: GPL-3.0-only
"""Check that timing comparisons cannot silently bypass correctness gates."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_benchmarks import compare, pixel_metrics
from cpu_counters import measure_cycles
from benchmark_iteration import comparison, measure_cli
import subprocess


def report():
    return {"schema": 2, "finished_utc": "2026-10-29", "label": "test",
            "cases": [{"name": "one", "input_sha256": "input", "instances": 1,
                       "bonds": 0, "camera": {}, "width": 1, "height": 1,
                       "settings": {"threads": 1, "image": {
                           "style": "shaded", "background": "#ffffff",
                           "depth_fade": 0, "outline_width": 1}},
                       "decoded_rgba_sha256": "pixels", "elapsed_ms": [10, 11, 12],
                       "latency_ms": {"median": 11}}]}


class BenchmarkTests(unittest.TestCase):
    def test_interleaved_interval_keeps_pairs_together(self):
        rows = [{'baseline': {'wall_ms': n, 'cpu_ms': n},
                 'candidate': {'wall_ms': n / 2, 'cpu_ms': n / 2}}
                for n in [1, 100, 10000]]
        result = comparison(rows)
        self.assertEqual(result['candidate_over_baseline'], .5)
        self.assertEqual(result['paired_bootstrap_ratio_95pct'], [.5, .5])

    def test_interleaved_rejects_changed_output_before_reporting(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            source = work / 'input'
            source.write_text('input')
            args = SimpleNamespace(output=work / 'report.json', warmups=1, samples=2, seed=1729)
            report_data = {'cases': []}

            def fake_run(command):
                output = b'reference' if str(command[0]) == '/baseline' else b'changed'
                return subprocess.CompletedProcess(command, 0, stdout=output), {}

            with mock.patch('benchmark_iteration.run', side_effect=fake_run):
                with self.assertRaisesRegex(ValueError, 'output differs'):
                    measure_cli({'baseline': Path('/baseline'), 'candidate': Path('/candidate')},
                                [('case', source, [], None)], args, work, report_data, lambda: None)
            self.assertEqual(report_data['cases'], [])

    def test_cycle_child_lifecycle(self):
        result=measure_cycles(["/bin/true"])
        self.assertIsInstance(result["available"],bool)
        if result["available"]:
            self.assertGreater(result["cycles"],0)
            self.assertGreaterEqual(result["time_enabled_ns"],result["time_running_ns"])
        else:
            self.assertTrue(result["reason"])
        with self.assertRaises(subprocess.CalledProcessError):
            measure_cycles(["/bin/false"])

    def test_lossless_thread_comparison(self):
        ref, candidate = report(), report()
        candidate["cases"][0]["settings"]["threads"] = 4
        candidate["cases"][0]["elapsed_ms"] = [4, 5, 6]
        candidate["cases"][0]["latency_ms"]["median"] = 5
        result = compare(ref, candidate)
        self.assertTrue(result["accepted_lossless_comparison"])
        self.assertTrue(result["cases"][0]["faster_with_interval_below_one"])

    def test_reject_changed_scene_or_style(self):
        for field, value in [("input_sha256", "other"), ("instances", 2),
                             ("bonds", 1), ("camera", {"scale": 5}), ("width", 2)]:
            with self.subTest(field=field):
                ref, candidate = report(), report()
                candidate["cases"][0][field] = value
                with self.assertRaises(ValueError):
                    compare(ref, candidate)
        ref, candidate = report(), report()
        candidate["cases"][0]["settings"]["image"]["style"] = "illustrated"
        with self.assertRaises(ValueError):
            compare(ref, candidate, True)

    def test_incomplete_and_duplicate_reports(self):
        ref, candidate = report(), report()
        del candidate["finished_utc"]
        with self.assertRaises(ValueError):
            compare(ref, candidate)
        candidate = report()
        candidate["cases"].append(copy.deepcopy(candidate["cases"][0]))
        with self.assertRaises(ValueError):
            compare(ref, candidate)
        candidate = report()
        candidate["cases"][0]["name"] = "unmeasured-reference-case"
        with self.assertRaises(ValueError):
            compare(ref, candidate)

    def test_alpha_metrics_and_lossy_gate(self):
        def png(path, pixel):
            def chunk(kind, payload):
                return (struct.pack(">I", len(payload)) + kind + payload
                        + struct.pack(">I", zlib.crc32(kind + payload)))
            path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(
                b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(b"\0" + bytes(pixel))) + chunk(b"IEND", b""))
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp)/"a.png", Path(tmp)/"b.png"
            png(a, [255, 0, 0, 0])
            png(b, [0, 255, 0, 0])
            self.assertEqual(pixel_metrics(a, b)["premultiplied_rgba_mae"], 0)
            png(b, [0, 0, 0, 255])
            self.assertEqual(pixel_metrics(a, b)["premultiplied_rgba_mae"], 63.75)
            ref, candidate = report(), report()
            ref["cases"][0]["png"] = str(a)
            candidate["cases"][0]["png"] = str(b)
            candidate["cases"][0]["decoded_rgba_sha256"] = "changed"
            result = compare(ref, candidate, True)
            self.assertFalse(result["accepted_lossless_comparison"])
            self.assertTrue(result["requires_visual_review"])


if __name__ == "__main__":
    unittest.main()
