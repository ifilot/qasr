# SPDX-License-Identifier: GPL-3.0-only
"""Render the standalone handbook's examples and package its offline downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--source-archive', type=Path, help='validated source archive to offer for download')
    args = parser.parse_args()
    exe = args.executable.resolve()
    site = ROOT/'docs/website'
    assets, downloads = site/'assets', site/'downloads'
    assets.mkdir(parents=True, exist_ok=True); downloads.mkdir(parents=True, exist_ok=True)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    fixtures = ROOT/'tests/fixtures'
    examples = {
        'CONTCAR.co': fixtures/'slabs/01_co0001_clean/CONTCAR',
        'CONTCAR.pd-ceria': fixtures/'slabs/18_pd_ceria_co_oxidation_ts/CONTCAR',
        'POSCAR.skew': fixtures/'POSCAR.skew',
        'OUTCAR.relax': fixtures/'OUTCAR.relax',
        'illustrated.json': ROOT/'presets/illustrated.json',
        'LICENSE': ROOT/'LICENSE',
        'SKILL.md': ROOT/'SKILL.md',
        'reaction.xyz': ROOT/'examples/movies/reaction.xyz',
        'reaction.json': ROOT/'examples/movies/reaction.json',
    }
    styles = ['ortep', 'balsac', 'ortep-monocolor', 'ortep-dualtone',
              'balsac-monocolor', 'balsac-dualtone']
    examples.update({style+'.json': ROOT/'presets'/(style+'.json') for style in styles})
    for name, source in examples.items(): shutil.copyfile(source, downloads/name)
    xml = '''<?xml version="1.0" encoding="UTF-8"?>
<modeling>
 <atominfo>
  <atoms>2</atoms><types>2</types>
  <array name="atoms"><set>
   <rc><c>Si</c><c>1</c></rc><rc><c>O</c><c>2</c></rc>
  </set></array>
  <array name="atomtypes"><set>
   <rc><c>1</c><c>Si</c></rc><rc><c>1</c><c>O</c></rc>
  </set></array>
 </atominfo>
 <calculation><structure>
  <crystal><varray name="basis">
   <v>2 0 0</v><v>1 2 0</v><v>0.5 1 2</v>
  </varray></crystal>
  <varray name="positions"><v>0.5 0.25 0.125</v><v>0 0 0</v></varray>
 </structure></calculation>
 <structure name="finalpos">
  <crystal><varray name="basis">
   <v>2 0 0</v><v>1 2 0</v><v>0.5 1 2</v>
  </varray></crystal>
  <varray name="positions"><v>0.5 0.25 0.125</v><v>0 0 0</v></varray>
 </structure>
</modeling>
'''
    (downloads/'vasprun.example.xml').write_text(xml)
    publication = {
        'image': {'width': 1200, 'height': 900, 'background': '#ffffff00', 'style': 'illustrated',
                  'aa': '2x', 'depth_fade': 0.6, 'outline_width': 1.5, 'png_encoder': 'libpng',
                  'png_compression': 1, 'png_filter': 'adaptive'},
        'camera': {'projection': 'orthographic', 'view': 'top', 'margin': 0.06},
        'geometry': {'expand_xy': [1, 1], 'image_colors': 'greyscale', 'sphere_scale': 1, 'bond_radius_A': 0.12},
        'elements': {'O': {'color': '#e88069', 'radius_A': 0.5}},
        'bond_cutoffs_A': {'Co-O': 2.5},
        'overlays': {'cell': True, 'cell_width': 3, 'labels': 'none', 'axes': True, 'legend': True, 'scale_bar': True},
    }
    (downloads/'publication.json').write_text(json.dumps(publication, indent=2)+'\n')
    (downloads/'EXAMPLES.txt').write_text('''QASR: Quick Atomic Structure Renderer — handbook example kit

Extract this kit into an examples/ directory inside your source checkout.
Commands in the handbook are run from that checkout root.

CONTCAR.co: user-provided clean Co(0001), 3x3 cell, 63 source atoms.
CONTCAR.pd-ceria: user-provided Pd/ceria CO-oxidation transition-state snapshot,
249 source atoms. The source's TS/convergence assignment is not independently verified.
The original slab provenance is retained in SLAB-PROVENANCE.md and SLAB-MANIFEST.json.
POSCAR.skew and OUTCAR.relax: original synthetic parser/geometry fixtures.
vasprun.example.xml: original synthetic, complete two-atom XML document.
illustrated.json: the repository's illustrated top-view preset.
ortep*.json and balsac*.json: spherical style and dithered tone presets.
publication.json: a portable libpng example with transparent output and periodic context.

Original project material and these user-provided examples: GPL-3.0-only.
See LICENSE. Website images are program-generated examples, not AI illustrations.
''')
    provenance=(fixtures/'slabs/README.md').read_text().replace('`manifest.json`','`SLAB-MANIFEST.json`')
    bundled={p.relative_to(fixtures/'slabs').as_posix():name for name,p in examples.items()
             if p.is_relative_to(fixtures/'slabs')}
    for source in sorted((fixtures/'slabs').glob('*/CONTCAR')):
        relative=source.relative_to(fixtures/'slabs').as_posix()
        target=bundled.get(relative,'https://github.com/ifilot/qasr/blob/master/tests/fixtures/slabs/'+relative)
        provenance=provenance.replace(']('+relative+')',']('+target+')')
    provenance+='\nIn the example kit, links to the two bundled slabs open local files; other slab links open the source fixture collection.\n'
    (downloads/'SLAB-PROVENANCE.md').write_text(provenance)
    shutil.copyfile(fixtures/'slabs/manifest.json', downloads/'SLAB-MANIFEST.json')
    with zipfile.ZipFile(downloads/'qasr-examples.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        kit = [*examples, 'vasprun.example.xml', 'publication.json', 'EXAMPLES.txt', 'SLAB-PROVENANCE.md', 'SLAB-MANIFEST.json']
        for name in sorted(kit): archive.write(downloads/name, name)
    jobs = [
        ('co-shaded', 'CONTCAR.co', ['--view', 'top', '--width', '768', '--height', '768']),
        ('co-illustrated', 'CONTCAR.co', ['--config', 'illustrated.json']),
        ('co-annotated', 'CONTCAR.co', ['--config', 'illustrated.json', '--labels', 'element', '--legend', 'on', '--axes', 'on', '--scale-bar', 'on']),
        ('co-periodic-normal', 'CONTCAR.co', ['--config', 'illustrated.json', '--expand-xy', '1,1', '--image-colors', 'normal']),
        ('co-periodic-muted', 'CONTCAR.co', ['--config', 'illustrated.json', '--expand-xy', '1,1', '--image-colors', 'muted']),
        ('co-periodic-greyscale', 'CONTCAR.co', ['--config', 'illustrated.json', '--expand-xy', '1,1', '--image-colors', 'greyscale']),
        ('pd-ceria', 'CONTCAR.pd-ceria', ['--config', 'illustrated.json']),
        ('skew-transparent', 'POSCAR.skew', ['--width', '768', '--height', '768', '--background', '#ffffff00']),
        ('relax-first', 'OUTCAR.relax', ['--view', 'top', '--width', '768', '--height', '768', '--frame', '0']),
        ('relax-final', 'OUTCAR.relax', ['--view', 'top', '--width', '768', '--height', '768', '--frame', 'final']),
        ('xml-example', 'vasprun.example.xml', ['--width', '768', '--height', '768', '--view', '1,1,1']),
        ('publication', 'CONTCAR.co', ['--config', 'publication.json']),
    ]
    jobs.extend(('co-'+style, 'CONTCAR.co',
                 ['--config', style+'.json', '--width', '640', '--height', '480'])
                for style in styles)
    records = []
    for name, source, flags in jobs:
        actual = [str(downloads/v) if v.endswith('.json') else v for v in flags]
        output = assets/(name+'.png')
        cmd = [str(exe), str(downloads/source), '--no-user-config', *actual, '--png-compression', '6', '-o', str(output)]
        subprocess.run(cmd, check=True, capture_output=True)
        shown = ['build-release/qasr', 'examples/'+source, '--no-user-config',
                 *['examples/'+v if v.endswith('.json') else v for v in flags], '--png-compression', '6', '-o', name+'.png']
        records.append({'id': name, 'source': source, 'input_sha256': sha(downloads/source),
                        'image': 'assets/'+output.name, 'png_sha256': sha(output), 'command': shown})
    if args.source_archive:
        shutil.copyfile(args.source_archive, downloads/'qasr-source.tar.gz')
        manifest = args.source_archive.parent/(args.source_archive.name.replace('.tar.gz', '-source-manifest.json'))
        if manifest.exists(): shutil.copyfile(manifest, downloads/'source-manifest.json')
    subprocess.run([str(exe), str(downloads/'CONTCAR.co'), '--config', str(downloads/'publication.json'), '--inspect', '--no-user-config'], capture_output=True, check=True)
    inspection = json.loads(subprocess.check_output([exe, downloads/'vasprun.example.xml', '--inspect', '--no-user-config']))
    (downloads/'inspect-example.json').write_text(json.dumps(inspection, indent=2)+'\n')
    (site/'examples-manifest.json').write_text(json.dumps({
        'executable_sha256': sha(exe), 'build_info': json.loads(subprocess.check_output([exe, '--build-info'])),
        'availability': subprocess.check_output([exe, '--version'], text=True).strip()+' release',
        'images': records,
        'downloads': {name: sha(downloads/name) for name in sorted(set(kit + ['qasr-examples.zip','inspect-example.json','qasr-source.tar.gz','source-manifest.json','vasp-render-source.tar.gz','legacy-v0.3.0-source-manifest.json']))},
    }, indent=2)+'\n')
    print(f'Built {len(records)} rendered examples and the offline example kit in {site}')


if __name__ == '__main__': main()
