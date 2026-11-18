# SPDX-License-Identifier: GPL-3.0-only
"""Point-group symmetry elements: resolution, verification, geometry and labels."""
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
MOLECULES = ROOT / 'examples/symmetry'
AXIS_RGB = [122, 47, 143]


def atoms(path):
    lines = Path(path).read_text().splitlines()[2:]
    return [(w[0], [float(v) for v in w[1:4]]) for w in (line.split() for line in lines) if w]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return [x-y for x, y in zip(a, b)]


class Symmetry(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.output = self.root / 'output.png'
        self.config = {'image': {'width': 160, 'height': 160, 'aa': 'native'},
                       'camera': {'view': [3, -5, 2]}, 'overlays': {'cell': False}}

    def invoke(self, molecule, symmetry, *flags, code=0, config=None):
        settings = dict(config or self.config)
        if symmetry is not None:
            settings['symmetry'] = symmetry
        path = self.root / 'config.json'
        path.write_text(json.dumps(settings))
        proc = subprocess.run([EXE, MOLECULES / molecule, '--no-user-config', '--json-errors', '--config', path,
                               *map(str, flags)], capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, code, proc.stderr)
        return proc

    def scene(self, molecule, symmetry):
        return json.loads(self.invoke(molecule, symmetry, '--inspect-scene').stdout)

    def error(self, molecule, symmetry, code=2):
        return json.loads(self.invoke(molecule, symmetry, '--inspect-scene', code=code).stderr)['error']

    def test_rotation_axis_passes_through_centroid_and_clears_atoms(self):
        scene = self.scene('nh3.xyz', {'operation': 'C3', 'axis': [0, 0, 2]})
        rods = [p for p in scene['primitives'] if p['kind'] == 'symmetry_axis']
        self.assertEqual(len(rods), 1)
        self.assertEqual(scene['face_count'], 0)
        mark = scene['symmetry']
        positions = [p for _, p in atoms(MOLECULES / 'nh3.xyz')]
        centroid = [sum(p[d] for p in positions)/len(positions) for d in range(3)]
        for d in range(3):
            self.assertAlmostEqual(mark['origin_A'][d], centroid[d], places=12)
        self.assertEqual(mark['axis'], [0, 0, 1])
        self.assertLess(mark['max_deviation_A'], 1e-5)
        a, b = rods[0]['a'], rods[0]['b']
        for end in (a, b):
            self.assertAlmostEqual(end[0], centroid[0], places=12)
            self.assertAlmostEqual(end[1], centroid[1], places=12)
        spheres = [p for p in scene['primitives'] if p['kind'] == 'sphere']
        self.assertLess(min(a[2], b[2]), min(p['a'][2]-p['radius'] for p in spheres) - .5)
        self.assertGreater(max(a[2], b[2]), max(p['a'][2]+p['radius'] for p in spheres) + .5)
        self.assertEqual(rods[0]['rgba'], AXIS_RGB + [255])

    def test_operation_must_map_structure_onto_itself(self):
        error = self.error('nh3.xyz', {'operation': 'C4', 'axis': [0, 0, 1]}, code=5)
        self.assertEqual(error['code'], 'symmetry_mismatch')
        self.assertIn('C4 does not map', error['message'])
        error = self.error('nh3.xyz', {'operation': 'C3', 'axis': [0, 0, 1], 'origin_A': [.4, 0, 0]}, code=5)
        self.assertEqual(error['code'], 'symmetry_mismatch')
        scene = self.scene('nh3.xyz', {'operation': 'C4', 'axis': [0, 0, 1], 'verify': False})
        self.assertIsNone(scene['symmetry']['max_deviation_A'])
        self.assertEqual(sum(p['kind'] == 'symmetry_axis' for p in scene['primitives']), 1)
        # Like atoms only: inversion of water maps H onto no hydrogen.
        self.assertEqual(self.error('h2o.xyz', {'operation': 'i'}, code=5)['code'], 'symmetry_mismatch')
        self.scene('nh3.xyz', {'operation': 'C4', 'axis': [0, 0, 1], 'tolerance_A': 1})

    def test_mirror_plane_shapes_and_coplanarity(self):
        scene = self.scene('h2o.xyz', {'operation': 'sigma_v', 'normal': [1, 0, 0]})
        self.assertEqual(scene['face_count'], 1)
        self.assertEqual(len(scene['face_triangles']), 2)  # anisotropic projection: rectangle
        self.assertFalse(any(p['kind'] == 'symmetry_axis' for p in scene['primitives']))
        for triangle in scene['face_triangles']:
            for vertex in triangle['vertices']:
                self.assertAlmostEqual(vertex[0], scene['symmetry']['origin_A'][0], places=12)
        self.assertEqual(sorted(t['boundary_edges'] for t in scene['face_triangles']), [3, 6])
        ys = [v[1] for t in scene['face_triangles'] for v in t['vertices']]
        zs = [v[2] for t in scene['face_triangles'] for v in t['vertices']]
        for _, p in atoms(MOLECULES / 'h2o.xyz'):
            self.assertTrue(min(ys) < p[1] < max(ys) and min(zs) < p[2] < max(zs))
        disc = self.scene('benzene.xyz', {'operation': 'sigma_h', 'axis': [0, 0, 1]})
        self.assertEqual(len(disc['face_triangles']), 64)  # isotropic projection: disc
        radii = {round(math.hypot(v[0], v[1]), 9) for t in disc['face_triangles'] for v in t['vertices'][1:]}
        self.assertEqual(len(radii), 1)
        self.assertGreater(radii.pop(), 2.481)
        forced = self.scene('benzene.xyz', {'operation': 'sigma_h', 'axis': [0, 0, 1], 'plane_shape': 'rectangle'})
        self.assertEqual(len(forced['face_triangles']), 2)
        self.assertEqual(len(self.scene('h2o.xyz', {'operation': 'sigma', 'normal': [1, 0, 0], 'plane_shape': 'disc'})['face_triangles']), 64)

    def test_improper_rotation_inversion_identity_and_canonical_forms(self):
        s6 = self.scene('ethane.xyz', {'operation': 'S6', 'axis': [0, 0, 1]})
        self.assertEqual(sum(p['kind'] == 'symmetry_axis' for p in s6['primitives']), 1)
        self.assertEqual(s6['face_count'], 1)
        for triangle in s6['face_triangles']:
            for vertex in triangle['vertices']:
                self.assertAlmostEqual(vertex[2], 0, places=9)
        self.assertEqual(self.error('ethane.xyz', {'operation': 'C6', 'axis': [0, 0, 1]}, code=5)['code'], 'symmetry_mismatch')
        self.scene('methane.xyz', {'operation': 'S4', 'axis': [0, 0, 1]})
        self.assertEqual(self.error('methane.xyz', {'operation': 'C4', 'axis': [0, 0, 1]}, code=5)['code'], 'symmetry_mismatch')
        for operation in ('i', 'S2'):
            scene = self.scene('ethane.xyz', {'operation': operation})
            self.assertEqual(scene['face_count'], 0)
            self.assertFalse(any(p['kind'] == 'symmetry_axis' for p in scene['primitives']))
            self.assertNotIn('axis', scene['symmetry'])
            self.assertLess(scene['symmetry']['max_deviation_A'], 1e-5)
        s1 = self.scene('benzene.xyz', {'operation': 'S1', 'axis': [0, 0, 1]})
        self.assertEqual((s1['face_count'], sum(p['kind'] == 'symmetry_axis' for p in s1['primitives'])), (1, 0))
        for operation in ('E', 'C1'):
            scene = self.scene('h2o.xyz', {'operation': operation})
            self.assertEqual(scene['face_count'], 0)
            self.assertIsNone(scene['symmetry']['max_deviation_A'])

    def test_atom_defined_axes_planes_and_origins(self):
        # C-C bond of ethane; the origin is the centroid's foot on that line.
        scene = self.scene('ethane.xyz', {'operation': 'C3', 'axis_atoms': [1, 0]})
        self.assertEqual(scene['symmetry']['axis'], [0, 0, 1])
        for value in scene['symmetry']['origin_A']:
            self.assertAlmostEqual(value, 0, places=9)
        # Normal of the three hydrogens of ammonia is its C3 axis.
        scene = self.scene('nh3.xyz', {'operation': 'C3', 'plane_atoms': [1, 2, 3]})
        self.assertAlmostEqual(abs(scene['symmetry']['axis'][2]), 1, places=12)
        # Mirror plane through the three atoms of water is the molecular plane.
        scene = self.scene('h2o.xyz', {'operation': 'sigma_v', 'plane_atoms': [0, 1, 2]})
        self.assertAlmostEqual(abs(scene['symmetry']['axis'][0]), 1, places=12)
        scene = self.scene('benzene.xyz', {'operation': 'C2', 'axis': [1, 0, 0], 'origin_atoms': [0, 3]})
        for value in scene['symmetry']['origin_A']:
            self.assertAlmostEqual(value, 0, places=9)
        self.assertEqual(self.error('nh3.xyz', {'operation': 'C3', 'axis_atoms': [0, 9]})['path'], 'symmetry.axis_atoms')
        linear = self.root / 'linear.xyz'
        linear.write_text('3\nlinear\nO 0 0 1.16\nC 0 0 0\nO 0 0 -1.16\n')
        settings = self.root / 'config.json'
        settings.write_text(json.dumps({'symmetry': {'operation': 'sigma', 'plane_atoms': [0, 1, 2]}}))
        proc = subprocess.run([EXE, linear, '--no-user-config', '--json-errors', '--config', settings, '--inspect-scene'], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 2)
        self.assertEqual(json.loads(proc.stderr)['error']['path'], 'symmetry.plane_atoms')

    def test_dashed_axis_is_centered_and_within_solid_extent(self):
        solid = [p for p in self.scene('nh3.xyz', {'operation': 'C3', 'axis': [0, 0, 1]})['primitives'] if p['kind'] == 'symmetry_axis'][0]
        dashes = [p for p in self.scene('nh3.xyz', {'operation': 'C3', 'axis': [0, 0, 1], 'axis_style': 'dashed'})['primitives'] if p['kind'] == 'symmetry_axis']
        self.assertGreater(len(dashes), 5)
        low, high = sorted((solid['a'][2], solid['b'][2]))
        ends = sorted(z for p in dashes for z in (p['a'][2], p['b'][2]))
        self.assertAlmostEqual((ends[0]+ends[-1])/2, (low+high)/2, places=9)
        for p in dashes:
            self.assertAlmostEqual(abs(p['b'][2]-p['a'][2]), .32, places=9)

    def test_invalid_definitions(self):
        cases = [({'operation': 'C0', 'axis': [0, 0, 1]}, 'symmetry.operation'),
                 ({'operation': 'D3', 'axis': [0, 0, 1]}, 'symmetry.operation'),
                 ({'operation': 'sigma_x', 'axis': [0, 0, 1]}, 'symmetry.operation'),
                 ({'axis': [0, 0, 1]}, 'symmetry.operation'),
                 ({'operation': 'C3'}, 'symmetry.axis'),
                 ({'operation': 'sigma'}, 'symmetry.axis'),
                 ({'operation': 'C3', 'axis': [0, 0, 0]}, 'symmetry.axis'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'normal': [0, 0, 1]}, 'symmetry.normal'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'axis_atoms': [0, 1]}, 'symmetry.axis'),
                 ({'operation': 'C3', 'axis_atoms': [1, 1]}, 'symmetry.axis_atoms'),
                 ({'operation': 'i', 'origin_A': [0, 0, 0], 'origin_atoms': [0]}, 'symmetry.origin_atoms'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'tolerance_A': 0}, 'symmetry.tolerance_A'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'label_color': '#11223344'}, 'symmetry.label_color'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'label_font': 'bold'}, 'symmetry.label_font'),
                 ({'operation': 'C3', 'axis': [0, 0, 1], 'colour': '#112233'}, 'symmetry.colour')]
        for symmetry, path in cases:
            with self.subTest(symmetry=symmetry):
                self.assertEqual(self.error('nh3.xyz', symmetry)['path'], path)
        self.assertEqual(self.error('nh3.xyz', 'C3')['path'], 'symmetry.operation')

    def test_cli_options_null_clear_and_resolved_dump(self):
        base = [EXE, MOLECULES / 'nh3.xyz', '--no-user-config', '--json-errors']
        scene = json.loads(subprocess.check_output(base + ['--cell', 'off', '--symmetry', 'C3', '--symmetry-axis', '0,0,1', '--symmetry-origin', '0,0,0.5', '--inspect-scene']))
        self.assertEqual(scene['symmetry']['origin_A'], [0, 0, .5])
        proc = subprocess.run(base + ['--symmetry-axis', '0,0,1', '--inspect-scene'], capture_output=True, text=True)
        self.assertEqual(proc.returncode, 2)
        proc = subprocess.run(base + ['--symmetry', 'C3', '--inspect-scene'], capture_output=True, text=True)
        self.assertEqual(json.loads(proc.stderr)['error']['path'], 'symmetry.axis')
        dumped = json.loads(self.invoke('nh3.xyz', {'operation': 'C3', 'axis_atoms': [0, 1]}, '--symmetry-axis', '0,0,1', '--dump-config').stdout)
        symmetry = dumped['config']['symmetry']
        self.assertEqual((symmetry['operation'], symmetry['axis'], symmetry['axis_style']), ('C3', [0, 0, 1], 'solid'))
        self.assertNotIn('axis_atoms', symmetry)  # the CLI direction replaces the atom-defined one
        self.assertEqual(dumped['provenance']['symmetry.axis']['layer'], 'cli')
        self.assertEqual(json.loads(self.invoke('nh3.xyz', symmetry, '--dump-config').stdout)['config']['symmetry'], symmetry)
        self.assertNotIn('symmetry', self.scene('nh3.xyz', None))
        self.assertNotIn('symmetry', json.loads(self.invoke('nh3.xyz', None, '--dump-config', config=dict(self.config, symmetry=None)).stdout)['config'])
        self.invoke('nh3.xyz', {'operation': 'C4', 'axis': [0, 0, 1]}, '--validate-config', code=5)

    def test_render_axis_label_report_and_thread_determinism(self):
        symmetry = {'operation': 'C3', 'axis': [0, 0, 1], 'label_scale': 2}
        report = self.root / 'report.json'
        self.invoke('nh3.xyz', symmetry, '-o', self.output, '--threads', 1, '--report', report)
        labelled = decode_png(self.output)
        pixels = [list(labelled[2][4*i:4*i+4]) for i in range(labelled[0]*labelled[1])]
        self.assertGreater(pixels.count(AXIS_RGB + [255]), 100)
        manifest = json.loads(report.read_text())
        self.assertEqual(manifest['symmetry']['operation'], 'C3')
        self.assertLess(manifest['symmetry']['max_deviation_A'], 1e-5)
        self.invoke('nh3.xyz', dict(symmetry, show_label=False), '-o', self.output, '--threads', 1)
        plain = decode_png(self.output)
        ink = [i for i in range(labelled[0]*labelled[1]) if pixels[i][:3] == [24, 28, 32]]
        self.assertGreater(len(ink), 8)  # label ink is absent without a label
        self.assertFalse(any(list(plain[2][4*i:4*i+3]) == [24, 28, 32] for i in range(plain[0]*plain[1])))
        # The label lies beyond the upper axis tip and inside the image.
        rows = [i // labelled[0] for i in ink]
        columns = [i % labelled[0] for i in ink]
        self.assertGreaterEqual(min(rows), 2)
        self.assertLess(max(rows), labelled[1] // 4)
        self.assertTrue(labelled[0]*.3 < sum(columns)/len(columns) < labelled[0]*.7)
        # Both embedded fonts, subscripts and an unknown character render; they differ.
        self.invoke('nh3.xyz', dict(symmetry, label="\u03c3_v' \u00e9", label_font='sans'), '-o', self.output, '--threads', 1)
        sans = decode_png(self.output)
        self.invoke('nh3.xyz', dict(symmetry, label="\u03c3_v' \u00e9"), '-o', self.output, '--threads', 1)
        self.assertNotEqual(sans[2], decode_png(self.output)[2])
        self.invoke('nh3.xyz', symmetry, '-o', self.output, '--threads', 4)
        again = decode_png(self.output)
        self.invoke('nh3.xyz', symmetry, '-o', self.output, '--threads', 1)
        self.assertEqual(again[2], decode_png(self.output)[2])

    def test_mirror_tints_only_geometry_behind_the_plane_and_keeps_styles(self):
        symmetry = {'operation': 'sigma_h', 'axis': [0, 0, 1], 'plane_color': '#ff000080', 'plane_outline_width': 0, 'show_label': False}
        config = dict(self.config, camera={'view': [0, 0, 1], 'up': [0, 1, 0]}, image={'width': 160, 'height': 160, 'aa': 'native', 'background': '#ffffff'})
        self.invoke('bf3.xyz', symmetry, '-o', self.output, config=config)
        image = decode_png(self.output)
        self.assertEqual(list(image[2][4*(80*160+2):4*(80*160+2)+3])[0], 255)
        corner = list(image[2][4*(80*160+30):4*(80*160+30)+4])
        self.assertEqual(corner, [255, 127, 127, 255])  # half-red disc over white
        for style in ('shaded', 'illustrated', 'ortep', 'balsac'):
            styled = dict(self.config, image={'width': 96, 'height': 96, 'style': style})
            self.invoke('ethane.xyz', {'operation': 'S6', 'axis': [0, 0, 1]}, '-o', self.output, config=styled)
            self.invoke('ethane.xyz', {'operation': 'i'}, '-o', self.output, config=styled)
        mono = dict(self.config, image={'width': 96, 'height': 96, 'style': 'ortep', 'tone': 'monocolor'})
        self.invoke('nh3.xyz', {'operation': 'C3', 'axis': [0, 0, 1]}, '-o', self.output, config=mono)

    def test_worker_scene_cache_tracks_symmetry(self):
        job = {'input': str(MOLECULES / 'nh3.xyz'), 'output': str(self.output), 'threads': 1,
               'config': dict(self.config, symmetry={'operation': 'C3', 'axis': [0, 0, 1]})}
        other = json.loads(json.dumps(job))
        other['config']['symmetry'] = {'operation': 'sigma_v', 'normal': [1, 0, 0]}
        requests = [{'id': 'a', 'op': 'render', 'job': job}, {'id': 'b', 'op': 'render', 'job': job}, {'id': 'c', 'op': 'render', 'job': other}]
        proc = subprocess.run([EXE, '--batch', '--cache-mib', '16'], input='\n'.join(json.dumps(r) for r in requests)+'\n',
                              capture_output=True, text=True, timeout=30)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        replies = [json.loads(line) for line in proc.stdout.splitlines() if line.strip()]
        hits = {r['id']: r['result']['cache']['scene_hit'] for r in replies if r.get('id') in ('a', 'b', 'c')}
        self.assertEqual(hits, {'a': False, 'b': True, 'c': False})


if __name__ == '__main__':
    unittest.main()
