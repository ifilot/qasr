# SPDX-License-Identifier: GPL-3.0-only
"""PLY interoperability, dimensional conversion, visibility and cache checks."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]
BOHR = 0.529177210903


def ply(path, binary=True, vertices=None, faces=None, normals=True, normal_values=None):
    vertices = vertices or [(-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)]
    faces = faces or [(0, 1, 2), (0, 2, 3)]
    header = ['ply', f'format {"binary_little_endian" if binary else "ascii"} 1.0',
              f'element vertex {len(vertices)}']
    header += [f'property float {p}' for p in (['x','y','z','nx','ny','nz'] if normals else ['x','y','z'])]
    header += [f'element face {len(faces)}', 'property list uchar uint vertex_indices', 'end_header']
    data = ('\n'.join(header)+'\n').encode()
    for index,v in enumerate(vertices):
        normal = normal_values[index] if normal_values is not None else (0,0,1)
        values = (*v, *normal) if normals else v
        data += struct.pack('<'+'f'*len(values), *values) if binary else (' '.join(map(str, values))+'\n').encode()
    for face in faces:
        data += struct.pack('<BIII', 3, *face) if binary else ('3 '+' '.join(map(str,face))+'\n').encode()
    path.write_bytes(data)


class Orbitals(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.mesh = self.dir/'surface.ply'
        ply(self.mesh)
        self.source = self.dir/'atoms.xyz'
        self.source.write_text('1\nanchor\nH 0 0 0\n')
        self.output = self.dir/'result.png'
        self.settings = self.dir/'settings.json'
        self.config = {'image': {'width':128,'height':128,'aa':'2x','style':'illustrated'},
                       'camera': {'view':[0,0,1],'up':[0,1,0]},
                       'overlays': {'cell':False},
                       'elements': {'H': {'radius_A':.2,'color':'#ff0000'}},
                       'orbitals': [{'file':'surface.ply','units':'angstrom','color':'#40a0e0'}]}

    def run_cli(self, *args, code=0):
        self.settings.write_text(json.dumps(self.config))
        p = subprocess.run([EXE,self.source,'--no-user-config','--config',self.settings,*map(str,args)],
                           capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,code,p.stderr)
        return p

    def scene(self):
        return json.loads(self.run_cli('--inspect-scene').stdout)

    def render(self, threads=1):
        self.run_cli('-o',self.output,'--threads',threads)
        return decode_png(self.output)

    def test_binary_ascii_identical_and_no_triangle_seam(self):
        binary = self.render()
        ply(self.mesh, binary=False)
        self.assertEqual(binary,self.render())
        width,height,pixels = binary
        center = pixels[4*((height//2)*width+width//2):][:4]
        self.assertGreater(center[2],center[0])
        self.assertNotEqual(center[:3],b'\x00\x00\x00')
        self.assertEqual(self.scene()['orbital_triangles'],2)
        self.assertEqual(self.render(1),self.render(4))

    def test_contours_ignore_normal_noise_and_preserve_internal_diagonal(self):
        # A flat patch has the same silhouette even if its supplied lighting
        # normals vary strongly. Normal derivatives must never create ink inside.
        self.config['image']['outline_width']=2
        for aa in ['native','2x','4x']:
            self.config['image']['aa']=aa
            ply(self.mesh)
            uniform=self.render()
            ply(self.mesh,normal_values=[(-.9,0,.01),(.9,0,.01),(0,.9,1),(0,-.9,1)])
            noisy=self.render()
            def ink(image):
                return {i//4 for i in range(0,len(image[2]),4) if image[2][i:i+3]==b'\0\0\0'}
            self.assertTrue(ink(uniform))
            self.assertEqual(ink(uniform),ink(noisy))
            width,height,pixels=noisy
            for y in range(height//3,2*height//3):
                for x in range(width//3,2*width//3):
                    self.assertNotEqual(pixels[4*(y*width+x):][:3],b'\0\0\0')

    def test_same_phase_depth_step_and_steep_continuous_slope(self):
        # Both patches belong to one PLY layer. A real gap in depth needs a
        # contour, while a steep but continuous plane must remain free of ink.
        self.config['elements']['H']['color']='#ff000000'
        self.config['image']['outline_width']=2
        faces=[(0,1,2),(0,2,3),(4,5,6),(4,6,7)]
        for aa in ['native','2x','4x']:
            self.config['image']['aa']=aa
            ply(self.mesh,vertices=[(-1,-1,.3),(0,-1,.3),(0,1,.3),(-1,1,.3),
                                    (0,-1,-.3),(1,-1,-.3),(1,1,-.3),(0,1,-.3)],faces=faces)
            stepped=self.render();w,h,pixels=stepped;i=4*((h//2)*w+w//2)
            self.assertEqual(pixels[i:i+3],b'\0\0\0')
            self.assertEqual(stepped,self.render(4))
            ply(self.mesh,vertices=[(-1,-1,-8),(0,-1,0),(0,1,0),(-1,1,-8),
                                    (0,-1,0),(1,-1,8),(1,1,8),(0,1,0)],faces=faces)
            continuous=self.render()
            self.assertNotEqual(continuous[2][i:i+3],b'\0\0\0')
            for y in range(h//3,2*h//3):
                for x in range(w//3,2*w//3):
                    self.assertNotEqual(continuous[2][4*(y*w+x):][:3],b'\0\0\0')

    def test_phase_boundary_contour_and_atom_occlusion(self):
        other=self.dir/'other.ply'
        ply(self.mesh,vertices=[(-1,-1,1),(0,-1,1),(0,1,1),(-1,1,1)])
        ply(other,vertices=[(0,-1,1),(1,-1,1),(1,1,1),(0,1,1)])
        self.config['orbitals'].append({'file':'other.ply','units':'angstrom','color':'#ed9163'})
        self.config['image']['outline_width']=2
        front=self.render();w,h,pixels=front;i=4*((h//2)*w+w//2)
        self.assertEqual(pixels[i:i+3],b'\0\0\0')
        self.config['image']['outline_width']=0
        no_line=self.render()
        self.assertNotEqual(no_line[2][i:i+3],b'\0\0\0')
        self.config['image']['outline_width']=2
        for layer in self.config['orbitals']:layer['origin']=[0,0,-2]
        behind=self.render()
        self.assertGreater(behind[2][i],behind[2][i+2])

    def test_bohr_origin_conversion_and_normals(self):
        self.config['orbitals'][0].update(units='bohr',origin=[3,4,5])
        triangle = self.scene()['face_triangles'][0]
        self.assertEqual(triangle['normals'],[[0,0,1]]*3)
        for actual,raw in zip(triangle['vertices'],[[-1,-1,1],[1,-1,1],[1,1,1]]):
            for d in range(3):
                self.assertAlmostEqual(actual[d],(raw[d]+[3,4,5][d])*BOHR,places=12)
        self.config['orbitals'][0]['units']='angstrom'
        for v in self.scene()['face_triangles'][0]['vertices']:
            self.assertIn(v[0],[2,4])
            self.assertEqual(v[2],6)

    def test_normals_optional_and_depth_with_atoms(self):
        ply(self.mesh,normals=False)
        front=self.render()
        self.config['orbitals'][0]['origin']=[0,0,-2]
        back=self.render()
        w,h,_=front;i=4*((h//2)*w+w//2)
        self.assertGreater(front[2][i+2],front[2][i])
        self.assertGreater(back[2][i],back[2][i+2])

    def test_invalid_input_and_preserve_destination(self):
        original=copy.deepcopy(self.config)
        for edit in [{'units':'nm'}, {'units':None}, {'origin':[0,0]}, {'color':'#ff000080'},
                     {'unknown':True}, {'file':'missing.ply'}]:
            with self.subTest(edit=edit):
                self.config=copy.deepcopy(original);self.config['orbitals'][0].update(edit)
                self.output.write_bytes(b'keep')
                p=self.run_cli('-o',self.output,code=7 if 'file' in edit else 2)
                self.assertEqual(self.output.read_bytes(),b'keep')
        self.config=original
        data=self.mesh.read_bytes()
        for broken in [data[:-1],data.replace(b'uint vertex_indices',b'float vertex_indices'),data+b'x']:
            self.mesh.write_bytes(broken)
            self.run_cli('--validate-config',code=2)
        ply(self.mesh,faces=[(0,1,99)])
        self.run_cli('--validate-config',code=2)
        self.mesh.write_text('ply\nformat ascii 1.0\nelement vertex 999999999999\nend_header\n')
        self.run_cli('--validate-config',code=2)

    def test_dump_and_cairo_rejection(self):
        dumped=json.loads(self.run_cli('--dump-config').stdout)['config']
        self.assertTrue(Path(dumped['orbitals'][0]['file']).is_absolute())
        expected=self.render();self.config=dumped
        self.assertEqual(expected,self.render())
        self.config['image']['backend']='cairo'
        self.run_cli('--validate-config',code=3)

    def test_worker_mesh_changes_invalidate_cache(self):
        job={'input':str(self.source),'output':str(self.output),
             'config':copy.deepcopy(self.config)}
        job['config']['orbitals'][0]['file']=str(self.mesh)
        p=subprocess.Popen([EXE,'--batch'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            results=[]
            for i in range(4):
                if i==2:ply(self.mesh,vertices=[(-2,-1,1),(2,-1,1),(2,1,1),(-2,1,1)])
                p.stdin.write(json.dumps({'id':str(i),'op':'render','job':job})+'\n');p.stdin.flush()
                reply=json.loads(p.stdout.readline());self.assertTrue(reply['ok'],reply)
                results.append(reply['result'])
            self.assertEqual([r['cache']['scene_hit'] for r in results],[False,True,False,True])
            self.assertNotEqual(results[0]['output']['sha256'],results[2]['output']['sha256'])
        finally:
            p.stdin.close();p.wait(timeout=30);p.stdout.close();p.stderr.close()

    def test_co_recessed_rim_and_convex_core_control(self):
        directory=ROOT/'examples/orbitals'
        for mo in [2,7]:
            p=subprocess.run([EXE,directory/'co.xyz','--no-user-config','--config',directory/f'recipes/co-{mo:02}.json',
                              '-o',self.output,'--threads','1'],capture_output=True,text=True,timeout=30)
            self.assertEqual(p.returncode,0,p.stderr)
            image=decode_png(self.output)
            if mo==2:
                # The smooth core must not acquire interior specks or a thicker
                # silhouette when self-occlusion contours are enabled.
                self.assertEqual(image,decode_png(ROOT/'tests/fixtures/orbitals/core-reference.png'))
            else:
                w,h,pixels=image
                # Fixed recipe: the orange recessed rim extends on both sides
                # of the blue phase. The previous silhouette-only pass lost it.
                for x in [210,270]:
                    self.assertTrue(any(pixels[4*(y*w+x):][:3]==b'\0\0\0' for y in range(183,189)))

    def test_real_pytessel_co_and_manifest_dimensions(self):
        directory=ROOT/'examples/orbitals'
        manifest=json.loads((directory/'manifest.json').read_text())
        self.assertEqual(len(manifest['orbitals']),10)
        self.assertEqual(sum(o['occupation'] for o in manifest['orbitals']),14)
        self.assertAlmostEqual(manifest['orbitals'][4]['energy_hartree'],manifest['orbitals'][5]['energy_hartree'],places=7)
        self.assertAlmostEqual(manifest['orbitals'][7]['energy_hartree'],manifest['orbitals'][8]['energy_hartree'],places=7)
        # Use the actual PyTessel binary writer output, not the synthetic helper.
        p=subprocess.run([EXE,directory/'co.xyz','--no-user-config','--config',directory/'recipes/co-07.json',
                          '--inspect-scene'],capture_output=True,text=True,timeout=30)
        self.assertEqual(p.returncode,0,p.stderr)
        scene=json.loads(p.stdout)
        self.assertGreater(scene['orbital_triangles'],1000)
        atoms=[p['a'] for p in scene['primitives'] if p['kind']=='sphere']
        self.assertAlmostEqual(abs(atoms[1][2]-atoms[0][2]),manifest['bond_length_A'],places=12)


if __name__=='__main__':
    unittest.main()
