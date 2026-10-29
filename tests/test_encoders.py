# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import random
import resource
import signal
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png

RENDER=Path(sys.argv.pop(1)).resolve()
BENCH=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
CAP=json.loads(subprocess.check_output([str(RENDER),"--build-info"],text=True))
ENCODERS=["libpng"]+(["libdeflate"] if CAP["libdeflate"] else [])


class EncoderTests(unittest.TestCase):
    def test_automatic_codec_resolution(self):
        if not CAP["libdeflate"]:self.skipTest("libdeflate build required")
        source=ROOT/"tests/fixtures/POSCAR.skew"
        cmd=[str(RENDER),str(source),"-o",str(self.png),"--width","48","--height","32",
             "--benchmark-stages","--png-encoder","libdeflate","--png-compression","auto"]
        p=subprocess.run(cmd,capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr)
        filesystem=subprocess.check_output(["stat","-f","-c","%t",str(self.path)],text=True).strip()
        expected=1 if filesystem=="1021997" else 0
        self.assertIn(f"png_compression_resolved={expected}",p.stderr)
        automatic=decode_png(self.png)
        p=subprocess.run([*cmd[:-1],str(expected)],capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual(decode_png(self.png),automatic)

    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix="qasr-png-")
        self.path=Path(self.tmp.name);self.raw=self.path/"image.rgba";self.png=self.path/"image.png"
        self.w,self.h=33,19
        rng=random.Random(20261029);p=bytearray(rng.randbytes(self.w*self.h*4))
        for i in range(0,len(p),4):
            if not p[i+3]:p[i:i+3]=b"\0\0\0"
        self.pixels=bytes(p);self.raw.write_bytes(self.pixels)

    def tearDown(self):
        self.tmp.cleanup()

    def command(self,encoder,level=1,filter_name="adaptive"):
        return [str(BENCH),str(self.w),str(self.h),str(self.raw),str(self.png),encoder,str(level),filter_name,"2"]

    def test_lossless_levels_and_filters(self):
        for encoder in ENCODERS:
            for level in [0,1,3,6,9]:
                for filter_name in ["none","sub","up","adaptive"]:
                    with self.subTest(encoder=encoder,level=level,filter=filter_name):
                        p=subprocess.run(self.command(encoder,level,filter_name),text=True,capture_output=True,timeout=30)
                        self.assertEqual(p.returncode,0,p.stderr)
                        report=json.loads(p.stdout)
                        self.assertEqual(len(report["encode_ms"]),2)
                        self.assertEqual(decode_png(self.png),(self.w,self.h,self.pixels))

    def test_cairo_opaque_and_rejected_alpha_roundtrip(self):
        if not CAP["cairo"]:self.skipTest("Cairo build required")
        p=subprocess.run(self.command("cairo"),text=True,capture_output=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr)
        decoded=decode_png(self.png)[2]
        self.assertEqual(decoded[3::4],self.pixels[3::4])
        self.assertNotEqual(decoded,self.pixels)
        opaque=bytearray(self.pixels);opaque[3::4]=bytes([255])*(self.w*self.h)
        self.raw.write_bytes(opaque)
        subprocess.run(self.command("cairo"),check=True,capture_output=True,timeout=30)
        self.assertEqual(decode_png(self.png)[2],opaque)
        p=subprocess.run([str(RENDER),str(ROOT/"tests/fixtures/POSCAR.skew"),"-o",str(self.png),
                          "--width","32","--height","32","--png-encoder","cairo","--benchmark-stages"],
                         text=True,capture_output=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertIn("png_encoder_internal=1",p.stderr)
        self.assertNotIn("png_compression_resolved=",p.stderr)

    def test_render_integration_and_option_precedence(self):
        source=ROOT/"tests/fixtures/POSCAR.skew"
        for style in ["shaded","illustrated"]:
            baseline=self.path/"baseline.png"
            cmd=[str(RENDER),str(source),"--width","65","--height","49","--background","#ffffff00","--style",style]
            subprocess.run([*cmd,"-o",str(baseline),"--png-encoder","libpng"],check=True,capture_output=True)
            expected=decode_png(baseline)
            for encoder in ENCODERS:
                for filter_name in ["none","sub","up","adaptive"]:
                    args=[*cmd,"-o",str(self.png),"--png-encoder",encoder,"--png-filter",filter_name,
                          "--threads","4" if CAP["openmp"] else "1"]
                    subprocess.run(args,check=True,capture_output=True,timeout=30)
                    self.assertEqual(decode_png(self.png),expected)
        config=self.path/"options.json"
        config.write_text(json.dumps({"image":{"png_encoder":"libpng","png_filter":"up","png_compression":6}}))
        inspected=json.loads(subprocess.check_output([str(RENDER),str(source),"--inspect","--config",str(config),
                                                     "--png-compression","0","--png-filter","sub"],text=True))
        self.assertEqual(inspected["settings"]["image"]["png_compression"],0)
        self.assertEqual(inspected["settings"]["image"]["png_filter"],"sub")
        for key,value in [("png_compression",-1),("png_compression",1.5),("png_compression",10),
                          ("png_filter","paeth-unknown"),("png_encoder","unknown")]:
            config.write_text(json.dumps({"image":{key:value}}))
            p=subprocess.run([str(RENDER),str(source),"--inspect","--config",str(config)],capture_output=True)
            self.assertEqual(p.returncode,2)

    def test_atomic_preservation_after_write_and_rename_failures(self):
        def short_file():
            resource.setrlimit(resource.RLIMIT_FSIZE,(64,64))
            signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
        for encoder in ENCODERS+(["cairo"] if CAP["cairo"] else []):
            with self.subTest(encoder=encoder):
                self.png.write_bytes(b"keep destination")
                p=subprocess.run(self.command(encoder),capture_output=True,preexec_fn=short_file,timeout=30)
                self.assertEqual(p.returncode,7,p.stderr)
                self.assertEqual(self.png.read_bytes(),b"keep destination")
                self.assertEqual(list(self.path.glob("*.tmp.*")),[])
                self.png.unlink();self.png.mkdir()
                p=subprocess.run(self.command(encoder),capture_output=True,timeout=30)
                self.assertEqual(p.returncode,7,p.stderr)
                self.assertTrue(self.png.is_dir())
                self.assertEqual(list(self.path.glob("*.tmp.*")),[])
                self.png.rmdir()


if __name__=="__main__":
    unittest.main()
