# SPDX-License-Identifier: GPL-3.0-only
"""End-to-end format selection, jobs, reports and atomic error handling."""
import json
from pathlib import Path
import resource
import signal
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
CAP=json.loads(subprocess.check_output([EXE,"--build-info"]))

class ImageFormats(unittest.TestCase):
    def test_cairo_outputs(self):
        if not CAP["cairo"]:self.skipTest("Cairo build required")
        for codec in ["jpeg","webp"]:
            if not CAP[codec]:continue
            with self.subTest(codec=codec), tempfile.TemporaryDirectory() as tmp:
                output=Path(tmp)/f"cairo.{codec}"
                cmd=[str(EXE),str(ROOT/"tests/fixtures/POSCAR.skew"),"-o",str(output),
                     "--backend","cairo","--width","48","--height","32","--no-user-config"]
                p=subprocess.run([*cmd,"--background","#ffffff","--report","-"],capture_output=True,text=True,timeout=30)
                self.assertEqual(p.returncode,0,p.stderr)
                report=json.loads(p.stdout)
                self.assertEqual(report["output"]["format"],codec)
                self.assertEqual(report["settings"]["image"]["backend"],"cairo")
                p=subprocess.run([*cmd,"--background","#ffffff00"],capture_output=True,timeout=30)
                self.assertEqual(p.returncode,2 if codec=="jpeg" else 0,p.stderr)

    def test_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp)
            for extension,codec in [("jpg","jpeg"),("JPEG","jpeg"),("WeBp","webp"),("png","png"),("other","png")]:
                with self.subTest(extension=extension):
                    output=directory/f"image.{extension}"
                    cmd=[str(EXE),str(ROOT/"tests/fixtures/POSCAR.skew"),"-o",str(output),
                         "--width","48","--height","32","--no-user-config","--background","#ffffff"]
                    p=subprocess.run([*cmd,"--report","-"],capture_output=True,text=True,timeout=30)
                    if codec!="png" and not CAP[codec]:
                        self.assertEqual(p.returncode,3,p.stderr)
                        self.assertIn("ENABLE_",p.stderr)
                        self.assertFalse(output.exists())
                        continue
                    self.assertEqual(p.returncode,0,p.stderr)
                    report=json.loads(p.stdout)
                    self.assertEqual(report["output"]["format"],codec)
                    self.assertEqual((report["output"]["width"],report["output"]["height"]),(48,32))
                    data=output.read_bytes()
                    if codec=="jpeg":self.assertTrue(data.startswith(b"\xff\xd8"))
                    elif codec=="webp":self.assertEqual((data[:4],data[8:12]),(b"RIFF",b"WEBP"))
                    else:self.assertTrue(data.startswith(b"\x89PNG"))
                    if codec!="png":
                        self.assertIn("encode",report["stages_ms"])
                        self.assertNotIn("png_encoder",report["settings"]["image"])
                    job=directory/"job.json"
                    job.write_text(json.dumps({"input":cmd[1],"output":str(output),"no_user_config":True,
                                               "config":{"image":{"width":48,"height":32,"background":"#ffffff"}}}))
                    p=subprocess.run([EXE,"--job",job],capture_output=True,timeout=30)
                    self.assertEqual(p.returncode,0,p.stderr)
                    self.assertEqual(output.read_bytes(),data)
                    requests=[{"id":str(i),"op":"render","job":json.loads(job.read_text())} for i in range(2)]
                    p=subprocess.run([EXE,"--batch"],input="".join(json.dumps(r)+"\n" for r in requests),
                                     capture_output=True,text=True,timeout=30)
                    self.assertEqual(p.returncode,0,p.stderr)
                    responses=[json.loads(line) for line in p.stdout.splitlines()]
                    self.assertEqual(len(responses),2)
                    self.assertTrue(all(r["ok"] for r in responses),responses)
                    self.assertTrue(responses[1]["result"]["cache"]["scene_hit"])
                    self.assertEqual(output.read_bytes(),data)
                    if codec=="png":continue
                    output.write_bytes(b"keep")
                    def short_file():
                        resource.setrlimit(resource.RLIMIT_FSIZE,(64,64))
                        signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
                    p=subprocess.run(cmd,capture_output=True,preexec_fn=short_file,timeout=30)
                    self.assertEqual(p.returncode,7,p.stderr)
                    self.assertEqual(output.read_bytes(),b"keep")
                    self.assertFalse(list(directory.glob("*.tmp.*")))
                    output.unlink();output.mkdir()
                    p=subprocess.run(cmd,capture_output=True,timeout=30)
                    self.assertEqual(p.returncode,7,p.stderr)
                    self.assertTrue(output.is_dir())
                    self.assertFalse(list(directory.glob("*.tmp.*")))
                    output.rmdir()
                    if codec=="jpeg":
                        output.write_bytes(b"keep")
                        p=subprocess.run([*cmd,"--background","#ffffff00"],capture_output=True,timeout=30)
                        self.assertEqual(p.returncode,2,p.stderr)
                        self.assertEqual(output.read_bytes(),b"keep")

if __name__=="__main__":unittest.main()
