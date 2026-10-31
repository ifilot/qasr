# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png
from ray_reference import reference_pixels

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def minus(a, b):
    return [x - y for x, y in zip(a, b)]


def cross(a, b):
    return [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]


def poscar(basis, elements, coords, mode="Direct"):
    assert len(elements) == len(coords)
    # Each atom gets a separate species group, preserving test input ordering.
    return ("test\n1\n" + "\n".join(" ".join(map(str, row)) for row in basis) + "\n"
            + " ".join(elements) + "\n" + " ".join("1" for _ in elements)
            + "\n" + mode + "\n" + "\n".join(" ".join(map(str, row)) for row in coords) + "\n")


def brute_bonds(basis, fractional, elements, cutoff, span):
    keys = set()
    for i in range(len(elements)):
        for j in range(i, len(elements)):
            for t in itertools.product(range(-span, span + 1), repeat=3):
                if i == j and (not any(t) or next(v for v in t if v) < 0):
                    continue
                delta = [fractional[j][d] - fractional[i][d] + t[d] for d in range(3)]
                cart = [sum(delta[d]*basis[d][k] for d in range(3)) for k in range(3)]
                distance = math.sqrt(dot(cart, cart))
                if 0 < distance <= cutoff:
                    keys.add((i, j, *t))
    return keys


class SceneTests(unittest.TestCase):
    def test_tile_thread_determinism(self):
        capabilities=json.loads(subprocess.check_output([str(EXE),"--build-info"],text=True))
        if not capabilities["openmp"]:
            self.skipTest("OpenMP build required")
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["H","H","O","O"],
                    [[4,5,4],[6,5,6],[5,4,6],[5,6,4]],"Cartesian")
        cfg={"image":{"width":97,"height":65,"background":"#ffffff00"},
             "camera":{"view":[0,0,1],"up":[0,1,0]},"geometry":{"bond_radius_A":0.35},
             "elements":{"H":{"radius_A":0.15},"O":{"radius_A":0.15}},
             "overlays":{"cell":True},"bond_cutoffs_A":{"H-H":3,"O-O":3,"H-O":0.01}}
        for style in ["shaded","illustrated"]:
            cfg["image"]["style"]=style
            for aa in ["native","2x","4x","adaptive"]:
                with self.subTest(style=style,aa=aa):
                    expected=self.render(text,cfg,"--threads","1","--aa",aa)[2]
                    for workers in ["2","4","8"]:
                        for _ in range(2):
                            self.assertEqual(self.render(text,cfg,"--threads",workers,"--aa",aa)[2],expected)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="qasr-render-")
        self.path = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def invoke(self, text, *args, config=None, code=0):
        source = self.path / "POSCAR"
        source.write_text(text)
        cfg_args = []
        if config is not None:
            cfg = self.path / "config.json"
            cfg.write_text(json.dumps(config))
            cfg_args = ["--config", str(cfg)]
        p = subprocess.run([str(EXE), str(source), *cfg_args, *map(str, args)],
                           capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, code, p.stderr)
        return p

    def scene(self, text, *args, config=None):
        return json.loads(self.invoke(text, "--inspect-scene", *args, config=config).stdout)

    def render(self, text, config=None, *args):
        output = self.path / "image.png"
        self.invoke(text, "-o", output, *args, config=config)
        return decode_png(output)

    def test_imported_parameters(self):
        atoms = json.loads((ROOT / "vendor/atom-architect/atoms.json").read_text())["atoms"]
        # Check all 118 color/radius entries, not just the elements in our slabs.
        for z in range(1, 119):
            symbol = atoms["nr2element"][str(z)]
            scene = self.scene(poscar([[10,0,0],[0,10,0],[0,0,10]], [symbol], [[0.5]*3]))
            p = scene["primitives"][0]
            self.assertEqual(p["radius"], float(atoms["radii"][symbol]))
            self.assertEqual(p["rgba"], list(bytes.fromhex(atoms["colors"][symbol][1:])) + [255])

    def test_pair_rules_and_thresholds(self):
        cases = [("H", "H", 1.2), ("H", "C", 1.2), ("H", "Co", 2.0),
                 ("C", "O", 2.0), ("O", "Ce", 2.5), ("Co", "Ru", 3.0), ("C", "Al", 3.5)]
        for a, b, cutoff in cases:
            for distance, exists in [(cutoff - 1e-7, True), (cutoff, True), (cutoff + 1e-7, False)]:
                text = poscar([[20,0,0],[0,20,0],[0,0,20]], [a,b], [[5,5,5],[5+distance,5,5]], "Cartesian")
                for order in [False, True]:
                    cfg = {"bond_cutoffs_A": {f"{b}-{a}" if order else f"{a}-{b}": cutoff}}
                    result = self.scene(text, config=cfg)
                    self.assertEqual(result["base_bond_count"], int(exists), (a,b,distance))
            # Also test the built-in policy separately from explicit overrides.
            text = poscar([[20,0,0],[0,20,0],[0,0,20]], [a,b], [[5,5,5],[5+cutoff-1e-7,5,5]], "Cartesian")
            self.assertEqual(self.scene(text)["base_bond_count"], 1)

    def test_skewed_images_beyond_adjacent_cells(self):
        basis = [[1,0,0],[3.2,0.2,0],[0,0,10]]
        f = [[0.43,0.37,0.5]]
        scene = self.scene(poscar(basis, ["H"], f), "--repeat", "4,2,1", config={"bond_cutoffs_A": {"H-H": 0.3}})
        expected = brute_bonds(basis, f, ["H"], 0.3, 8)
        actual = {(p["source"][0], p["source"][1], *p["image"]) for p in scene["primitives"] if p["kind"] == "bond"}
        self.assertEqual(actual, expected)
        self.assertIn((0,0,3,-1,0), actual)
        self.assertEqual(scene["base_bond_count"], len(expected))

    def test_random_skewed_periodic_oracle(self):
        rng = random.Random(103)
        for _ in range(8):
            basis = [[1.3,0,0],[rng.uniform(1,3),0.8,0],[0.2,-0.1,2]]
            f = [[rng.random() for _ in range(3)] for _ in range(3)]
            elements = ["H", "O", "H"]
            cfg = {"bond_cutoffs_A": {"H-H": 0.8, "H-O": 0.8, "O-O": 0.8}}
            result = self.scene(poscar(basis,elements,f), "--repeat", "2,2,1", config=cfg)
            expected = brute_bonds(basis,f,elements,0.8,5)
            bonds = [p for p in result["primitives"] if p["kind"] == "bond"]
            actual = {(p["source"][0],p["source"][1],*p["image"]) for p in bonds}
            visible={key for key in expected if all(abs(shift)<size for shift,size in zip(key[2:],[2,2,1]))}
            self.assertEqual(actual,visible)
            self.assertEqual(result["base_bond_count"],len(expected))
            unique = {(tuple(p["source"]),tuple(p["image"]),tuple(p["origin"])) for p in bonds}
            self.assertEqual(len(unique),len(bonds))
            reciprocal = [cross(basis[1],basis[2]),cross(basis[2],basis[0]),cross(basis[0],basis[1])]
            volume = dot(basis[0],reciprocal[0])
            for p in bonds:
                for endpoint in [p["a"],p["b"]]:
                    fractional = [dot(endpoint, r)/volume for r in reciprocal]
                    for value, limit in zip(fractional,[2,2,1]):
                        self.assertGreaterEqual(value,-1e-10)
                        self.assertLessEqual(value,limit+1e-10)
            self.assertEqual(result["instance_count"],12)

    def test_wrap_repeat_camera_and_overrides(self):
        text = poscar([[10,0,0],[2,10,0],[0,0,10]],["C"],[[-0.1,1.2,0.5]])
        cfg = {"elements": {"C": {"radius_A": 0.7,"color":"#123456"}}}
        result = self.scene(text,"--repeat","2,2,2","--width","800","--height","200",config=cfg)
        spheres = [p for p in result["primitives"] if p["kind"] == "sphere"]
        self.assertEqual(len(spheres),8)
        self.assertEqual(sum(p["kind"]=="cell" for p in result["primitives"]),12)
        for value, expected in zip(spheres[0]["a"],[9.4,2,5]):
            self.assertAlmostEqual(value,expected)
        self.assertEqual(spheres[0]["rgba"],[18,52,86,255])
        camera = result["camera"]
        self.assertAlmostEqual(dot(cross(camera["right"],camera["up"]),camera["view"]),1)
        self.assertAlmostEqual(dot(camera["right"],camera["up"]),0)
        for p in result["primitives"]:
            for endpoint in [p["a"],p["b"]]:
                for d, axis in enumerate(["right","up"]):
                    extent = [800,200][d]
                    projected = (dot(endpoint,camera[axis])-camera["center"][d])*camera["scale"]+extent/2
                    self.assertGreaterEqual(projected-p["radius"]*camera["scale"],extent*0.06-1e-8)
                    self.assertLessEqual(projected+p["radius"]*camera["scale"],extent*0.94+1e-8)

    def test_limits_and_atomic_output(self):
        text = poscar([[10,0,0],[0,10,0],[0,0,10]],["H"],[[0.5]*3])
        out = self.path / "image.png"
        out.write_bytes(b"original")
        self.invoke(text,"-o",out,"--width","8192","--height","8192",code=6)
        self.assertEqual(out.read_bytes(),b"original")
        self.invoke(text,"-o",self.path/"missing/out.png",code=7)
        directory = self.path/"directory"
        directory.mkdir()
        self.invoke(text,"-o",directory,code=7)
        self.assertEqual(list(self.path.glob("*.tmp.*")),[])
        self.invoke(text,"--inspect-scene",config={"bond_cutoffs_A":{"H-H":1e300}},code=5)
        extreme = poscar([[1e-8,0,0],[0,1e-8,0],[0,0,10]],["H"],[[0.5]*3])
        self.invoke(extreme,"--inspect-scene",code=6)
        self.invoke(text,"-o",out)
        self.assertEqual(decode_png(out)[:2],(512,512))

    def test_dimensions_alpha_edges_and_determinism(self):
        text = poscar([[10,0,0],[0,10,0],[0,0,10]],["O"],[[0.5]*3])
        cfg={"image":{"width":80,"height":40,"background":"#ffffff00"},
             "overlays":{"cell":False}}
        w,h,pixels=self.render(text,cfg)
        self.assertEqual((w,h),(80,40))
        alphas=pixels[3::4]
        self.assertIn(0,alphas);self.assertIn(255,alphas)
        self.assertTrue(any(0<a<255 for a in alphas))
        for i in range(0,len(pixels),4):
            if 0<pixels[i+3]<255:
                self.assertEqual(pixels[i+1:i+3],b"\0\0")
        self.assertEqual(self.render(text,cfg)[2],pixels)

    def test_sphere_occlusion_independent_of_input_order(self):
        basis=[[10,0,0],[0,10,0],[0,0,10]]
        cfg={"camera":{"view":[0,0,1],"up":[0,1,0]},"overlays":{"cell":False},
             "elements":{"C":{"radius_A":0.8},"O":{"radius_A":0.4}},"bond_cutoffs_A":{"C-O":0.1}}
        a=poscar(basis,["C","O"],[[5,5,4],[5,5,6]],"Cartesian")
        b=poscar(basis,["O","C"],[[5,5,6],[5,5,4]],"Cartesian")
        self.assertEqual(self.render(a,cfg)[2],self.render(b,cfg)[2])

    def test_cylinder_occlusion_against_ray_reference(self):
        basis=[[10,0,0],[0,10,0],[0,0,10]]
        # Crossing tilted cylinders exchange depth order inside their projected
        # overlap. Other variants exercise axial caps, side walls and cell depth.
        cases=[
            (["H","H","O","O"],[[4,5,4],[6,5,6],[5,4,6],[5,6,4]],False),
            (["H","H"],[[5,5,4],[5,5,6]],False),
            (["H","H"],[[5,5,4],[5.000001,5,6]],False),
            (["H","H"],[[4,5,5],[6,5,5]],False),
            (["O"],[[0,0,9]],True),
        ]
        for elements,positions,cell in cases:
            with self.subTest(positions=positions):
                cfg={"image":{"width":40,"height":40},
                     "camera":{"view":[0,0,1],"up":[0,1,0]},
                     "overlays":{"cell":cell},"geometry":{"bond_radius_A":0.35},
                     "elements":{"H":{"radius_A":0.15},"O":{"radius_A":1.2 if cell else 0.15}},
                     "bond_cutoffs_A":{"H-H":3,"H-O":0.01,"O-O":3}}
                text=poscar(basis,elements,positions,"Cartesian")
                for aa,factor in [("native",1),("2x",2),("4x",4)]:
                    with self.subTest(aa=aa):
                        cfg["image"]["aa"]=aa
                        scene=self.scene(text,config=cfg)
                        actual=self.render(text,cfg)[2]
                        expected=reference_pixels(scene,factor)
                        self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)),1)

    def test_aa_validation_and_alpha(self):
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O"],[[0.5]*3])
        cfg={"image":{"width":40,"height":32,"aa":"4x","background":"#ffffff00"},
             "overlays":{"cell":False}}
        settings=json.loads(self.invoke(text,"--inspect","--aa","native",config=cfg).stdout)["settings"]
        self.assertEqual(settings["image"]["aa"],"native")
        for aa in ["2x","4x"]:
            cfg["image"]["aa"]=aa
            pixels=self.render(text,cfg)[2]
            self.assertTrue(any(0<a<255 for a in pixels[3::4]))
            for i in range(0,len(pixels),4):
                if 0<pixels[i+3]<255:
                    self.assertEqual(pixels[i+1:i+3],b"\0\0")
        self.invoke(text,"--inspect","--aa","3x",code=2)
        cfg["image"]["aa"]=4
        self.invoke(text,"--inspect",config=cfg,code=2)
        # 4x must account for all sixteen depth/color samples before allocating.
        self.invoke(text,"-o",self.path/"large.png","--width","4096",
                    "--height","4096","--aa","4x",code=6)

    def test_adaptive_flat_interiors_and_occlusion(self):
        basis=[[10,0,0],[0,10,0],[0,0,10]]
        # Odd dimensions and non-aligned centers catch native-tile row flips.
        cfg={"image":{"width":95,"height":73,"style":"illustrated","background":"#ffffff00"},
             "camera":{"view":[0,0,1],"up":[0,1,0]},"overlays":{"cell":False}}
        text=poscar(basis,["O"],[[4.8,5.2,5]],"Cartesian")
        uniform=self.render(text,cfg)[2]
        actual=self.render(text,cfg,"--aa","adaptive")[2]
        self.assertEqual(actual,uniform)
        proc=self.invoke(text,"-o",self.path/"adaptive.png","--aa","adaptive",
                         "--benchmark-stages",config=cfg)
        native=int(proc.stderr.split("adaptive_native_tiles=")[1].split()[0])
        self.assertGreater(native,0)
        # Crossing surfaces and thin primitives cannot qualify as isolated
        # interiors, even when the center ray misses a small foreground sphere.
        for positions in [
            [[4.7,5,5],[5.3,5,5]],
            [[4,5,4],[6,5,6],[5,4,6],[5,6,4]],
        ]:
            elements=["H","H"] if len(positions)==2 else ["H","H","O","O"]
            cfg["geometry"]={"bond_radius_A":0.035}
            cfg["bond_cutoffs_A"]={"H-H":3,"O-O":3,"H-O":0.01}
            cfg["elements"]={"H":{"radius_A":0.15},"O":{"radius_A":0.15}}
            text=poscar(basis,elements,positions,"Cartesian")
            self.assertEqual(self.render(text,cfg,"--aa","adaptive")[2],
                             self.render(text,cfg,"--aa","2x")[2])
        rng=random.Random(20261029)
        symbols=["H","C","N","O"]
        cfg["elements"]={symbol:{"radius_A":0.5+0.15*i} for i,symbol in enumerate(symbols)}
        cfg["bond_cutoffs_A"]={a+"-"+b:0.01 for a,b in itertools.combinations_with_replacement(symbols,2)}
        # Random overlapping foreground/background surfaces validate the depth
        # bounds against uniform per-sample depth, including partial alpha.
        cfg["elements"]["O"]["color"]="#e8806980"
        for _ in range(6):
            positions=[[rng.uniform(2,8) for _ in range(3)] for _ in range(12)]
            text=poscar(basis,[symbols[i%4] for i in range(12)],positions,"Cartesian")
            self.assertEqual(self.render(text,cfg,"--aa","adaptive")[2],
                             self.render(text,cfg,"--aa","2x")[2])

    def test_fixed_environment_golden_pixels(self):
        manifest=ROOT/"tests/golden_pixels.json"
        references=json.loads(manifest.read_text())
        for item in references["images"]:
            with self.subTest(name=item["name"]):
                source=ROOT/item["input"]
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),item["input_sha256"])
                scene=self.scene(source.read_text(),config=item["config"])
                self.assertEqual({'atom_instances':scene['instance_count'],'bonds':scene['bond_count']},item['scene_counts'])
                _,_,pixels=self.render(source.read_text(),item["config"])
                self.assertEqual(hashlib.sha256(pixels).hexdigest(),item["rgba_sha256"])

    def test_all_user_slabs_render(self):
        for path in sorted((ROOT/"tests/fixtures/slabs").glob("*/CONTCAR")):
            with self.subTest(structure=path.parent.name):
                w,h,pixels=self.render(path.read_text(),{"image":{"width":96,"height":96}})
                self.assertEqual((w,h),(96,96))
                self.assertTrue(any(pixels[i:i+3]!=b"\xff\xff\xff" for i in range(0,len(pixels),4)))

    def test_surface_normal_top_view(self):
        for sign in [1,-1]:
            basis=[[2,0,1],[0.3,3,0.5],[0,0,10*sign]]
            text=poscar(basis,["O"],[[0.5]*3])
            camera=self.scene(text,"--view","top")["camera"]
            expected=cross(basis[0],basis[1])
            expected=[sign*v/math.sqrt(dot(expected,expected)) for v in expected]
            for actual,value in zip(camera["view"],expected):
                self.assertAlmostEqual(actual,value,places=12)
            self.assertAlmostEqual(dot(camera["right"],basis[0])/math.sqrt(dot(basis[0],basis[0])),1)
            self.assertAlmostEqual(dot(camera["view"],camera["up"]),0)
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O"],[[0.5]*3])
        cfg={"camera":{"view":"top"}}
        camera=self.scene(text,"--view","1,1,1",config=cfg)["camera"]
        self.assertAlmostEqual(camera["view"][2],1/math.sqrt(3))
        self.invoke(text,"--inspect-scene","--view","top","--up","0,0,1",code=2)

    def test_illustrated_color_bands(self):
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O"],[[0.5]*3])
        cfg={"image":{"width":96,"height":96,"style":"illustrated"},
             "camera":{"view":"top"},"overlays":{"cell":False}}
        w,h,pixels=self.render(text,cfg)
        colors={pixels[i:i+4] for i in range(0,len(pixels),4)}
        for color in [(181,0,0,255),(224,26,26,255),(240,140,140,255)]:
            self.assertIn(bytes(color),colors)
        self.assertLess(len(colors),40)
        self.assertEqual(pixels,self.render(text,cfg)[2])
        self.invoke(text,"--inspect","--style","unknown",code=2)
        settings=json.loads(self.invoke(text,"--inspect","--style","shaded",config=cfg).stdout)["settings"]
        self.assertEqual(settings["image"]["style"],"shaded")

    def test_illustration_depth_fade(self):
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O","O"],[[3,5,3],[7,5,7]],"Cartesian")
        cfg={"image":{"width":128,"height":64,"style":"illustrated"},
             "camera":{"view":"top"},"overlays":{"cell":False}}
        baseline=self.render(text,cfg)[2]
        original_scene=self.scene(text,config=cfg)
        cfg["image"]["depth_fade"]=0.9
        faded=self.render(text,cfg)[2]
        self.assertEqual(self.scene(text,config=cfg),original_scene)
        self.assertNotEqual(baseline,faded)
        for y in range(64):
            self.assertEqual(baseline[4*(y*128+64):4*((y+1)*128)],faded[4*(y*128+64):4*((y+1)*128)])

    def test_illustrated_contours(self):
        cfg={"image":{"width":96,"height":96,"style":"illustrated","outline_width":0},
             "camera":{"view":"top"},"overlays":{"cell":False},
             "geometry":{"bond_radius_A":0.3}}
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O","O"],[[4.2,5,5],[5.8,5,5]],"Cartesian")
        plain=self.render(text,cfg)[2]
        self.assertNotIn(b"\0\0\0\xff",{plain[i:i+4] for i in range(0,len(plain),4)})
        cfg["image"]["outline_width"]=1.5
        outlined=self.render(text,cfg)[2]
        self.assertIn(b"\0\0\0\xff",{outlined[i:i+4] for i in range(0,len(outlined),4)})
        # Compare bond-only pixels: contours add black side edges while its core remains gray.
        black_in_middle=0
        for y in range(96):
            for x in range(45,51):
                pixel=outlined[4*(y*96+x):4*(y*96+x)+4]
                black_in_middle+=pixel==b"\0\0\0\xff"
        self.assertGreater(black_in_middle,0)
        self.assertNotEqual(plain,outlined)
        self.invoke(text,"--inspect","--outline-width","-1",code=2)

    def test_illustrated_user_gallery(self):
        cfg=json.loads((ROOT/"presets/illustrated.json").read_text())
        cfg["image"].update(width=96,height=96)
        for path in sorted((ROOT/"tests/fixtures/slabs").glob("*/CONTCAR")):
            with self.subTest(structure=path.parent.name):
                w,h,pixels=self.render(path.read_text(),cfg)
                self.assertEqual((w,h),(96,96))
                camera=self.scene(path.read_text(),config=cfg)["camera"]
                self.assertAlmostEqual(camera["view"][2],1)
                self.assertTrue(any(v!=255 for v in pixels[0::4]))

    def test_cell_width_pixels_and_overrides(self):
        text=poscar([[10,0,0],[0,10,0],[0,0,10]],["O"],[[0.5]*3])
        cfg={"image":{"width":128,"height":128},"camera":{"view":"top"},
             "overlays":{"cell":True,"cell_width":2}}
        for width in [2,6]:
            cfg["overlays"]["cell_width"]=width
            _,_,pixels=self.render(text,cfg)
            # Measure the visible left boundary in a horizontal image scan,
            # away from atoms/corners. Width is specified in output pixels.
            row=pixels[4*(64*128):4*(65*128)]
            dark=sum(all(v<100 for v in row[4*x:4*x+3]) for x in range(32))
            self.assertLessEqual(abs(dark-width),1)
        settings=json.loads(self.invoke(text,"--inspect","--cell-width","4",config=cfg).stdout)["settings"]
        self.assertEqual(settings["overlays"]["cell_width"],4)
        for value in ["0","-1","33","nan"]:
            self.invoke(text,"--inspect","--cell-width",value,code=2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
