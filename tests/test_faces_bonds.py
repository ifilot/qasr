# SPDX-License-Identifier: GPL-3.0-only
"""Independent polygon coverage/depth/alpha and continuous half-bond checks."""
import copy
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from png_support import decode_png

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
CAP = json.loads(subprocess.check_output([EXE, '--build-info']))


def square(z=0, color='#ff000080'):
    return {'vertices': [[-1, -1, z], [1, -1, z], [1, 1, z], [-1, 1, z]], 'color': color}


class FacesBonds(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'atoms.xyz'
        self.source.write_text('1\nanchor\nH 0 0 0\n')
        self.config = {'image': {'width': 128, 'height': 128, 'aa': 'native'},
                       'camera': {'view': [0, 0, 1], 'up': [0, 1, 0]},
                       'overlays': {'cell': False},
                       'elements': {'H': {'radius_A': .1, 'color': '#00000000'}}}
        self.output = self.root / 'output.png'

    def invoke(self, config=None, *flags, code=0):
        settings = self.root / 'config.json'
        settings.write_text(json.dumps(config or self.config))
        proc = subprocess.run([EXE, self.source, '--no-user-config', '--config', settings,
                               *map(str, flags)], capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, code, proc.stderr)
        return proc

    def scene(self, config=None):
        return json.loads(self.invoke(config, '--inspect-scene').stdout)

    def render(self, config=None, threads=1):
        self.invoke(config, '-o', self.output, '--threads', threads)
        return decode_png(self.output)

    def sample(self, image, scene, x, y):
        camera = scene['camera']
        px = math.floor((x - camera['center'][0]) * camera['scale'] + image[0] / 2)
        py = image[1] - 1 - math.floor((y - camera['center'][1]) * camera['scale'] + image[1] / 2)
        return list(image[2][4*(py*image[0]+px):4*(py*image[0]+px)+4])

    def test_concave_coverage_area_and_winding(self):
        face = {'vertices': [[0,0,0],[2,0,0],[2,1,0],[1,1,0],[1,2,0],[0,2,0]], 'color': '#ff000080'}
        self.config['faces'] = [face]
        scene = self.scene()
        self.assertEqual((scene['face_count'],len(scene['face_triangles'])),(1,4))
        area = 0
        for triangle in scene['face_triangles']:
            a,b,c = triangle['vertices']
            area += abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2
        self.assertAlmostEqual(area,3)
        image = self.render()
        self.assertEqual(self.sample(image,scene,.5,1.5),[255,127,127,255])
        self.assertEqual(self.sample(image,scene,1.5,1.5),[255,255,255,255])
        face['vertices'].reverse()
        self.assertEqual(self.render()[2],image[2])

    def test_internal_diagonal_has_no_alpha_seam(self):
        self.config['faces'] = [square()]
        image = self.render()
        scene = self.scene()
        for x,y in [(-.5,-.5),(0,0),(.5,.5),(-.5,.5),(.5,-.5)]:
            self.assertEqual(self.sample(image,scene,x,y),[255,127,127,255])
        self.config['image']['background']='#ffffff00'
        image = self.render()
        self.assertEqual(self.sample(image,scene,0,0),[255,0,0,128])

    def test_transparent_layers_and_order(self):
        self.config['faces'] = [square(-.2,'#ff000080'), square(.2,'#0000ff80')]
        scene = self.scene();image=self.render()
        self.assertEqual(self.sample(image,scene,.2,.3),[127,63,191,255])
        self.config['faces'].reverse()
        self.assertEqual(self.render()[2],image[2])

    def test_empty_and_opaque_face_samples_preserve_background(self):
        self.config['faces']=[square(color='#ed6e14')]
        for aa in ['native','2x','4x','adaptive']:
            self.config['image']['aa']=aa
            for alpha in [0,1,127,255]:
                self.config['image']['background']=f'#476c91{alpha:02x}'
                scene=self.scene();image=self.render()
                self.assertEqual(list(image[2][:4]),[71,108,145,alpha] if alpha else [0,0,0,0])
                self.assertEqual(self.sample(image,scene,0,0),[237,110,20,255])

    def test_opaque_front_keeps_overlap_limit_and_failed_output(self):
        self.config['faces']=[square(-i*.01,'#ff000080') for i in range(65)]+[square(1,'#0000ff')]
        self.output.write_bytes(b'keep')
        self.invoke(self.config,'-o',self.output,code=6)
        self.assertEqual(self.output.read_bytes(),b'keep')

    def test_intersecting_faces_use_surface_depth(self):
        a = {'vertices':[[-1,-1,-1],[1,-1,1],[1,1,1],[-1,1,-1]],'color':'#ff0000'}
        b = {'vertices':[[-1,-1,1],[1,-1,-1],[1,1,-1],[-1,1,1]],'color':'#0000ff'}
        for aa in ['native','2x','4x','adaptive']:
            self.config['image']['aa']=aa;self.config['faces']=[a,b]
            scene=self.scene();image=self.render()
            self.assertEqual(self.sample(image,scene,-.5,.2),[0,0,255,255])
            self.assertEqual(self.sample(image,scene,.5,.2),[255,0,0,255])
            self.config['faces']=[b,a]
            # Pixels on exactly equal depths intentionally follow input order.
            other=self.render()
            self.assertEqual(self.sample(other,scene,-.5,.2),[0,0,255,255])
            self.assertEqual(self.sample(other,scene,.5,.2),[255,0,0,255])

    def test_atom_face_occlusion_and_partial_alpha(self):
        self.config['elements']['H']={'radius_A':.5,'color':'#0000ff'}
        self.config['faces']=[square(1,'#00ff0000')]
        scene=self.scene();base=self.render();blue=self.sample(base,scene,0,0)
        self.config['faces'][0]['color']='#00ff0080'
        front=self.sample(self.render(),scene,0,0)
        self.assertEqual(front[1],128)
        self.assertLessEqual(abs(front[2]-blue[2]*127/255),1)
        self.config['faces']=[square(-1,'#00ff0080')]
        self.assertEqual(self.sample(self.render(),scene,0,0),blue)
        self.config['elements']['H']['color']='#0000ff80'
        back=self.sample(self.render(),scene,0,0)
        # A translucent atom must reveal the face behind it, rather than only white.
        self.assertIn(back[1],[127,128]);self.assertLess(back[0],100)

    def test_outlines_only_follow_polygon_boundary(self):
        self.config['faces']=[dict(square(color='#ff0000'),outline_width=2,outline_color='#000000')]
        scene=self.scene();image=self.render()
        self.assertEqual(self.sample(image,scene,0,0),[255,0,0,255])
        self.assertEqual(self.sample(image,scene,.5,.5),[255,0,0,255])
        self.assertTrue(any(image[2][i:i+3]==b'\0\0\0' for i in range(0,len(image[2]),4)))

    def test_invalid_faces_and_existing_destination(self):
        cases = [
            {'vertices':[[0,0,0],[1,0,0],[2,0,0]]},
            {'vertices':[[0,0,0],[1,1,0],[0,1,0],[1,0,0]]},
            {'vertices':[[0,0,0],[1,0,0],[0,0,0],[0,1,0]]},
            {'vertices':[[0,0,0],[1,0,0]]},
            {'atom_indices':[0,1,2]},
            dict(square(),atom_indices=[0,1,2,3]),
            dict(square(),color='red'), dict(square(),outline_width=-1),
            dict(square(),repeat=1),dict(square(),unknown=True),
            dict(square(),cell_offsets=[[0,0,0]]*4),
        ]
        for face in cases:
            with self.subTest(face=face):
                self.config['faces']=[face];self.output.write_bytes(b'keep')
                self.invoke(None,'-o',self.output,code=2)
                self.assertEqual(self.output.read_bytes(),b'keep')
                self.invoke(None,'--validate-config',code=2)

    def test_sidecar_relative_path_dump_and_schema(self):
        side=self.root/'polygons.json';side.write_text(json.dumps({'schema_version':1,'faces':[square()]}))
        self.config['faces_file']='polygons.json'
        scene=self.scene();self.assertEqual(scene['face_count'],1)
        dumped=json.loads(self.invoke(None,'--dump-config').stdout)['config']
        self.assertIn('faces',dumped);self.assertNotIn('faces_file',dumped)
        self.assertEqual(self.scene(dumped)['face_triangles'],scene['face_triangles'])
        self.config.pop('faces_file')
        self.assertEqual(json.loads(self.invoke(None,'--faces',side,'--inspect-scene').stdout)['face_count'],1)
        side.write_text('{"faces":[],"faces":[]}')
        self.invoke(None,'--faces',side,'--inspect-scene',code=2)
        self.assertEqual(json.loads(subprocess.check_output([EXE,'--schema','faces'])),json.loads((ROOT/'schemas/faces.schema.json').read_text()))

    def test_periodic_vertices_and_atom_indices(self):
        self.source.write_text('4\nLattice="4 0 0 0 4 0 0 0 4" pbc="T T T" Properties=species:S:1:pos:R:3\nSi 3 1 0\nSi 1 1 0\nSi 1 3 0\nSi 3 3 0\n')
        face={'atom_indices':[0,1,2,3],'cell_offsets':[[0,0,0],[1,0,0],[1,0,0],[0,0,0]]}
        self.config['faces']=[face];self.config['geometry']={'repeat':[2,1,1]}
        scene=self.scene();self.assertEqual(scene['face_count'],2)
        self.assertEqual({t['origin'][0] for t in scene['face_triangles']},{0,1})
        self.assertEqual(sorted({v[0] for t in scene['face_triangles'] for v in t['vertices']}),[3,5,7,9])
        face['repeat']=False;self.assertEqual(self.scene()['face_count'],1)
        face={'vertices':[[.1,.1,0],[.5,.1,0],[.5,.5,0]],'coordinates':'fractional'}
        self.config['faces']=[face]
        self.assertEqual(self.scene()['face_triangles'][0]['vertices'][0],[.4,.4,0])

    def test_layer_and_work_limits(self):
        self.config['image'].update(width=64,height=64)
        self.config['faces']=[square(i/1000) for i in range(64)]
        self.render()
        self.config['faces']=[square(i/1000) for i in range(65)]
        self.output.write_bytes(b'keep')
        self.invoke(None,'-o',self.output,code=6)
        self.assertEqual(self.output.read_bytes(),b'keep')
        self.config['faces']=[square()]*4096
        self.config['image'].update(width=256,height=256)
        self.invoke(None,'-o',self.output,code=6)

    def test_faces_and_half_bonds_thread_determinism(self):
        if not CAP['openmp']:self.skipTest('OpenMP required')
        self.source.write_text('2\nbond\nH -.6 0 0\nO .6 0 0\n')
        self.config['elements']={'H':{'color':'#ed243c','radius_A':.2},'O':{'color':'#247aed','radius_A':.2}}
        self.config['geometry']={'bond_colors':'atoms'}
        self.config['faces']=[square(-.2),square(.1,'#edb84470')]
        self.config['image'].update(width=512,height=512)
        for aa in ['2x','4x','adaptive']:
            self.config['image']['aa']=aa
            self.assertEqual(self.render(threads=1),self.render(threads=8))

    def test_continuous_half_bonds_and_thickness(self):
        self.source.write_text('2\nbond\nH -1 0 0\nO 1 0 0\n')
        self.config.update(elements={'H':{'color':'#ff0000','radius_A':.25},'O':{'color':'#0000ff','radius_A':.25}},
                           geometry={'bond_colors':'atoms','bond_radius_A':.1},bond_cutoffs_A={'H-O':2.1})
        scene=self.scene();image=self.render()
        bond=next(p for p in scene['primitives'] if p['kind']=='bond')
        self.assertEqual((bond['rgba'],bond['rgba_b'],bond['color_split']),([255,0,0,255],[0,0,255,255],.5))
        red=self.sample(image,scene,-.05,0);blue=self.sample(image,scene,.05,0)
        self.assertGreater(red[0],0);self.assertEqual(red[1:3],[0,0]);self.assertEqual(blue[:2],[0,0]);self.assertEqual(red[0],blue[2])
        self.config['geometry']['bond_radius_A']=.2
        thick=self.render();self.assertEqual(self.scene()['camera'],scene['camera'])
        def column(image):
            return sum(image[2][4*(y*image[0]+image[0]//2):4*(y*image[0]+image[0]//2)+3]!=b'\xff\xff\xff' for y in range(image[1]))
        self.assertGreater(column(thick),column(image)*1.7)
        self.config['image']['outline_width']=0
        for style in ['shaded','illustrated','ortep','balsac']:
            self.config['image']['style']=style
            image=self.render();red=self.sample(image,scene,-.3,0);blue=self.sample(image,scene,.3,0)
            self.assertGreater(red[0],red[2],style);self.assertGreater(blue[2],blue[0],style)

    def test_endpoint_overrides_in_displayed_periodic_context(self):
        self.source.write_text('2\nLattice="4 0 0 0 4 0 0 0 4" pbc="T T T" Properties=species:S:1:pos:R:3\nSi .1 1 1\nO 3.9 1 1\n')
        self.config.update(geometry={'bond_colors':'atoms','repeat':[2,1,1],'image_colors':'muted'},
                           bond_cutoffs_A={'Si-O':.5},atom_overrides=[{'select':{'source_indices':[0]},'color':'#ee4400'},
                           {'select':{'source_indices':[1],'cell':[1,0,0]},'color':'#6600ff'}])
        scene=self.scene();spheres={(p['source'][0],tuple(p['origin'])):p for p in scene['primitives'] if p['kind']=='sphere'}
        for bond in [p for p in scene['primitives'] if p['kind']=='bond']:
            first=(bond['source'][0],tuple(bond['origin']))
            second=(bond['source'][1],tuple(a+b for a,b in zip(bond['origin'],bond['image'])))
            if first in spheres:self.assertEqual(bond['rgba'],spheres[first]['rgba'])
            if second in spheres:self.assertEqual(bond['rgba_b'],spheres[second]['rgba'])
            for d in range(3):
                source_a=[.1,1,1][d]+4*bond['origin'][d]
                source_b=[3.9,1,1][d]+4*(bond['origin'][d]+bond['image'][d])
                self.assertAlmostEqual(bond['a'][d]+bond['color_split']*(bond['b'][d]-bond['a'][d]),(source_a+source_b)/2)

    def test_transparent_bond_endpoint_keeps_other_half_visible(self):
        self.source.write_text('2\nbond\nH -1 0 0\nO 1 0 0\n')
        self.config.update(elements={'H':{'color':'#ff000000','radius_A':.25},'O':{'color':'#0000ff','radius_A':.25}},
                           geometry={'bond_colors':'atoms','bond_radius_A':.1},bond_cutoffs_A={'H-O':2.1})
        scene=self.scene();image=self.render()
        self.assertEqual(self.sample(image,scene,-.5,0),[255,255,255,255])
        right=self.sample(image,scene,.5,0)
        self.assertEqual(right[:2],[0,0]);self.assertGreater(right[2],0)
        self.config['elements']['H']['color']='#ff0000';self.config['elements']['O']['color']='#0000ff00'
        image=self.render()
        self.assertEqual(self.sample(image,scene,.5,0),[255,255,255,255])
        self.assertGreater(self.sample(image,scene,-.5,0)[0],0)

    def test_opaque_faces_occlude_label_anchors(self):
        self.config['elements']['H']={'radius_A':.5,'color':'#0000ff'}
        self.config['overlays']['labels']='element'
        self.config['faces']=[square(1,'#00ff00')]
        proc=self.invoke(None,'-o',self.output,'--benchmark-stages')
        self.assertIn('overlay_labels=0',proc.stderr)
        self.config['faces'][0]['color']='#00ff0080'
        proc=self.invoke(None,'-o',self.output,'--benchmark-stages')
        self.assertIn('overlay_labels=1',proc.stderr)

    def test_worker_file_changes_invalidate_scene_and_cli_match(self):
        side=self.root/'faces.json';side.write_text(json.dumps({'faces':[square()]}))
        job={'input':str(self.source),'output':str(self.output),'threads':1,'config':dict(self.config,faces_file=str(side))}
        p=subprocess.Popen([EXE,'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            results=[]
            for i in range(4):
                if i==2:side.write_text(json.dumps({'faces':[square(color='#0000ff80')]}))
                p.stdin.write(json.dumps({'id':str(i),'op':'render','job':job})+'\n');p.stdin.flush()
                reply=json.loads(p.stdout.readline());self.assertTrue(reply['ok'],reply);results.append(reply['result'])
            self.assertEqual([r['cache']['scene_hit'] for r in results],[False,True,False,True])
            self.assertTrue(all(r['cache']['source_hit'] for r in results[1:]))
            self.assertNotEqual(results[0]['output']['sha256'],results[2]['output']['sha256'])
            self.config['faces_file']=str(side)
            self.render();import hashlib
            self.assertEqual(hashlib.sha256(self.output.read_bytes()).hexdigest(),results[-1]['output']['sha256'])
        finally:
            p.stdin.close();p.wait(timeout=30);p.stdout.close();p.stderr.close()

    def test_cairo_rejects_unsupported_features(self):
        self.config['image']['backend']='cairo';self.config['faces']=[square()]
        self.invoke(None,'--validate-config',code=3)
        self.config.pop('faces');self.config['geometry']={'bond_colors':'atoms'}
        self.invoke(None,'--validate-config',code=3)


if __name__ == '__main__':
    unittest.main()
