# SPDX-License-Identifier: GPL-3.0-only
"""Endpoint membership and screen-sized dashed/outlined cell strokes."""
import copy
import itertools
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png

EXE=Path(sys.argv.pop(1)).resolve()
CAP=json.loads(subprocess.check_output([EXE,'--build-info']))
ROOT=Path(__file__).resolve().parents[1]


def poscar(basis,positions):
    return 'test\n1\n'+'\n'.join(' '.join(map(str,v)) for v in basis)+'\nH\n'+str(len(positions))+'\nDirect\n'+''.join(' '.join(map(str,p))+'\n' for p in positions)


class CellLineTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.source=self.root/'POSCAR';self.source.write_text(poscar([[8,0,0],[0,6,0],[0,0,10]],[[.5,.5,.5]]))
        self.config={'image':{'width':256,'height':224,'aa':'native','background':'#637991'},
                     'camera':{'view':[0,0,1],'up':[0,1,0]},
                     'elements':{'H':{'color':'#00000000','radius_A':.1}},
                     'overlays':{'cell':True,'cell_width':2,'cell_color':'#000000'}}
        self.output=self.root/'output.png'

    def invoke(self,*flags,config=None,code=0):
        settings=self.root/'config.json';settings.write_text(json.dumps(config or self.config))
        p=subprocess.run([EXE,self.source,'--no-user-config','--config',settings,*map(str,flags)],capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,code,p.stderr)
        return p

    def scene(self,*flags):return json.loads(self.invoke('--inspect-scene',*flags).stdout)
    def image(self,*flags):
        self.invoke('-o',self.output,*flags);return decode_png(self.output)

    def assert_endpoints(self,scene):
        atoms={(p['source'][0],tuple(p['origin'])):p for p in scene['primitives'] if p['kind']=='sphere'}
        for bond in [p for p in scene['primitives'] if p['kind']=='bond']:
            a=(bond['source'][0],tuple(bond['origin']))
            b=(bond['source'][1],tuple(x+y for x,y in zip(bond['origin'],bond['image'])))
            self.assertIn(a,atoms);self.assertIn(b,atoms)
            self.assertLess(math.dist(bond['a'],atoms[a]['a']),1e-9)
            self.assertLess(math.dist(bond['b'],atoms[b]['a']),1e-9)

    def test_default_omits_boundary_stubs_but_keeps_internal_connections(self):
        self.source.write_text(poscar([[4,0,0],[0,4,0],[0,0,4]],[[.025,.25,.25],[.975,.25,.25]]))
        self.config['bond_cutoffs_A']={'H-H':.5}
        scene=self.scene();self.assertEqual(scene['base_bond_count'],1);self.assertEqual(scene['bond_count'],0)
        repeated=self.scene('--repeat','2,1,1')
        self.assertEqual(repeated['bond_count'],1);self.assert_endpoints(repeated)

    def test_endpoint_rule_cannot_be_disabled(self):
        for value in ['clip','atoms']:
            self.invoke('--inspect','--bond-boundary',value,code=2)
            invalid=copy.deepcopy(self.config);invalid['geometry']={'bond_boundary':value}
            self.invoke('--inspect',config=invalid,code=2)
            request={'id':'obsolete','op':'render','job':{'input':str(self.source),'output':str(self.output),'config':invalid}}
            p=subprocess.run([EXE,'--batch'],input=json.dumps(request)+'\n',text=True,capture_output=True,check=True,timeout=30)
            self.assertFalse(json.loads(p.stdout)['ok'])
        self.assertNotIn('--bond-boundary',subprocess.check_output([EXE,'--help'],text=True))

    def test_xy_expansion_keeps_bonds_to_displayed_neighbor_atoms(self):
        self.source.write_text(poscar([[4,0,0],[0,4,0],[0,0,4]],[[.025,.25,.25],[.975,.25,.25]]))
        self.config['bond_cutoffs_A']={'H-H':.5}
        self.assertEqual(self.scene()['bond_count'],0)
        self.assertEqual(self.scene('--expand-xy','0,1')['bond_count'],0)
        for flags,count in [(['--expand-xy','1,0'],2),(['--expand-xy','1,1'],6)]:
            scene=self.scene(*flags);self.assert_endpoints(scene)
            self.assertEqual(scene['bond_count'],count)
            bonds=[p for p in scene['primitives'] if p['kind']=='bond']
            # The primary-cell atom connects to the included left image;
            # the right image connects back to the primary cell as well.
            self.assertTrue(any(p['origin']==[0,0,0] and p['image']==[-1,0,0] for p in bonds))
            self.assertTrue(any(p['origin']==[1,0,0] and p['image']==[-1,0,0] for p in bonds))

    def test_self_image_bonds_require_both_atom_instances(self):
        self.source.write_text(poscar([[1,0,0],[0,4,0],[0,0,4]],[[.4,.5,.5]]))
        self.assertEqual(self.scene()['bond_count'],0)
        repeated=self.scene('--repeat','3,1,1')
        self.assertEqual(repeated['bond_count'],2);self.assert_endpoints(repeated)

    def test_endpoint_rule_matches_finite_cartesian_pair_oracle(self):
        for basis in [[[2,0,0],[0,2,0],[0,0,3]],[[2,0,0],[1.1,1.8,0],[.2,-.1,3]]]:
            self.source.write_text(poscar(basis,[[0,0,0],[.3,.7,.4],[.99,.02,.8]]))
            self.config['bond_cutoffs_A']={'H-H':1.4}
            for flags in [[],['--repeat','2,2,2'],['--expand-xy','1,1']]:
                scene=self.scene(*flags);self.assert_endpoints(scene)
                atoms=[p for p in scene['primitives'] if p['kind']=='sphere']
                identity=lambda p:(p['source'][0],tuple(p['origin']))
                expected={frozenset([identity(a),identity(b)]) for a,b in itertools.combinations(atoms,2) if 0<math.dist(a['a'],b['a'])<=1.4+1e-12}
                actual={frozenset([(p['source'][0],tuple(p['origin'])),(p['source'][1],tuple(x+y for x,y in zip(p['origin'],p['image'])))]) for p in scene['primitives'] if p['kind']=='bond'}
                self.assertEqual(actual,expected)

    def test_centered_context_and_atom_colors_keep_matching_endpoints(self):
        self.source.write_text(poscar([[4,0,0],[1,4,0],[0,0,4]],[[.025,.25,.25],[.975,.25,.25]]))
        self.config.update(geometry={'bond_colors':'atoms'},bond_cutoffs_A={'H-H':.5})
        scene=self.scene('--expand-xy','1,1','--image-colors','greyscale')
        self.assert_endpoints(scene)
        atoms={(p['source'][0],tuple(p['origin'])):p for p in scene['primitives'] if p['kind']=='sphere'}
        for p in [p for p in scene['primitives'] if p['kind']=='bond']:
            self.assertEqual(p['color_split'],.5)
            self.assertEqual(p['rgba'],atoms[(p['source'][0],tuple(p['origin']))]['rgba'])
            self.assertEqual(p['rgba_b'],atoms[(p['source'][1],tuple(a+b for a,b in zip(p['origin'],p['image'])))]['rgba'])

    def edge_pixels(self,image,scene):
        camera=scene['camera'];scale=camera['scale']
        ax=(0-camera['center'][0])*scale+image[0]/2
        bx=(8-camera['center'][0])*scale+image[0]/2
        y=math.floor((0-camera['center'][1])*scale+image[1]/2)
        return [image[2][4*((image[1]-1-y)*image[0]+x):4*((image[1]-1-y)*image[0]+x)+4] for x in range(math.ceil(ax)+12,math.floor(bx)-12)]

    def test_dash_gaps_reveal_background_and_solid_is_continuous(self):
        solid=self.image();row=self.edge_pixels(solid,self.scene())
        self.assertTrue(row);self.assertTrue(all(p==bytes([0,0,0,255]) for p in row))
        self.config['overlays'].update(cell_style='dashed',cell_dash_length=8,cell_dash_gap=5)
        row=self.edge_pixels(self.image(),self.scene())
        self.assertIn(bytes([0,0,0,255]),row);self.assertIn(bytes([99,121,145,255]),row)

    def test_white_contour_and_transparent_background(self):
        for style in ['solid','dashed']:
            self.config['overlays'].update(cell_style=style,cell_outline_width=1.5)
            image=self.image();pixels=[image[2][i:i+4] for i in range(0,len(image[2]),4)]
            self.assertGreater(pixels.count(bytes([255,255,255,255])),30)
            self.assertGreater(pixels.count(bytes([0,0,0,255])),30)
        self.config['image']['background']='#ffffff00';self.config['overlays']['cell_color']='#00000080'
        image=self.image();pixels=[image[2][i:i+4] for i in range(0,len(image[2]),4)]
        self.assertIn(bytes([255,255,255,128]),pixels);self.assertIn(bytes([0,0,0,128]),pixels)

    def test_outline_is_not_clipped_at_zero_margin(self):
        self.config['camera']['margin']=0
        self.config['overlays'].update(cell_style='dashed',cell_outline_width=8,cell_width=5)
        w,h,pixels=self.image()
        border=[pixels[4*(y*w+x):4*(y*w+x)+4] for y in range(h) for x in range(w) if x in [0,w-1] or y in [0,h-1]]
        self.assertEqual(set(border),{bytes([99,121,145,255])})

    def test_cell_strokes_respect_depth_in_front_and_behind_atoms(self):
        self.config['elements']['H']={'color':'#ff0000','radius_A':1}
        self.config['overlays'].update(cell_outline_width=1.5)
        # The y=0 top edge lies at z=10. At z=10.5 the sphere's visible surface
        # covers it; at z=8.5 the cell's front edge passes across the sphere.
        for z,black in [(1.05,False),(.85,True)]:
            self.source.write_text(poscar([[8,0,0],[0,6,0],[0,0,10]],[[.5,0,z]]))
            # Nonperiodic z retains the intentionally external test coordinate.
            xyz=self.root/'depth.xyz'
            xyz.write_text(f'1\nLattice="8 0 0 0 6 0 0 0 10" pbc="F F F" Properties=species:S:1:pos:R:3\nH 4 0 {z*10}\n')
            self.source=xyz
            scene=self.scene();image=self.image();camera=scene['camera']
            x=math.floor((4-camera['center'][0])*camera['scale']+image[0]/2)
            y=image[1]-1-math.floor(-camera['center'][1]*camera['scale']+image[1]/2)
            pixel=image[2][4*(y*image[0]+x):4*(y*image[0]+x)+4]
            if black:self.assertEqual(pixel[:3],b'\0\0\0')
            else:self.assertGreater(pixel[0],0);self.assertEqual(pixel[1:3],b'\0\0')

    def test_settings_round_trip_and_invalid_values(self):
        self.config['overlays'].update(cell_style='dashed',cell_outline_width=1.5,cell_dash_length=12,cell_dash_gap=7)
        dumped=json.loads(self.invoke('--dump-config').stdout)['config'];a=self.image()
        b=self.invoke('-o',self.output,config=dumped);self.assertEqual(a,decode_png(self.output))
        self.assertNotIn('bond_boundary',dumped['geometry'])
        for flag,value in [('--cell-style','dots'),('--cell-color','black'),('--cell-outline-width','-1'),('--cell-outline-width','9'),('--cell-dash-length','0'),('--cell-dash-gap','.5'),('--cell-dash-gap','257'),('--bond-boundary','outside')]:
            self.invoke('--inspect',flag,value,code=2)
        for key,value in [('cell_style','dots'),('cell_outline_width',True),('cell_dash_length',0),('cell_dash_gap',300),('cell_color','white')]:
            invalid=copy.deepcopy(self.config);invalid['overlays'][key]=value
            self.invoke('--inspect',config=invalid,code=2)

    def test_nearly_end_on_contour_does_not_jump_in_front_of_atom(self):
        self.source=self.root/'axial.xyz'
        self.source.write_text('1\nLattice="8 0 0 0 6 0 0 0 10" pbc="F F F" Properties=species:S:1:pos:R:3\nH 0 0 11\n')
        self.config['camera']['view']=[1e-8,0,1]
        self.config['elements']['H']={'color':'#ff0000','radius_A':1}
        self.config['overlays'].update(cell_style='dashed',cell_outline_width=1.5)
        scene=self.scene();image=self.image();camera=scene['camera']
        # Probe the halo of an almost end-on z edge, behind the red sphere.
        world=[0,2/camera['scale'],11]
        screen=[(sum(v*a for v,a in zip(world,camera[axis]))-camera['center'][d])*camera['scale']+image[d]/2 for d,axis in enumerate(['right','up'])]
        x=math.floor(screen[0]);y=image[1]-1-math.floor(screen[1])
        pixel=image[2][4*(y*image[0]+x):4*(y*image[0]+x)+4]
        self.assertGreater(pixel[0],0);self.assertEqual(pixel[1:3],b'\0\0')

    def test_end_on_strokes_with_no_projected_dash_segments(self):
        self.source.write_text('2\nLattice="0 0 0 0 0 0 0 0 5" Properties=species:S:1:pos:R:3 pbc="F F T"\nH 0 0 1\nH 0 0 3\n')
        self.config['overlays'].update(cell_outline_width=2,cell_width=3)
        for aa in ['native','2x','4x']:
            solid=self.image('--aa',aa,'--cell-style','solid')
            dashed=self.image('--aa',aa,'--cell-style','dashed')
            self.assertEqual(solid,dashed)

    def test_thread_and_aa_determinism(self):
        if not CAP['openmp']:self.skipTest('OpenMP build required')
        self.config['overlays'].update(cell_style='dashed',cell_outline_width=1.5)
        for aa in ['native','2x','4x','adaptive']:
            self.config['image']['aa']=aa
            self.assertEqual(self.image('--threads','1'),self.image('--threads','8'))
        adaptive=self.image('--aa','adaptive');self.assertEqual(adaptive,self.image('--aa','2x'))

    def test_worker_cache_invalidation(self):
        self.source.write_text(poscar([[4,0,0],[0,4,0],[0,0,4]],[[.025,.25,.25],[.975,.25,.25]]))
        base=copy.deepcopy(self.config);base['bond_cutoffs_A']={'H-H':.5}
        configurations=[copy.deepcopy(base) for _ in range(6)]
        for i in range(2,6):configurations[i]['overlays'].update(cell_style='dashed',cell_outline_width=1)
        for i in range(3,6):configurations[i]['overlays']['cell_color']='#ff0000'
        for i in range(4,6):configurations[i]['geometry']={'repeat':[2,1,1]}
        requests=[{'id':str(i),'op':'render','job':{'input':str(self.source),'output':str(self.output),'threads':1,'config':c}} for i,c in enumerate(configurations)]
        p=subprocess.run([EXE,'--batch'],input=''.join(json.dumps(r)+'\n' for r in requests),text=True,capture_output=True,check=True,timeout=30)
        replies=[json.loads(line) for line in p.stdout.splitlines()]
        self.assertTrue(all(r['ok'] for r in replies),replies)
        self.assertEqual([r['result']['cache']['scene_hit'] for r in replies],[False,True,True,False,False,True])
        self.assertEqual([r['result']['counts']['bonds'] for r in replies],[0,0,0,0,1,1])

    def test_cairo_supports_dashes_and_white_outline(self):
        if not CAP['cairo']:self.skipTest('Cairo build required')
        self.config['image']['backend']='cairo';self.config['overlays'].update(cell_style='dashed',cell_outline_width=1.5)
        image=self.image();pixels=[image[2][i:i+4] for i in range(0,len(image[2]),4)]
        self.assertGreater(pixels.count(bytes([255,255,255,255])),30)
        self.assertGreater(pixels.count(bytes([0,0,0,255])),30)


if __name__=='__main__':unittest.main()
