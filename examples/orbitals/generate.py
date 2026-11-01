#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compute CO with PyQInt, tessellate with PyTessel, and render with QASR.

Optional dependencies: pyqint==1.4.3 pytessel==1.3.0 pillow==13.0.0.
Run from any directory; --render-only reuses the checked-in PLY meshes.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

os.environ.setdefault('MPLCONFIGDIR', '/tmp/qasr-matplotlib')
BOHR_A = 0.529177210903
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def calculate():
    import importlib.metadata
    import numpy as np
    from pyqint import Molecule, HF, PyQInt
    from pytessel import PyTessel

    mol = Molecule('CO')
    # Pass bohr explicitly to avoid differences in library conversion constants.
    distance_A = 1.128
    mol.add_atom('C', 0, 0, -distance_A / (2 * BOHR_A), unit='bohr')
    mol.add_atom('O', 0, 0, distance_A / (2 * BOHR_A), unit='bohr')
    result = HF(mol, 'sto3g').rhf()
    integrator, tess = PyQInt(), PyTessel()
    n, half, iso = 121, 7.0, 0.03
    # Both programs use x fastest, then y, then z, and spacing L/N.
    # PyTessel returns positions centered on the box: [-L/2, L/2).
    grid = integrator.build_rectgrid3d(-half, half, n, endpoint=False)
    axis = np.linspace(-half, half, n, endpoint=False)
    assert np.allclose(grid[:n, 0], axis)
    assert np.allclose(grid[:n, 1:], -half)
    # An asymmetric planar field catches shifts, axis swaps and N vs N-1 errors.
    for dim in range(3):
        field = grid[:, dim] - 0.73
        v, _, _ = tess.marching_cubes(field, (n, n, n), np.diag([2*half]*3).ravel(), 0.)
        assert np.allclose(v[:, dim], 0.73, atol=2e-6)
    (HERE / 'co.xyz').write_text(f'2\nCO; coordinates in angstrom; bond {distance_A} A\nC 0 0 {-distance_A/2}\nO 0 0 {distance_A/2}\n')
    records = []
    for i in range(result['orbc'].shape[1]):
        field = integrator.plot_wavefunction(grid, result['orbc'][:, i], result['cgfs'])
        # No surface may touch the sampling boundary.
        cube = field.reshape(n, n, n)
        assert max(np.max(np.abs(np.take(cube, [0, -1], axis=d))) for d in range(3)) < iso
        layers = []
        for sign, name, color in [(1, 'positive', '#4c9cdb'), (-1, 'negative', '#ed9163')]:
            # Tessellate +iso of sign*psi, giving consistent normals on both phases.
            v, normals, indices = tess.marching_cubes(sign*field, (n, n, n), np.diag([2*half]*3).ravel(), iso)
            if not len(indices):
                continue
            assert v.ndim == 2 and v.shape[1] == 3 and normals.shape == v.shape
            assert np.all(np.linalg.norm(normals, axis=1) > 0.9)
            # Analytic wavefunction gradients avoid marching-cubes finite-difference
            # noise in highlights; no vertex positions or isovalues are changed.
            gradients = integrator.plot_gradient(v.astype(np.float64), result['orbc'][:, i], result['cgfs'])
            norms = np.linalg.norm(gradients, axis=1)
            assert np.all(norms > 1e-12)
            normals = -sign * gradients / norms[:, None]
            filename = f'meshes/co-{i+1:02}-{name}.ply'
            tess.write_ply(str(HERE / filename), v, normals, indices)
            layers.append({'file': '../'+filename, 'units': 'bohr', 'origin': [0, 0, 0], 'color': color})
        config = {'schema_version': 1,
                  'image': {'width': 480, 'height': 400, 'style': 'illustrated', 'aa': '4x', 'outline_width': 1.5},
                  'camera': {'view': [1, 0.65, 0.28], 'up': [0, 0, 1], 'margin': 0.12},
                  'overlays': {'cell': False, 'scale_bar': True},
                  'geometry': {'bond_radius_A': 0.065, 'bond_colors': 'atoms'},
                  'elements': {'C': {'radius_A': 0.20}, 'O': {'radius_A': 0.20}},
                  'orbitals': layers}
        (HERE / f'recipes/co-{i+1:02}.json').write_text(json.dumps(config, indent=2)+'\n')
        records.append({'mo': i+1, 'energy_hartree': float(result['orbe'][i]),
                        'occupation': 2 if i < 7 else 0, 'config': f'recipes/co-{i+1:02}.json'})
    manifest = {'molecule': 'CO', 'method': 'RHF/STO-3G', 'bond_length_A': distance_A,
                'energy_hartree': float(result['energy']), 'bohr_A': BOHR_A,
                'grid': {'shape': [n]*3, 'half_width_bohr': half, 'endpoint': False,
                         'order': 'x fastest, z slowest', 'spacing_bohr': 2*half/n},
                'isovalue_bohr^-3/2': iso,
                'normals': 'normalized analytic PyQInt orbital gradients',
                'versions': {p: importlib.metadata.version(p) for p in ['pyqint', 'pytessel', 'numpy']},
                'orbitals': records}
    (HERE / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')


def render(exe):
    from PIL import Image, ImageDraw
    manifest = json.loads((HERE / 'manifest.json').read_text())
    gallery = Image.new('RGB', (5*480, 2*460+75), 'white')
    draw = ImageDraw.Draw(gallery)
    draw.text((24, 12), 'CO molecular orbitals | RHF / STO-3G | C-O = 1.128 angstrom', fill='#222222', font_size=25)
    draw.text((24, 45), 'PyQInt wavefunctions; PyTessel isosurfaces at +/-0.03 bohr^(-3/2); QASR illustrated style. Blue/orange denote phase; panel scales differ.', fill='#333333', font_size=18)
    for index, orbital in enumerate(manifest['orbitals']):
        stem = f"co-{orbital['mo']:02}"
        subprocess.run([str(exe), str(HERE/'co.xyz'), '--no-user-config', '--config', str(HERE/orbital['config']),
                        '-o', str(HERE/f'renders/{stem}.png'), '--threads', '4'], check=True)
        x, y = index % 5*480, index//5*460+75
        gallery.paste(Image.open(HERE/f'renders/{stem}.png').convert('RGB'), (x, y))
        role = 'occupied' if orbital['occupation'] else 'virtual'
        if orbital['mo'] == 7: role += ' / HOMO'
        if orbital['mo'] == 8: role += ' / LUMO'
        draw.text((x+18, y+402), f"MO {orbital['mo']} | {role}", fill='#222222', font_size=21)
        draw.text((x+18, y+430), f"{orbital['energy_hartree']:.6f} hartree", fill='#444444', font_size=18)
    gallery.save(HERE/'gallery.png')
    cards = '\n'.join(f'<figure><img src="renders/co-{o["mo"]:02}.png" alt="CO MO {o["mo"]}"><figcaption>MO {o["mo"]} — {o["energy_hartree"]:.6f} hartree; occupation {o["occupation"]}</figcaption></figure>' for o in manifest['orbitals'])
    (HERE/'gallery.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>CO molecular orbitals</title><style>body{font:18px system-ui;margin:2em;background:#fafafa}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:1em}figure{margin:0;background:white;padding:1em}img{width:100%}</style><h1>CO molecular orbitals</h1><p>RHF/STO-3G; bond 1.128 Å. Blue and orange: opposite phases at ±0.03 bohr<sup>−3/2</sup>. Orbitals 1–7 occupied, 7 HOMO, 8–9 degenerate LUMOs. Each panel fits its own bounds.</p><main>'+cards+'</main></html>\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render-only', action='store_true')
    parser.add_argument('--qasr', type=Path, default=ROOT/'build/qasr')
    args = parser.parse_args()
    for directory in ['meshes','recipes','renders']:
        (HERE/directory).mkdir(exist_ok=True)
    if not args.render_only:
        calculate()
    render(args.qasr.resolve())
