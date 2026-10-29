# SPDX-License-Identifier: GPL-3.0-only
"""Experimental Cairo behavior, including an explicit failed depth gate."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png
from ray_reference import reference_pixels

EXE=Path(sys.argv.pop(1)).resolve()


class PainterTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix="qasr-painter-")
        self.path=Path(self.tmp.name)
        self.source=self.path/"POSCAR"
        self.config=self.path/"config.json"
        self.source.write_text("gate\n1\n10 0 0\n0 10 0\n0 0 10\nO\n1\nCartesian\n5 5 5\n")
        self.settings={"image":{"width":64,"height":64,"background":"#ffffff00"},
                       "camera":{"view":[0,0,1],"up":[0,1,0]},"overlays":{"cell":False}}

    def tearDown(self):
        self.tmp.cleanup()

    def invoke(self,*args,code=0):
        self.config.write_text(json.dumps(self.settings))
        p=subprocess.run([str(EXE),str(self.source),"--config",str(self.config),*map(str,args)],
                         text=True,capture_output=True,timeout=60)
        self.assertEqual(p.returncode,code,p.stderr)
        return p

    def render(self,name,*args):
        path=self.path/(name+".png")
        self.invoke("-o",path,*args)
        return path,decode_png(path)

    def test_alpha_grids_determinism(self):
        for aa in ["native","2x","4x"]:
            with self.subTest(aa=aa):
                _,(w,h,pixels)=self.render(aa,"--backend","cairo","--aa",aa)
                self.assertEqual((w,h),(64,64))
                self.assertIn(0,pixels[3::4]);self.assertIn(255,pixels[3::4])
                self.assertTrue(any(0<a<255 for a in pixels[3::4]))
                for i in range(0,len(pixels),4):
                    if pixels[i+3]:
                        self.assertEqual(pixels[i+1:i+3],b"\0\0")
                self.assertEqual(pixels,self.render(aa+"-again","--backend","cairo","--aa",aa)[1][2])

    def test_rejected_depth_gate(self):
        # Equal-depth, interpenetrating red/blue spheres switch surface order
        # within their overlap. No whole-primitive painter ordering can pass.
        self.source.write_text("overlap\n1\n10 0 0\n0 10 0\n0 0 10\nH O\n1 1\nCartesian\n4.6 5 5\n5.4 5 5\n")
        self.settings["image"].update(width=96,height=96,style="illustrated",outline_width=0)
        self.settings["elements"]={"H":{"color":"#ff0000","radius_A":1},
                                   "O":{"color":"#0000ff","radius_A":1}}
        self.settings["bond_cutoffs_A"]={"H-O":0.01}
        scene=json.loads(self.invoke("--inspect-scene").stdout)
        zpath,(w,h,z)=self.render("zbuffer","--backend","zbuffer","--aa","2x")
        cpath,(_,_,c)=self.render("cairo-native","--backend","cairo","--aa","native")
        reference=reference_pixels(scene)
        # Oracle is shaded, whereas this gate uses illustrated colors. Check
        # which color dominates instead of conflating the shading models.
        x=int(w/2-0.25*scene["camera"]["scale"]);y=int(h/2)
        index=4*(y*w+x)
        self.assertGreater(reference[index],reference[index+2])
        self.assertGreater(z[index],z[index+2])
        self.assertGreater(c[index+2],c[index])
        for backend,aa in [("cairo","2x"),("cairo","4x")]:
            _,(_,_,pixels)=self.render(backend+"-"+aa,"--backend",backend,"--aa",aa)
            self.assertGreater(pixels[index+2],pixels[index])
        _,(_,_,adaptive)=self.render("adaptive","--aa","adaptive")
        self.assertEqual(z,adaptive)
        destination=os.environ.get("QASR_EXPERIMENT_ARTIFACTS")
        if destination:
            target=Path(destination)/"depth-gate";target.mkdir(parents=True,exist_ok=True)
            for path in self.path.iterdir():
                if path.is_file(): shutil.copyfile(path,target/path.name)
            (target/"gate.json").write_text(json.dumps({
                "cairo_accepted":False,"reason":"interpenetrating spheres change surface depth order",
                "witness_xy":[x,y],"zbuffer_rgba":list(z[index:index+4]),
                "cairo_rgba":list(c[index:index+4]),"scene":scene},indent=2)+"\n")

    def test_limits_unsupported_combinations_and_atomic_output(self):
        out=self.path/"protected.png";out.write_bytes(b"keep")
        self.invoke("-o",out,"--backend","cairo","--aa","adaptive",code=3)
        self.assertEqual(out.read_bytes(),b"keep")
        self.invoke("-o",out,"--backend","cairo","--aa","4x",
                    "--width","4096","--height","4096",code=6)
        self.assertEqual(out.read_bytes(),b"keep")
        self.assertEqual(list(self.path.glob("*.tmp.*")),[])
        self.settings["image"].update(width=256,height=256)
        self.invoke("-o",out,"--backend","cairo","--axes","on",code=3)
        self.assertEqual(out.read_bytes(),b"keep")
        self.invoke("--inspect","--backend","unknown",code=2)


if __name__=="__main__":
    unittest.main()
