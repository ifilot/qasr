# SPDX-License-Identifier: GPL-3.0-only
"""Check handbook links, asset provenance, downloadable sources and CLI coverage."""
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import tarfile
from urllib.parse import unquote, urlsplit
import zipfile

ROOT = Path(__file__).resolve().parents[1]


class Document(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids, self.links, self.reference = [], [], []
        self.in_reference = False

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if 'id' in attrs:
            self.ids.append(attrs['id'])
            if tag == 'section': self.in_reference = attrs['id'] == 'cli-reference'
        if tag in ['a','link','script','img']:
            value = attrs.get('href') if tag in ['a','link'] else attrs.get('src')
            if value: self.links.append(value)
        if 'data-image' in attrs: self.links.append(attrs['data-image'])
        if 'data-lightbox' in attrs: self.links.append(attrs['data-lightbox'])

    def handle_endtag(self, tag):
        if tag == 'section': self.in_reference = False

    def handle_data(self, data):
        if self.in_reference: self.reference.append(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path, nargs='?')
    parser.add_argument('--development-executable', type=Path, help='also verify post-release development schemas and source against this executable/worktree')
    parser.add_argument('--static-only', action='store_true', help='check standalone site assets without compiling or invoking the renderer')
    parser.add_argument('--output', type=Path, default=ROOT/'artifacts/website-validation/static-report.json')
    args = parser.parse_args()
    if not args.static_only and not args.executable: parser.error('provide an executable or use --static-only')
    exe = args.executable.resolve() if args.executable else None
    site = ROOT/'docs/website'
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    document = Document(); document.feed((site/'index.html').read_text())
    assert len(document.ids) == len(set(document.ids)), 'duplicate HTML IDs'
    guide = Document(); guide.feed((site/'configuration.html').read_text())
    batch = Document(); batch.feed((site/'batch.html').read_text())
    checked = 0
    for value in document.links + guide.links + batch.links:
        url = urlsplit(value)
        if url.scheme or url.netloc: continue
        if url.path:
            path = (site/unquote(url.path)).resolve()
            assert path.is_relative_to(site.resolve()) and path.is_file(), value
        if url.fragment and not url.path: assert unquote(url.fragment) in document.ids + guide.ids + batch.ids, value
        checked += 1
    for path in ['app.js', 'styles.css']:
        text = (site/path).read_text()
        assert 'https://' not in text and 'http://' not in text, 'runtime network dependency in '+path
    manifest = json.loads((site/'examples-manifest.json').read_text())
    if not args.static_only:
        assert manifest['executable_sha256'] == sha(exe), 'examples refer to a different executable'
    assert len(manifest['images']) == 12
    for record in manifest['images']:
        assert sha(site/record['image']) == record['png_sha256']
        assert sha(site/'downloads'/record['source']) == record['input_sha256']
    for name, digest in manifest['downloads'].items(): assert sha(site/'downloads'/name) == digest, name
    with zipfile.ZipFile(site/'downloads/vasp-render-examples.zip') as z:
        for name in ['CONTCAR.co','CONTCAR.pd-ceria','POSCAR.skew','OUTCAR.relax','vasprun.example.xml','illustrated.json','publication.json','LICENSE']:
            assert z.read(name) == (site/'downloads'/name).read_bytes(), name
    # The complete inspection download is real program output, not a hand-written
    # JSON mock that may drift from parser behavior.
    if not args.static_only:
        expected = json.loads(subprocess.check_output([exe, site/'downloads/vasprun.example.xml', '--inspect']))
        assert expected == json.loads((site/'downloads/inspect-example.json').read_text())
    source = json.loads((site/'downloads/source-manifest.json').read_text())
    assert sha(site/'downloads/vasp-render-source.tar.gz') == source['sha256']
    with tarfile.open(site/'downloads/vasp-render-source.tar.gz') as archive:
        for member in archive:
            parts = Path(member.name).parts
            assert not Path(member.name).is_absolute() and '..' not in parts and not member.issym() and not member.islnk()
            assert not set(parts)&{'.git','.aws','.codex','.agents','artifacts','__pycache__'}
            if member.isfile():
                relative = Path(*parts[1:]).as_posix()
                assert hashlib.sha256(archive.extractfile(member).read()).hexdigest() == source['files'][relative]
                if not args.static_only and not args.development_executable and (parts[1] == 'src' or relative == 'CMakeLists.txt'):
                    assert sha(ROOT/relative) == source['files'][relative], 'source download differs from current renderer: '+relative
    development = json.loads((site/'configuration-manifest.json').read_text())
    for name,digest in development['files'].items(): assert sha(site/name) == digest, name
    dev_source = json.loads((site/'downloads/development-source-manifest.json').read_text())
    assert sha(site/'downloads/vasp-render-development-source.tar.gz') == dev_source['sha256']
    with tarfile.open(site/'downloads/vasp-render-development-source.tar.gz') as archive:
        for member in archive:
            parts = Path(member.name).parts
            assert not Path(member.name).is_absolute() and '..' not in parts and not member.issym() and not member.islnk()
            assert not set(parts)&{'.git','.aws','.codex','.agents','artifacts','__pycache__'}
            if member.isfile():
                relative = Path(*parts[1:]).as_posix()
                assert hashlib.sha256(archive.extractfile(member).read()).hexdigest() == dev_source['files'][relative]
                if args.development_executable: assert sha(ROOT/relative) == dev_source['files'][relative], relative
    if args.development_executable:
        dev = args.development_executable.resolve()
        assert sha(dev) == development['executable_sha256']
        for kind in ['config','user','job','worker']:
            assert json.loads(subprocess.check_output([dev,'--schema',kind])) == json.loads((site/'schemas'/f'{kind}.schema.json').read_text())
        reference = ''.join(document.reference)
        assert all(flag in reference for flag in re.findall(r'--[a-z][a-z-]*',subprocess.check_output([dev,'--help'],text=True)))
    flags = set()
    if not args.static_only:
        help_text = subprocess.check_output([exe, '--help'], text=True)
        flags = set(re.findall(r'--[a-z][a-z-]*', help_text))
        reference = ''.join(document.reference)
        assert all(flag in reference for flag in flags), 'CLI flags missing from reference: '+str(flags-set(re.findall(r'--[a-z][a-z-]*', reference)))
    record = {'passed':True, 'local_links_checked':checked, 'images_with_verified_provenance':12,
              'development_images_with_verified_provenance':len(development['images']),
              'documented_cli_flags':None if args.static_only else len(flags),'static_only':args.static_only,
              'example_kit_verified':True,'validated_source_archive_verified':True,
              'source_files':len(source['files']),'source_archive_sha256':source['sha256']}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(record,indent=2)+'\n')
    print('Handbook checks passed:',checked,'local links, 12 rendered examples and downloads'+('' if args.static_only else '; CLI coverage verified'))


if __name__ == '__main__': main()
