# SPDX-License-Identifier: GPL-3.0-only
"""Source-independent geometry, reader and automation contracts; no ASE required."""
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

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
WATER = '3\nwater\nO 0 0 0\nH .96 0 0\nH -.24 .93 0\n'
GJF = '%chk=water.chk\n# HF/3-21G\n\nwater\n\n0 1\nO 0 0 0\nH .96 0 0\nH -.24 .93 0\n\n'
AMS = 'Task SinglePoint\nSystem\n Atoms\n O 0 0 0\n H .96 0 0\n H -.24 .93 0\n End\nEnd\nEngine DFTB\n Model GFN1-xTB\nEndEngine\n'


def extxyz(cell, pbc, atoms, properties='species:S:1:pos:R:3'):
    lattice = ' '.join(str(x) for row in cell for x in row)
    flags = ' '.join('T' if x else 'F' for x in pbc)
    return f'{len(atoms)}\nLattice="{lattice}" pbc="{flags}" Properties={properties}\n' + ''.join(
        f'{element} '+ ' '.join(map(str, p))+'\n' for element,p in atoms)


def orientation(kind, rows):
    return f' {kind} orientation:\n -----\n Center Atomic Atomic Coordinates (Angstroms)\n Number Number Type X Y Z\n -----\n' + ''.join(
        f' {i+1} {z} 0 {x} {y} {zz}\n' for i,(z,x,y,zz) in enumerate(rows))+' -----\n'


class Inputs(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.path = self.root/'input.data'

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, ok=True):
        p = subprocess.run([str(EXE), '--no-user-config', *map(str,args)], text=True, capture_output=True, timeout=20)
        if ok:
            self.assertEqual(p.returncode, 0, p.stderr)
        else:
            self.assertNotEqual(p.returncode, 0)
            self.assertTrue(p.stderr)
            self.assertEqual(p.stdout, '')
        return p

    def read(self, text, *args, scene=False, ok=True):
        self.path.write_text(text)
        p = self.run_cli(self.path, '--inspect-scene' if scene else '--inspect', *args, ok=ok)
        return json.loads(p.stdout) if ok else p

    def test_molecule_coordinates_order_and_bonds(self):
        data = self.read(WATER)
        self.assertEqual(data['format'], 'xyz')
        self.assertEqual(data['pbc'], [False]*3)
        self.assertEqual(data['cell_present'], [False]*3)
        self.assertEqual(data['atoms'][2]['position_A'], [-.24,.93,0])
        scene = self.read(WATER, scene=True)
        self.assertEqual(scene['instance_count'], 3)
        self.assertEqual(scene['base_bond_count'], 2)
        self.assertEqual(scene['bond_count'], 2)
        self.assertFalse(any(p['kind']=='cell' for p in scene['primitives']))
        self.assertEqual([p['source'][0] for p in scene['primitives'] if p['kind']=='sphere'], [0,1,2])

    def test_sources_produce_identical_molecule_pixels(self):
        images=[]
        for text in [WATER,GJF,AMS]:
            self.path.write_text(text)
            output=self.root/'image.png'
            self.run_cli(self.path, '-o', output, '--width','64','--height','64')
            images.append(decode_png(output)[2])
        self.assertEqual(images[0],images[1])
        self.assertEqual(images[0],images[2])

    def test_xyz_trajectories_changing_count_and_validation_to_eof(self):
        text=WATER+'1\nhelium\nHe 4 5 6\n'
        self.assertEqual(self.read(text)['atom_count'],1)
        data=self.read(text,'--frame','0')
        self.assertEqual((data['frame'],data['frame_count'],data['atom_count']),(0,2,3))
        self.read(text,'--frame','2',ok=False)
        self.read(text+'1\nbroken\nHe NaN 0 0\n','--frame','0',ok=False)
        self.read(text+'2\ntruncated\nH 0 0 0\n','--frame','0',ok=False)

    def test_xyz_validation(self):
        for bad in ['0\nempty\n','2\nshort\nH 0 0 0\n', '1\nunknown\nXx 0 0 0\n',
                    '1\nnonfinite\nH Inf 0 0\n','1\nextra\nH 0 0 0 junk\n',
                    '1\nnul\nH 0 0 0\x00\n','1000001\nlarge\n']:
            with self.subTest(bad=bad): self.read(bad,ok=False)
        self.read(WATER,'--recover',ok=False)
        self.read(WATER,'--species','O,H',ok=False)

    def test_display_cell_does_not_imply_periodicity(self):
        text=extxyz([[2,0,0],[0,2,0],[0,0,2]], [False]*3, [('H',[-2,0,0]),('H',[-1.3,0,0])])
        scene=self.read(text,scene=True)
        self.assertEqual(scene['base_bond_count'],1)
        spheres=[p for p in scene['primitives'] if p['kind']=='sphere']
        self.assertEqual(spheres[0]['a'],[-2,0,0])
        self.assertEqual(sum(p['kind']=='cell' for p in scene['primitives']),12)

    def test_extended_columns_atomic_numbers_and_defaults(self):
        text='2\nProperties=tag:I:1:pos:R:3:Z:I:1:force:R:3\n5 0 0 0 8 1 2 3\n6 .96 0 0 1 0 0 0\n'
        data=self.read(text)
        self.assertEqual([a['element'] for a in data['atoms']], ['O','H'])
        self.assertEqual(data['format'],'extxyz')
        self.assertEqual(data['pbc'],[False]*3)
        text=extxyz([[2,0,0],[1,2,0],[0,0,2]],[True]*3,[('Si',[0,0,0])]).replace(' pbc="T T T"','')
        self.assertEqual(self.read(text)['pbc'],[True]*3)
        self.read(text.replace('species:S:1','species:R:1'),ok=False)
        self.read('1\nProperties=species:S:1:Z:I:1:pos:R:3\nH 8 0 0 0\n',ok=False)
        self.read('1\nProperties=species:S:1:pos:R:3 pbc="T F F"\nH 0 0 0\n',ok=False)
        self.read(text.replace('Properties=','Lattice="1 0 0 0 1 0 0 0 1" Properties='),ok=False)

    def test_extended_positions_reused_with_reordered_properties(self):
        for properties, row in [
            ('pos:R:3:fixed:L:1:species:S:1:charge:R:1', '1D-1 -2.5 3 T C .25'),
            ('fixed:L:1:charge:R:1:species:S:1:pos:R:3', 'T .25 C 1D-1 -2.5 3'),
        ]:
            text = f'1\nProperties={properties}\n{row}\n'
            self.assertEqual(self.read(text)['atoms'][0]['position_A'], [.1, -2.5, 3])
            for old, bad in [('1D-1', 'NaN'), ('.25', 'Inf'), ('T', 'invalid')]:
                with self.subTest(properties=properties, bad=bad):
                    self.read(text.replace('\n' + row, '\n' + row.replace(old, bad)), ok=False)

    def test_partial_cells_edges_wrap_and_replication(self):
        wire=extxyz([[2,0,0],[0,0,0],[0,0,0]],[True,False,False],[('H',[2.1,-3,5]),('H',[1.9,-3,5])])
        scene=self.read(wire,scene=True)
        spheres=[p for p in scene['primitives'] if p['kind']=='sphere']
        for actual, expected in zip(spheres[0]['a'],[.1,-3,5]): self.assertAlmostEqual(actual,expected)
        self.assertEqual(scene['base_bond_count'],1)
        self.assertEqual(sum(p['kind']=='cell' for p in scene['primitives']),1)
        for p in scene['primitives']:
            if p['kind']=='bond': self.assertEqual(p['image'][1:],[0,0])
        self.assertEqual(self.read(wire,'--repeat','2,1,1',scene=True)['instance_count'],4)
        self.read(wire,'--repeat','1,2,1',ok=False)
        self.read(wire,'--view','top',scene=True,ok=False)
        slab=extxyz([[2,0,0],[1,2,0],[0,0,0]],[True,True,False],[('Si',[0,0,-5])])
        self.assertEqual(sum(p['kind']=='cell' for p in self.read(slab,'--view','top',scene=True)['primitives']),4)
        self.assertEqual(self.read(slab,'--expand-xy','1,1',scene=True)['instance_count'],9)

    def test_all_pbc_masks_against_cartesian_image_oracle(self):
        rng=random.Random(193)
        cell=[[3,0,0],[1,2.8,0],[.5,.3,3.2]]
        atoms=[('H',[rng.uniform(-1,4) for _ in range(3)]) for _ in range(5)]
        cutoff=1.6
        config=self.root/'config.json'
        config.write_text(json.dumps({'bond_cutoffs_A':{'H-H':cutoff}}))
        for mask in itertools.product([False,True],repeat=3):
            scene=self.read(extxyz(cell,mask,atoms),'--config',config,scene=True)
            expected=0
            # Source positions may be unwrapped: distance enumeration is invariant to wrapping.
            shifts=itertools.product(*(range(-4,5) if p else [0] for p in mask))
            shifts=list(shifts)
            for i,(_,a) in enumerate(atoms):
                for j,(_,b) in enumerate(atoms[i:],i):
                    for shift in shifts:
                        if i==j and not (shift>(0,0,0)): continue
                        delta=[b[d]-a[d]+sum(shift[k]*cell[k][d] for k in range(3)) for d in range(3)]
                        distance=math.sqrt(sum(x*x for x in delta))
                        if 0<distance<=cutoff: expected+=1
            self.assertEqual(scene['base_bond_count'],expected,mask)

    def test_tilted_lower_dimensional_cells_and_changing_pbc(self):
        wire=extxyz([[2,2,2],[0,0,0],[0,0,0]],[True,False,False],[('H',[.1,.1,.1]),('H',[1.9,1.9,1.9])])
        scene=self.read(wire,scene=True)
        self.assertEqual(scene['base_bond_count'],1)
        for bond in [p for p in scene['primitives'] if p['kind']=='bond']:
            self.assertEqual(bond['image'][1:],[0,0])
            self.assertAlmostEqual(bond['distance'],math.sqrt(.12))
        molecule='1\nno cell\nHe -3 4 -5\n'
        data=self.read(wire+molecule)
        self.assertEqual(data['pbc'],[False]*3)
        self.assertEqual(data['cell_present'],[False]*3)
        self.assertEqual(self.read(wire+molecule,'--frame','0')['pbc'],[True,False,False])
        slab=extxyz([[2,0,1],[1,2,1],[0,0,0]],[True,True,False],[('H',[0,0,-3])])
        self.assertEqual(self.read(slab,'--view','top',scene=True)['instance_count'],1)

    def test_degenerate_cells_and_missing_periodic_vectors(self):
        for cell,mask in [([[1,0,0],[2,0,0],[0,0,0]],[True,True,False]),
                          ([[0,0,0]]*3,[True,False,False]),
                          ([[1,0,0],[0,1,0],[1,1,0]],[False]*3)]:
            self.read(extxyz(cell,mask,[('H',[0,0,0])]),ok=False)

    def test_gaussian_input_units_labels_freeze_and_translation_vectors(self):
        self.assertEqual(self.read(GJF)['format'],'gaussian-input')
        text=GJF.replace('HF/3-21G','HF/3-21G Units = ( Bohr )').replace('H .96','1 -1 .96').replace('O 0','O1 0')
        data=self.read(text)
        self.assertAlmostEqual(data['atoms'][1]['position_A'][0],.96*.529177210903)
        tv=GJF.rstrip()+'\nTV 3 0 0\nTV 1 3 0\n\n'
        data=self.read(tv)
        self.assertEqual(data['pbc'],[True,True,False])
        self.assertEqual(data['atom_count'],3)
        self.assertEqual(self.read(tv,'--format','gjf')['format'],'gaussian-input')

    def test_gaussian_input_rejects_unsupported_variants(self):
        for text in [GJF.replace('O 0 0 0','O'),GJF.replace('H .96 0 0','H 1 r'),
                     GJF.replace('O 0 0 0','Bq 0 0 0'),GJF+'--Link1--\n'+GJF,
                     GJF.replace('HF/3-21G','HF/3-21G Geom=AllCheck'),GJF.replace('HF/3-21G','HF/3-21G Units=Nanometers'), GJF.replace('HF/3-21G','HF/3-21G Units=BohrJunk')]:
            with self.subTest(text=text): self.read(text,ok=False)

    def test_gaussian_output_frame_family_selection(self):
        rows=[(8,0,0,0),(1,.96,0,0),(1,-.24,.93,0)]
        text='Entering Gaussian System\n'+orientation('Input',rows)+orientation('Standard',rows)+orientation('Input',rows)+orientation('Standard',[(2,4,5,6)])
        data=self.read(text)
        self.assertEqual((data['frame'],data['frame_count'],data['atom_count']),(1,2,1))
        self.assertEqual(self.read(text,'--frame','0')['atom_count'],3)
        self.assertEqual(self.read(orientation('Input',rows))['frame_count'],1)
        self.read(text+'Standard orientation:\n -----\n','--frame','0',ok=False)
        self.read(text.replace('Coordinates (Angstroms)','Coordinates (Bohr)'),ok=False)

    def test_ams_units_fractional_and_atom_annotations(self):
        text=AMS.replace('Atoms','Atoms [Bohr]').replace('O 0 0 0','O 0 0 0 region=water')
        self.assertAlmostEqual(self.read(text)['atoms'][1]['position_A'][0],.96*.529177210903)
        text='system\n atoms\n H .5 0 7\n end\n lattice [Bohr]\n 4 0 0\n end\n FractionalCoords True\nend\n'
        data=self.read('# AMS wire\n'+text)
        self.assertEqual(data['pbc'],[True,False,False])
        self.assertAlmostEqual(data['atoms'][0]['position_A'][0],2*.529177210903)
        self.assertEqual(data['atoms'][0]['position_A'][2],7)
        slab='System\nAtoms\nH .5 .5 -3\nEnd\nLattice\n2 0 0\n1 2 0\nEnd\nFractionalCoords True\nEnd\n'
        self.assertEqual(self.read(slab)['atoms'][0]['position_A'],[1.5,1,-3])

    def test_ams_rejects_unsupported_or_incomplete_systems(self):
        for text in [AMS.replace('System','System other'), AMS+AMS, AMS.replace(' Atoms',' GeometryFile file.xyz\n Atoms'),
                     AMS.replace('Atoms','Atoms [nm]'), AMS.replace('End\nEngine','Engine'),
                     AMS.replace(' Atoms',' FractionalCoords True\n Atoms')]:
            with self.subTest(text=text): self.read(text,ok=False)

    def test_detection_preserves_poscar_title_freedom(self):
        poscar=(ROOT/'tests/fixtures/POSCAR.skew').read_text()
        for title in ['# Gaussian-like','System','42','<xml-like>']:
            self.assertEqual(self.read(title+'\n'+poscar.split('\n',1)[1])['format'],'poscar')

    def test_worker_reports_and_cache(self):
        self.path.write_text(WATER)
        job={'input':str(self.path),'output':str(self.root/'out.png'),'format':'xyz','no_user_config':True,
             'config':{'image':{'width':32,'height':32}}}
        requests=[{'id':str(i),'op':'render','job':job} for i in range(2)]
        p=subprocess.run([EXE,'--batch'],input=''.join(json.dumps(r)+'\n' for r in requests),text=True,capture_output=True,timeout=20)
        self.assertEqual(p.returncode,0,p.stderr)
        results=[json.loads(line) for line in p.stdout.splitlines()]
        self.assertTrue(all(r['ok'] for r in results),results)
        self.assertEqual(results[0]['result']['input']['pbc'],[False]*3)
        self.assertTrue(results[1]['result']['cache']['source_hit'])
        self.assertTrue(results[1]['result']['cache']['scene_hit'])
        schema=json.loads(subprocess.check_output([EXE,'--schema','job']))
        for name in ['xyz','extxyz','gaussian-input','gaussian-output','ams']:
            self.assertIn(name,schema['properties']['format']['enum'])


if __name__=='__main__': unittest.main(verbosity=2)
