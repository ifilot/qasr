# SPDX-License-Identifier: GPL-3.0-only
"""Cross-PR inputs x styles x image formats, CLI/jobs/workers and alpha."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png
try:
    from PIL import Image
except ImportError:
    Image = None

EXE=Path(sys.argv.pop(1)).resolve()
CAP=json.loads(subprocess.check_output([EXE,"--build-info"]))
ATOMS="O 5 5 5\nH 6 5 5\nH 4.75 5.75 5\n"
SOURCES={
    "poscar":"water\n1\n16 0 0\n0 16 0\n0 0 16\nO H\n1 2\nCartesian\n5 5 5\n6 5 5\n4.75 5.75 5\n",
    "xyz":"3\nwater\n"+ATOMS,
    "extxyz":'3\nLattice="16 0 0 0 16 0 0 0 16" pbc="F F F" Properties=species:S:1:pos:R:3\n'+ATOMS,
    "gaussian-input":"# HF/3-21G\n\nwater\n\n0 1\n"+ATOMS+"\n",
    "gaussian-output":"Entering Gaussian System\n Standard orientation:\n -----\n Center Atomic Atomic Coordinates (Angstroms)\n Number Number Type X Y Z\n -----\n 1 8 0 5 5 5\n 2 1 0 6 5 5\n 3 1 0 4.75 5.75 5\n -----\n",
    "ams":"System\n Atoms\n"+ATOMS+" End\nEnd\n"}
VARIANTS=[(s,t) for s in ["ortep","balsac"] for t in ["color","monocolor","dualtone"]]
CODECS=["png"]+[name for name in ["jpeg","webp"] if CAP.get(name)]


@unittest.skipIf(Image is None,"Pillow required for independent JPEG/WebP decoding")
class Compatibility(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)

    def run_cli(self,*args,code=0,stdin=None):
        prefix=[EXE] if args and args[0]=="--batch" else [EXE,"--no-user-config"]
        r=subprocess.run([*prefix,*map(str,args)],input=stdin,
                         text=True,capture_output=True,timeout=30)
        self.assertEqual(r.returncode,code,r.stderr)
        return r

    def config(self,style,tone,transparent=False):
        return {"image":{"width":96,"height":96,"style":style,"tone":tone,
                         "background":"#ffffff00" if transparent else "#ffffff"},
                "overlays":{"cell":False}}

    def decode(self,path):
        with Image.open(path) as image:
            self.assertEqual(image.size,(96,96))
            image.load()
            return image.convert("RGBA").tobytes()

    def test_inputs_styles_codecs_and_automation(self):
        canonical={};requests=[];expected=[];cli_count=0
        for fmt,text in SOURCES.items():
            source=self.root/(fmt+".data");source.write_text(text)
            for style,tone in VARIANTS:
                cfg=self.config(style,tone);config=self.root/"config.json"
                config.write_text(json.dumps(cfg))
                for codec in CODECS:
                    key=(style,tone,codec);out=self.root/f"{fmt}-{style}-{tone}.{codec}"
                    r=self.run_cli(source,"--config",config,"-o",out,"--report","-")
                    report=json.loads(r.stdout)
                    self.assertEqual(report["input"]["format"],fmt)
                    self.assertEqual(report["input"]["pbc"],[True]*3 if fmt=="poscar" else [False]*3)
                    self.assertEqual(report["output"]["format"],codec)
                    self.assertEqual(report["settings"]["image"]["tone"],tone)
                    pixels=self.decode(out)
                    if key in canonical:self.assertEqual(pixels,canonical[key],key)
                    else:canonical[key]=pixels
                    if codec=="webp":self.assertEqual(pixels,canonical[(style,tone,"png")])
                    if codec=="jpeg":
                        png=canonical[(style,tone,"png")]
                        self.assertLess(sum(abs(a-b) for a,b in zip(pixels,png))/len(png),12)
                    expected.append(out.read_bytes())
                    job={"input":str(source),"output":str(out),"no_user_config":True,"config":cfg}
                    requests.append({"id":str(len(requests)),"op":"render","job":job})
                    # Exercise stdin job dispatch for every input/tone/output.
                    self.run_cli("--job","-",stdin=json.dumps(job))
                    self.assertEqual(out.read_bytes(),expected[-1])
                    cli_count+=1
        r=self.run_cli("--batch",stdin="".join(json.dumps(q)+"\n" for q in requests))
        replies=[json.loads(line) for line in r.stdout.splitlines()]
        self.assertEqual(len(replies),len(requests))
        for i,(request,reply) in enumerate(zip(requests,replies)):
            self.assertTrue(reply["ok"],reply)
            self.assertEqual(Path(request["job"]["output"]).read_bytes(),expected[i])
            if i%(len(VARIANTS)*len(CODECS)):self.assertTrue(reply["result"]["cache"]["scene_hit"])
        print(f"Verified {cli_count} input/style/codec combinations through CLI, jobs and cached worker.")

    def test_partial_pbc_with_new_styles_and_codecs(self):
        source=self.root/"slab.in"
        source.write_text("System\nAtoms\nH .5 .5 -3\nH 1.2 .5 -3\nEnd\nLattice\n3 0 0\n0 3 0\nEnd\nEnd\n")
        for style,tone in VARIANTS:
            reference=None
            for codec in CODECS:
                out=self.root/f"slab-{style}-{tone}.{codec}"
                r=self.run_cli(source,"--style",style,"--tone",tone,"--repeat","2,2,1",
                               "--width","96","--height","96","-o",out,"--report","-")
                report=json.loads(r.stdout)
                self.assertEqual(report["input"]["pbc"],[True,True,False])
                self.assertEqual(report["input"]["cell_present"],[True,True,False])
                pixels=self.decode(out)
                if codec=="png":reference=pixels
                if codec=="webp":self.assertEqual(pixels,reference)

    def test_transparency_preservation_and_jpeg_rejection(self):
        source=self.root/"water.xyz";source.write_text(SOURCES["xyz"])
        for style,tone in VARIANTS:
            cfg=self.root/"config.json";cfg.write_text(json.dumps(self.config(style,tone,True)))
            png=self.root/"alpha.png"
            self.run_cli(source,"--config",cfg,"-o",png)
            expected=self.decode(png);self.assertEqual(expected[3],0)
            if CAP.get("webp"):
                webp=self.root/"alpha.webp";self.run_cli(source,"--config",cfg,"-o",webp)
                self.assertEqual(self.decode(webp),expected)
            if CAP.get("jpeg"):
                jpeg=self.root/"alpha.jpg";jpeg.write_bytes(b"existing")
                self.run_cli(source,"--config",cfg,"-o",jpeg,code=2)
                self.assertEqual(jpeg.read_bytes(),b"existing")


if __name__=="__main__":unittest.main(verbosity=2)
