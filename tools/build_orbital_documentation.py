# SPDX-License-Identifier: GPL-3.0-only
"""Package the runnable CO example and record the handbook's orbital figures."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    args=parser.parse_args();exe=args.executable.resolve()
    site=ROOT/'docs/website';example=ROOT/'examples/orbitals'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    shutil.copyfile(example/'gallery.png',site/'assets/co-orbitals.png')
    bundle=site/'downloads/co-orbitals.zip'
    files=sorted(p for p in example.rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED) as archive:
        for p in files:archive.write(p,'orbitals/'+p.relative_to(example).as_posix())
    figures=['assets/co-orbitals.png','assets/co-orbital-rim-comparison.png']
    (site/'orbital-manifest.json').write_text(json.dumps({
        'build_info':json.loads(subprocess.check_output([exe,'--build-info'])),
        'figures':[{ 'image':name,'sha256':sha(site/name)} for name in figures],
        'inputs':{p.relative_to(ROOT).as_posix():sha(p) for p in files},
        'downloads':{'co-orbitals.zip':sha(bundle)}},indent=2)+'\n')
    print('Built orbital example download and two figure records')


if __name__=='__main__':main()
