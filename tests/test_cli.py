# SPDX-License-Identifier: GPL-3.0-only
"""Black-box contract tests; all temporary data stays outside the source tree."""
import gzip
import hashlib
import json
import math
import pathlib
import random
import subprocess
import sys
import tempfile
import unittest

EXE = pathlib.Path(sys.argv.pop(1)).resolve()
FIXTURES = pathlib.Path(sys.argv.pop(1)).resolve()


class InputTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="qasr-test-")
        self.root = pathlib.Path(self.tmp.name)
        self.poscar = (FIXTURES / "POSCAR.skew").read_text()
        self.outcar = (FIXTURES / "OUTCAR.relax").read_text()

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, code=0):
        p = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True, timeout=15)
        self.assertEqual(p.returncode, code, p.stderr)
        if code:
            self.assertTrue(p.stderr)
            self.assertEqual(p.stdout, "")
        return p

    def data(self, text, *args, code=0, fmt=None):
        path = self.root / "input.data"
        path.write_text(text)
        extra = ["--format", fmt] if fmt else []
        p = self.run_cli(path, "--inspect", *extra, *args, code=code)
        return json.loads(p.stdout) if not code else p

    def config(self, content):
        path = self.root / "settings.json"
        path.write_text(content if isinstance(content, str) else json.dumps(content))
        return path

    def test_help_and_version(self):
        self.assertIn("qasr 0.5.0", self.run_cli("--help").stdout)
        self.assertIn("QASR: Quick Atomic Structure Renderer", self.run_cli("--help").stdout)
        self.assertEqual("qasr 0.5.0\n", self.run_cli("--version").stdout)

    def test_skewed_direct_and_species_order(self):
        result = self.data(self.poscar)
        self.assertEqual(result["atoms"][0]["position_A"], [1.3125, 0.625, 0.25])
        self.assertEqual(result["lattice_A"], [[2, 0, 0], [1, 2, 0], [0.5, 1, 2]])
        self.assertEqual([a["element"] for a in result["atoms"]], ["Si", "O"])

    def test_cartesian_and_three_component_scaling(self):
        text = self.poscar.replace("\n2\n", "\n2 3 4\n").replace("\nDirect\n", "\nCartesian\n")
        result = self.data(text)
        self.assertEqual(result["lattice_A"], [[2, 0, 0], [1, 3, 0], [0.5, 1.5, 4]])
        self.assertEqual(result["atoms"][0]["position_A"], [1, 0.75, 0.5])
        text = self.poscar.replace("\nDirect\n", "\nKpoints\n")
        self.assertEqual(self.data(text)["atoms"][0]["position_A"], [1, 0.5, 0.25])

    def test_negative_volume_scaling(self):
        for mode, expected in [("Direct", [1.3125, 0.625, 0.25]), ("Cartesian", [1, 0.5, 0.25])]:
            text = self.poscar.replace("\n2\n", "\n-8\n").replace("\nDirect\n", f"\n{mode}\n")
            self.assertEqual(self.data(text)["atoms"][0]["position_A"], expected)
        text = self.poscar.replace("\n2\n", "\n-27\n").replace("\n1 0 0\n", "\n-1 0 0\n")
        for actual, expected in zip(self.data(text)["atoms"][0]["position_A"], [-1.03125, 0.9375, 0.375]):
            self.assertAlmostEqual(actual, expected, places=12)

    def test_old_format_explicit_species(self):
        text = self.poscar.replace("Si O\n", "")
        self.data(text, code=5)
        self.assertEqual(self.data(text, "--species", "Si,O")["atom_count"], 2)
        cfg = self.config({"species": ["Si", "O"]})
        self.assertEqual(self.data(text, "--config", cfg)["atom_count"], 2)
        self.data(text, "--species", "Si", code=5)
        self.data(self.poscar, "--species", "C,O", code=5)

    def test_optional_tail_and_fortran_numbers(self):
        text = self.poscar.replace("0.5 0.25 0.125", "5D-1 2.5d-1 1.25e-1")
        result = self.data(text + "\nCartesian\n0 0 0\n0 0 0\n")
        self.assertEqual(result["atoms"][0]["position_A"], [1.3125, 0.625, 0.25])

    def test_poscar_errors(self):
        cases = [
            (self.poscar.replace("\n2\n", "\n0\n"), 4),
            (self.poscar.replace("\n2\n", "\n1 -2 1\n"), 4),
            (self.poscar.replace("Si O", "SiX O"), 5),
            (self.poscar.replace("1 1\nSelective", "1 1 1\nSelective"), 4),
            (self.poscar.replace("1 1\nSelective", "1000001 1\nSelective"), 4),
            (self.poscar.replace("1 1\nSelective", "0 0\nSelective"), 4),
            (self.poscar.replace("1 1\nSelective", "-1 2\nSelective"), 4),
            (self.poscar.replace("0.25 0.5 1\n", "1 0 0\n"), 5),
            (self.poscar.replace("0.5 0.25 0.125", "nan 0 0"), 4),
            (self.poscar.replace("0.5 0.25 0.125", "1e309 0 0"), 4),
            (self.poscar.replace("T F T", "T Z T"), 4),
            (self.poscar.replace("Direct", "Bogus"), 4),
            ("\n".join(self.poscar.splitlines()[:-1]), 4),
        ]
        for text, code in cases:
            with self.subTest(text=text):
                self.data(text, fmt="poscar", code=code)

    def test_geometry_property_cases(self):
        rng = random.Random(43)
        for _ in range(20):
            basis = [[rng.uniform(1, 4), 0, 0], [rng.uniform(-1, 1), rng.uniform(1, 4), 0],
                     [rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(1, 4)]]
            f = [rng.uniform(-2, 3) for _ in range(3)]
            scales = [rng.uniform(0.2, 3) for _ in range(3)]
            text = "random\n" + " ".join(map(str, scales)) + "\n"
            text += "\n".join(" ".join(map(str, row)) for row in basis)
            text += "\nC\n1\nDirect\n" + " ".join(map(str, f)) + "\n"
            actual = self.data(text)["atoms"][0]["position_A"]
            expected = [sum(f[i] * basis[i][j] for i in range(3)) * scales[j] for j in range(3)]
            for a, b in zip(actual, expected):
                self.assertTrue(math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-12))

    def test_outcar_final_and_indexed_frames(self):
        final = self.data(self.outcar)
        self.assertEqual((final["frame"], final["frame_count"]), (1, 2))
        self.assertEqual(final["atoms"][0]["position_A"], [0.25, 0, 0])
        self.assertEqual(final["lattice_A"][0], [3, 0, 0])
        first = self.data(self.outcar, "--frame", "0")
        self.assertEqual(first["lattice_A"][0], [2, 0, 0])
        self.assertEqual(first["atoms"][0]["position_A"], [0, 0, 0])
        self.data(self.outcar, "--frame", "2", code=4)
        self.data(self.poscar, "--frame", "1", code=4)

    def test_outcar_truncation_recovery(self):
        for marker in [" General timing", " 1.5 1.5 2", " 0.25 0 0"]:
            text = self.outcar[:self.outcar.index(marker)]
            with self.subTest(marker=marker):
                self.data(text, code=4)
                result = self.data(text, "--recover")
                self.assertTrue(result["recovered"])
                self.assertEqual(result["frame"], 1 if marker == " General timing" else 0)
        self.data(self.outcar[:self.outcar.index(" POSITION")], "--recover", code=4)
        for marker in [" General timing", " Voluntary context"]:
            text = self.outcar[:self.outcar.index(marker)]
            self.data(text, code=4)
            self.assertEqual(self.data(text, "--recover")["frame"], 1)
        text = self.outcar[:self.outcar.index(" 1.5 1.5 2")] + " 1.5 1.5"
        self.assertEqual(self.data(text, "--recover")["frame"], 0)
        self.data(text.replace("1.5 1.5", "NaN 1.5"), "--recover", code=4)

    def test_outcar_later_malformed_frame_is_not_hidden(self):
        text = self.outcar.replace("1.5 1.5 2", "NaN 1.5 2")
        self.data(text, "--frame", "0", code=4)
        self.data(text, "--recover", code=4)
        self.data(self.outcar.replace("NIONS = 2", "NIONS = 3"), code=4)
        self.data(self.outcar + " vasp.6.2.0\n", code=4)

    def test_outcar_metadata(self):
        self.data(self.outcar.replace("TITEL = PAW_PBE O", "TITEL = PAW_PBE C"), code=5)
        self.data(self.outcar, "--species", "C,O", code=5)
        text = "\n".join(line for line in self.outcar.splitlines() if not any(x in line for x in ["TITEL", "VRHFIN", "POTCAR:"]))
        self.data(text, code=5)
        self.assertEqual(self.data(text, "--species", "Si,O")["atom_count"], 2)
        self.assertEqual(self.data(self.outcar.replace("PAW_PBE Si ", "PAW_PBE Si_sv "))["atom_count"], 2)

    def test_outcar_table_validation(self):
        rule = " -----------------------------------------------------------------------------------"
        self.data(self.outcar.replace(rule, "not a separator", 1), "--recover", code=4)
        text = self.outcar.replace(" 0 0 0  0.1 0 0\n", "", 1)
        self.data(text, "--recover", code=4)
        text = self.outcar.replace(" 1 1 1 -0.1 0 0\n", " 1 1 1 -0.1 0 0\n 0 0 0 0 0 0\n", 1)
        self.data(text, "--recover", code=4)
        text = self.outcar.replace(" 1 1 1 -0.1 0 0", " 1 1 1 -0.1 0 NaN", 1)
        self.data(text, "--recover", code=4)

    def test_real_vasp_versions(self):
        for name, count, frames, first in [("OUTCAR.vasp5.gz", 7, 5, "Li"), ("OUTCAR.vasp6.gz", 1, 4, "Fe")]:
            with self.subTest(name=name):
                text = gzip.decompress((FIXTURES / name).read_bytes()).decode()
                result = self.data(text)
                self.assertEqual(result["atom_count"], count)
                self.assertEqual(result["frame_count"], frames)
                self.assertEqual(result["atoms"][0]["element"], first)
                # Independently extract the last recognized table for coordinate/lattice comparison.
                rows = text.rsplit("TOTAL-FORCE (eV/Angst)", 1)[1].splitlines()[2:2 + count]
                positions = [[float(v) for v in row.split()[:3]] for row in rows]
                self.assertEqual([a["position_A"] for a in result["atoms"]], positions)

    def test_real_static_run(self):
        result = self.data((FIXTURES / "OUTCAR.static").read_text())
        self.assertEqual((result["atom_count"], result["frame_count"]), (2, 1))
        self.assertEqual([a["element"] for a in result["atoms"]], ["Si", "Si"])
        self.assertEqual(result["atoms"][0]["position_A"], [2.05077] * 3)

    def test_real_slab_collection(self):
        root = FIXTURES / "slabs"
        manifest = json.loads((root / "manifest.json").read_text())
        self.assertEqual(len(manifest["structures"]), 20)
        for item in manifest["structures"]:
            with self.subTest(structure=item["id"]):
                path = root / item["file"]
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), item["sha256"])
                result = json.loads(self.run_cli(path, "--inspect").stdout)
                self.assertEqual((result["frame"], result["frame_count"]), (0, 1))
                self.assertEqual(result["atom_count"], item["atom_count"])
                elements = [symbol for symbol, count in zip(item["species"], item["counts"])
                            for _ in range(count)]
                self.assertEqual([a["element"] for a in result["atoms"]], elements)
                for actual, expected in zip(result["lattice_A"], item["lattice_vectors_angstrom"]):
                    for a, b in zip(actual, expected):
                        self.assertAlmostEqual(a, b, places=11)
                # Source snapshots use Direct coordinates. Compare every atom
                # against the independently recorded physical basis, preserving
                # fractional values outside [0,1) rather than wrapping them.
                lines = path.read_text().splitlines()
                direct = next(i for i, line in enumerate(lines) if line.strip().lower() == "direct")
                coords = lines[direct + 1:direct + 1 + item["atom_count"]]
                for index, (atom, line) in enumerate(zip(result["atoms"], coords)):
                    self.assertEqual(atom["index"], index)
                    fractional = list(map(float, line.split()[:3]))
                    expected = [sum(fractional[i] * item["lattice_vectors_angstrom"][i][j]
                                    for i in range(3)) for j in range(3)]
                    for a, b in zip(atom["position_A"], expected):
                        self.assertTrue(math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-11))

    def test_synthetic_md_stream(self):
        header = self.outcar[:self.outcar.index(" Iteration 1(1)")]
        frames = []
        for i in range(1000):
            frames.append(f"Iteration {i}(1)\nIteration {i}(2)\n"
                          " POSITION TOTAL-FORCE (eV/Angst)\n----------\n"
                          f"{i / 1000} 0 0 0 0 0\n1 1 1 0 0 0\n----------\n")
        text = header + " IBRION = 0\n" + "".join(frames)
        text += self.outcar[self.outcar.index(" General timing"):]
        final = self.data(text)
        self.assertEqual((final["frame"], final["frame_count"]), (999, 1000))
        self.assertEqual(final["atoms"][0]["position_A"], [0.999, 0, 0])
        self.assertEqual(self.data(text, "--frame", "567")["atoms"][0]["position_A"], [0.567, 0, 0])

    def test_config_precedence_and_overrides(self):
        cfg = self.config({"image": {"width": 640, "background": "#abcdef80"},
                           "geometry": {"repeat": [2, 1, 1]},
                           "elements": {"Si": {"radius_A": 1.1, "color": "#ffee00"}},
                           "bond_cutoffs_A": {"O-Si": 2},
                           "overlays": {"cell": False}})
        for options in [("--width", "128", "--config", cfg), ("--config", cfg, "--width", "128")]:
            settings = self.data(self.poscar, *options)["settings"]
            capabilities=json.loads(subprocess.check_output([str(EXE),"--build-info"],text=True))
            self.assertEqual(settings["image"], {"width": 128, "height": 512, "background": "#abcdef80", "style":"shaded", "tone":"color", "aa":"2x", "backend":"zbuffer", "depth_fade":0, "outline_width":1,
                                                "png_encoder":"libdeflate" if capabilities["libdeflate"] else "libpng",
                                                "png_filter":"none" if capabilities["libdeflate"] else "adaptive",
                                                "png_compression":"auto" if capabilities["libdeflate"] else 1})
            self.assertFalse(settings["overlays"]["cell"])
            self.assertEqual(settings["elements"]["Si"]["radius_A"], 1.1)
            self.assertEqual(settings["bond_cutoffs_A"], {"O-Si": 2})

    def test_invalid_configs(self):
        cases = [
            "[]", "{", '{"image":{"width":01}}', '{"image":{"width":1.}}',
            '{"image":{"width":1e}}', '{"image":{"width":512,"width":128}}',
            '{"image":{"width\\u0000junk":512}}', '{"image":{}} trailing',
            {"extra": {}}, {"image": {"width": "512"}}, {"image": {"width": 0}},
            {"image": {"width": True}}, {"image": {"height": 2.5}},
            {"image": {"background": "white"}}, {"image": {"width": 1e300}},
            {"image": {"style": "unknown"}}, {"image": {"depth_fade": 1.01}},
            {"image": {"depth_fade": -0.1}}, {"image": {"depth_fade": True}},
            {"image": {"outline_width": -1}}, {"image": {"outline_width": 9}},
            {"overlays": {"cell_width":0}}, {"overlays": {"cell_width":33}},
            {"overlays": {"cell_width":True}},
            {"camera": {"projection": "perspective"}}, {"camera": {"view": [0, 0, 0]}},
            {"camera": {"view": [0, 0, 1]}}, {"camera": {"up": [1, 2]}},
            {"camera": {"margin": 0.5}}, {"geometry": {"repeat": [1, 0, 1]}},
            {"geometry": {"sphere_scale": -1}}, {"geometry": {"bond_radius_A": False}},
            {"elements": {"Xx": {}}}, {"elements": {"Si": {"colour": "#ffffff"}}},
            {"bond_cutoffs_A": {"Si-O": 2, "O-Si": 3}}, {"bond_cutoffs_A": {"X-O": 2}},
            {"overlays": {"axes": 1}}, {"species": []}, {"species": ["Xx"]},
            {"camera": {"view": [float("inf"), 1, 1]}},
            '{"image":{"width":512}}\u0000',
            '{"unused":' + '[' * 33 + '0' + ']' * 33 + '}',
        ]
        for content in cases:
            with self.subTest(content=content):
                self.data(self.poscar, "--config", self.config(content), code=2)

    def test_cli_validation_and_limits(self):
        for args in [("--frame", "-1"), ("--width", "0"), ("--width", "99999999999999999999"),
                     ("--view", "0,0,0"), ("--view", "0,0,1"), ("--up", "0,0,0"),
                     ("--repeat", "1.5,1,1"), ("--repeat", "1024,1024,1024"),
                     ("--width", "16384", "--height", "16384"), ("--threads", "0"),
                     ("--width", "64", "--width", "32"), ("--missing", "42"),
                     ("--format", "xdatcar"), ("--background", "#gggggg"),
                     ("--view", "nan,1,1"), ("--repeat", "1,2,0"), ("--species", "Si,"),
                     ("--config",), ("--frame", "0x1")]:
            with self.subTest(args=args):
                self.data(self.poscar, *args, code=2)
        self.data(self.poscar, "--repeat", "1024,1024,1", code=6)

    def test_determinism_and_benchmark(self):
        first = self.data(self.poscar)
        self.assertEqual(first, self.data(self.poscar))
        path = FIXTURES / "POSCAR.skew"
        p = self.run_cli(path, "--inspect", "--benchmark-stages")
        self.assertIn("stages_ms:", p.stderr)
        json.loads(p.stdout)

    def test_no_destination_on_failure(self):
        output = self.root / "out.png"
        capabilities=json.loads(subprocess.check_output([str(EXE),"--build-info"],text=True))
        if not capabilities["openmp"]:
            self.run_cli(FIXTURES / "POSCAR.skew", "-o", output, "--threads", "2", code=3)
        self.assertFalse(output.exists())
        output.write_bytes(b"existing")
        self.run_cli(FIXTURES / "POSCAR.skew", "-o", output, "--axes", "on", "--width","64","--height","64",code=2)
        self.assertEqual(output.read_bytes(), b"existing")

    def test_io_and_format_errors(self):
        self.run_cli(self.root / "absent", "--inspect", code=7)
        self.data("not a known geometry\n", code=3)
        self.data(self.poscar + "x" * 65537, fmt="poscar", code=4)
        path = self.root / "nul"
        path.write_bytes(self.poscar.encode() + b"\0")
        self.run_cli(path, "--inspect", code=4)
        self.data(self.poscar + "x" * 65536 + "\n", fmt="poscar")
        self.data(self.poscar.replace("\n", "\r\n"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
