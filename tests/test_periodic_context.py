# SPDX-License-Identifier: GPL-3.0-only
"""Periodic context: fractional identity, centered geometry, colors and pixels."""
import itertools
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png

EXE = Path(sys.argv.pop(1)).resolve()
ROOT = Path(__file__).resolve().parents[1]


def poscar(basis, positions):
    return 'test\n1\n'+'\n'.join(' '.join(map(str, v)) for v in basis)+'\nC O\n1 1\nDirect\n'+'\n'.join(' '.join(map(str, v)) for v in positions)+'\n'


class PeriodicContextTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='qasr-context-')
        self.root = Path(self.tmp.name)
        self.input = self.root/'POSCAR'
        self.basis = [[4, 0, 0], [-1.5, 4, 0], [.5, .25, 10]]
        self.input.write_text(poscar(self.basis, [[.1, .1, .2], [.9, .9, .2]]))
        self.cfg = self.root/'settings.json'
        self.cfg.write_text(json.dumps({'elements': {'C': {'color': '#cc402080'}, 'O': {'color': '#2266ccff'}}}))

    def tearDown(self):
        self.tmp.cleanup()

    def run_cli(self, *args, code=0):
        p = subprocess.run([EXE, self.input, '--config', self.cfg, *map(str, args)], capture_output=True, text=True, timeout=30)
        self.assertEqual(p.returncode, code, p.stderr)
        if code: self.assertEqual(p.stdout, '')
        return p

    def scene(self, *args):
        return json.loads(self.run_cli('--inspect-scene', *args).stdout)

    def test_centered_counts_primary_identity_and_palette(self):
        base = [p for p in self.scene()['primitives'] if p['kind'] == 'sphere']
        for mode in ['normal', 'muted', 'greyscale']:
            scene = self.scene('--expand-xy', '1,2', '--image-colors', mode)
            spheres = [p for p in scene['primitives'] if p['kind'] == 'sphere']
            self.assertEqual(scene['instance_count'], 30)
            self.assertEqual({tuple(p['origin']) for p in spheres}, set(itertools.product(range(-1, 2), range(-2, 3), [0])))
            self.assertEqual(sum(p['primary_cell'] for p in spheres), 2)
            for p in spheres:
                original = base[p['source'][0]]
                expected = [original['a'][j]+sum(p['origin'][d]*self.basis[d][j] for d in range(3)) for j in range(3)]
                for a, b in zip(p['a'], expected): self.assertAlmostEqual(a, b)
                self.assertEqual(p['radius'], original['radius'])
                self.assertEqual(p['rgba'][3], original['rgba'][3])
                if p['primary_cell'] or mode == 'normal': self.assertEqual(p['rgba'], original['rgba'])
                else:
                    self.assertNotEqual(p['rgba'][:3], original['rgba'][:3])
                    if mode == 'greyscale': self.assertEqual(len(set(p['rgba'][:3])), 1)
                    else: self.assertLess(max(p['rgba'][:3])-min(p['rgba'][:3]), max(original['rgba'][:3])-min(original['rgba'][:3]))

    def test_expansion_defaults_cli_json_precedence_and_restore(self):
        settings = json.loads(self.cfg.read_text()); settings['geometry'] = {'expand_xy': [1, 1], 'image_colors': 'greyscale'}
        self.cfg.write_text(json.dumps(settings))
        a = self.scene(); b = self.scene('--expand-xy', '0,0', '--image-colors', 'normal')
        self.assertEqual(a['instance_count'], 18); self.assertEqual(b['instance_count'], 2)
        data = json.loads(self.run_cli('--inspect', '--expand-xy', '2,0', '--image-colors', 'muted').stdout)
        self.assertEqual(data['settings']['geometry']['repeat'], [5, 1, 1])
        self.assertEqual(data['settings']['geometry']['expand_xy'], [2, 0])
        self.assertEqual(data['settings']['geometry']['image_colors'], 'muted')
        del settings['geometry']['image_colors']; self.cfg.write_text(json.dumps(settings))
        self.assertEqual(json.loads(self.run_cli('--inspect').stdout)['settings']['geometry']['image_colors'], 'muted')
        # Disabled expansion and explicit normal colors restore the old pixels.
        self.cfg.write_text(json.dumps({'elements': settings['elements']}))
        for flags, name in [([], 'base'), (['--expand-xy', '0,0', '--image-colors', 'normal'], 'restored')]:
            self.run_cli('-o', self.root/(name+'.png'), *flags)
        self.assertEqual(decode_png(self.root/'base.png'), decode_png(self.root/'restored.png'))

    def test_centered_bonds_match_translated_positive_supercell(self):
        positive = self.scene('--repeat', '3,3,1')
        centered = self.scene('--expand-xy', '1,1', '--image-colors', 'normal')
        self.assertEqual(positive['base_bond_count'], centered['base_bond_count'])
        shift = [self.basis[0][j]+self.basis[1][j] for j in range(3)]
        def keys(scene, translated=False):
            result = set()
            for p in scene['primitives']:
                if p['kind'] != 'bond': continue
                coords = [round(v-(shift[j] if translated else 0), 9) for point in ['a', 'b'] for j, v in enumerate(p[point])]
                result.add((tuple(p['source']), tuple(p['image']), *coords))
            return result
        self.assertTrue(keys(centered))
        self.assertEqual(keys(positive, True), keys(centered))
        self.assertEqual(centered['bond_count'], len(keys(centered)))

    def test_primary_outline_wrap_and_existing_repeat_color_support(self):
        self.input.write_text(poscar(self.basis, [[-0.1, 1.1, .2], [1, 0, .2]]))
        scene = self.scene('--expand-xy', '1,1')
        cells = [p for p in scene['primitives'] if p['kind'] == 'cell']
        self.assertEqual(len(cells), 12)
        corners = {tuple(sum(bits[d]*self.basis[d][j] for d in range(3)) for j in range(3)) for bits in itertools.product([0,1], repeat=3)}
        self.assertEqual({tuple(p[point]) for p in cells for point in ['a', 'b']}, corners)
        for p in scene['primitives']:
            if p['kind'] == 'sphere' and p['primary_cell']:
                self.assertEqual(p['origin'], [0,0,0])
        positive = self.scene('--repeat', '2,2,1', '--image-colors', 'greyscale')
        self.assertEqual(positive['instance_count'], 8)
        self.assertEqual(sum(p.get('primary_cell', False) for p in positive['primitives']), 2)

    def test_invalid_values_conflicts_and_limits_preserve_output(self):
        output = self.root/'out.png'; output.write_bytes(b'original')
        for flags in [['--expand-xy','-1,1'], ['--expand-xy','1.5,1'], ['--expand-xy','512,1'],
                      ['--expand-xy','1'], ['--expand-xy','1,1,1'], ['--image-colors','sepia'],
                      ['--expand-xy','1,1','--repeat','2,2,1']]:
            self.run_cli('-o', output, *flags, code=2); self.assertEqual(output.read_bytes(), b'original')
        # Four atoms times 1023^2 exceeds the parsed instance limit.
        self.input.write_text('large repeat\n1\n5 0 0\n0 5 0\n0 0 5\nC\n4\nDirect\n0 0 0\n.1 0 0\n.2 0 0\n.3 0 0\n')
        self.run_cli('-o', output, '--expand-xy', '511,511', code=6)
        self.assertEqual(output.read_bytes(), b'original')
        for value in [[-1,1], [1.5,1], [True,1], [512,0], [1], '1,1']:
            self.cfg.write_text(json.dumps({'geometry': {'expand_xy': value}}))
            self.run_cli('-o', output, code=2)

    def test_render_styles_threads_and_legend_use_primary_palette(self):
        # The legend must use the primary palette even though negative image
        # origins appear first in the scene's deterministic iteration order.
        workers = ['1', '2'] if json.loads(subprocess.check_output([EXE, '--build-info']))['openmp'] else ['1']
        for style in ['shaded', 'illustrated']:
            rendered = {}
            for mode in ['normal', 'muted', 'greyscale']:
                for threads in workers:
                    out = self.root/f'{style}-{mode}-{threads}.png'
                    self.run_cli('-o', out, '--expand-xy', '1,1', '--image-colors', mode, '--style', style,
                                 '--view', 'top', '--width', '512', '--height', '512', '--legend', 'on', '--axes', 'on', '--scale-bar', 'on', '--threads', threads)
                    rendered[mode, threads] = decode_png(out)
                if len(workers) > 1: self.assertEqual(rendered[mode, '1'], rendered[mode, '2'])
            self.assertNotEqual(rendered['normal', '1'], rendered['muted', '1'])
            self.assertNotEqual(rendered['normal', '1'], rendered['greyscale', '1'])
            scene = self.scene('--expand-xy', '1,1', '--legend', 'on', '--axes', 'on', '--scale-bar', 'on', '--width','512','--height','512')
            right = scene['camera']['viewport_px']['width']
            width, height, _ = rendered['normal', '1']
            for mode in ['muted', 'greyscale']:
                for y in range(height):
                    a = rendered['normal', '1'][2][4*(y*width+right):4*((y+1)*width)]
                    b = rendered[mode, '1'][2][4*(y*width+right):4*((y+1)*width)]
                    self.assertEqual(a, b)

    def test_all_twenty_slabs_centered(self):
        manifest = json.loads((ROOT/'tests/fixtures/slabs/manifest.json').read_text())
        for item in manifest['structures']:
            with self.subTest(item=item['id']):
                source = ROOT/'tests/fixtures/slabs'/item['file']
                result = subprocess.run([EXE, source, '--expand-xy', '1,1', '--image-colors', 'greyscale', '--inspect-scene'], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                scene = json.loads(result.stdout)
                atoms = [p for p in scene['primitives'] if p['kind'] == 'sphere']
                self.assertEqual(len(atoms), 9*sum(p['primary_cell'] for p in atoms))
                self.assertEqual(len({(p['source'][0], tuple(p['origin'])) for p in atoms}), len(atoms))
                self.assertTrue(all(len(set(p['rgba'][:3])) == 1 for p in atoms if not p['primary_cell']))


if __name__ == '__main__': unittest.main()
