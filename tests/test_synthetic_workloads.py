# SPDX-License-Identifier: GPL-3.0-only
"""Independent checks for synthetic source data and baseline-workload contracts."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from generate_m10_workloads import generate,digest
from run_m10_workloads import run_suite,verify_suite,confined
CAPABILITIES=json.loads(subprocess.check_output([str(EXE),'--build-info']))


class SyntheticWorkloadTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.suite=self.root/'suite'
        self.manifest=generate(self.suite,'smoke')

    def test_byte_reproducibility_and_seed_changes(self):
        other=self.root/'same';generate(other,'smoke')
        self.assertEqual({str(p.relative_to(self.suite)):digest(p) for p in self.suite.rglob('*') if p.is_file()},
                         {str(p.relative_to(other)):digest(p) for p in other.rglob('*') if p.is_file()})
        different=self.root/'different';generate(different,'smoke',seed=1730)
        self.assertNotEqual(digest(self.suite/'inputs/POSCAR.bulk'),digest(different/'inputs/POSCAR.bulk'))

    def test_existing_destination_and_bounds_preserve_data(self):
        before=digest(self.suite/'manifest.json')
        with self.assertRaises(FileExistsError):generate(self.suite,'smoke')
        self.assertEqual(before,digest(self.suite/'manifest.json'))
        for kwargs in [dict(atoms=0),dict(frames=1),dict(seed=-1),dict(atoms=10000,frames=4096)]:
            with self.subTest(kwargs=kwargs),self.assertRaises(ValueError):generate(self.root/'bad','smoke',**kwargs)
        self.assertFalse((self.root/'bad').exists())

    def test_geometry_oracles_against_renderer(self):
        _,checked=verify_suite(self.suite,EXE,CAPABILITIES)
        self.assertEqual(checked,15 if CAPABILITIES['xml'] else 12)
        oracle=json.loads((self.suite/'oracles/POSCAR.boundary.json').read_text())
        self.assertEqual(oracle['positions_A'],[[-.1,2,2],[4.1,2,2]])
        slab=json.loads((self.suite/'oracles/POSCAR.slab.json').read_text())
        z=[p[2] for p in slab['positions_A']]
        self.assertGreater(min(z),7);self.assertGreater(slab['lattice_A'][2][2]-max(z),7)

    def test_xml_structure_and_poscar_frames_agree(self):
        xml=ET.parse(self.suite/'inputs/vasprun.synthetic.xml').getroot()
        calculations=xml.findall('calculation');self.assertEqual(len(calculations),4)
        for frame in [0,2,3]:
            actual=[[float(v) for v in node.text.split()] for node in calculations[frame].findall('structure/varray[@name="positions"]/v')]
            poscar=(self.suite/f'inputs/POSCAR.frame-{frame:04}').read_text().splitlines()
            self.assertEqual(actual,[[float(v) for v in line.split()] for line in poscar[8:]])
        self.assertNotEqual(calculations[0].find('structure/varray/v').text,calculations[3].find('structure/varray/v').text)

    def test_changed_input_and_path_escape_rejected(self):
        with self.assertRaises(ValueError):confined(self.suite,'../outside')
        with self.assertRaises(ValueError):confined(self.suite,'/tmp/outside')
        (self.suite/'inputs/POSCAR.bulk').write_text('tampered')
        with self.assertRaisesRegex(ValueError,'suite file changed'):verify_suite(self.suite,EXE,CAPABILITIES)

    def run_baseline(self,folder,**kwargs):
        with contextlib.redirect_stdout(io.StringIO()):
            return run_suite(EXE,self.suite,self.root/folder,samples=1,warmups=0,**kwargs)

    def test_all_families_and_error_continuation(self):
        report=self.run_baseline('baseline')
        self.assertTrue(report['passed'])
        self.assertEqual(len(report['results']),29 if CAPABILITIES['xml'] else 23)
        self.assertEqual(len(report['skipped']),0 if CAPABILITIES['xml'] else 6)
        byid={r['id']:r for r in report['results']}
        self.assertEqual(byid['isolation-002']['exit_code'],4)
        self.assertEqual(byid['isolation-003']['exit_code'],2)
        self.assertEqual(byid['isolation-000']['decoded_rgba_sha256'],byid['isolation-004']['decoded_rgba_sha256'])
        for key in ['bytes','mtime_ns','inode']:
            self.assertEqual(byid['invalidation-000']['input_identity'][key],byid['invalidation-001']['input_identity'][key])
        self.assertNotEqual(byid['invalidation-000']['decoded_rgba_sha256'],byid['invalidation-001']['decoded_rgba_sha256'])
        self.assertEqual(byid['invalidation-000']['decoded_rgba_sha256'],byid['invalidation-002']['decoded_rgba_sha256'])
        if CAPABILITIES['xml']:
            for i,frame in enumerate([0,2,3]):
                png=self.root/'baseline'/f'frame-oracle-{frame}.png'
                subprocess.run([str(EXE),str(self.suite/f'inputs/POSCAR.frame-{frame:04}'),'--width','128','--height','128','--png-encoder','libpng','--png-compression','1','--no-user-config','-o',str(png)],check=True,capture_output=True)
                from png_support import decode_png
                import hashlib
                self.assertEqual(hashlib.sha256(decode_png(png)[2]).hexdigest(),byid[f'frames-{i:03}']['decoded_rgba_sha256'])

    def test_reference_comparison_and_fail_closed(self):
        self.run_baseline('reference',families=['invalidation'])
        reference=self.root/'reference/report.json'
        report=self.run_baseline('comparison',families=['invalidation'],reference=reference)
        self.assertTrue(all('reference_median_ratio' in r for r in report['results']))
        original=json.loads(reference.read_text());original['results'][0]['decoded_rgba_sha256']='wrong'
        reference.write_text(json.dumps(original))
        with self.assertRaisesRegex(ValueError,'reference correctness differs'):
            self.run_baseline('bad-comparison',families=['invalidation'],reference=reference)
        self.assertFalse(json.loads((self.root/'bad-comparison/report.json').read_text())['passed'])


if __name__=='__main__':unittest.main()
