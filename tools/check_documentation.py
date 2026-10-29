# SPDX-License-Identifier: GPL-3.0-only
"""Check the current QASR handbook and the immutable legacy source download."""
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import subprocess
import tarfile
from urllib.parse import unquote,urlsplit
import zipfile

ROOT=Path(__file__).resolve().parents[1]


class Document(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.ids,self.links,self.reference=[],[],[];self.in_reference=False
    def handle_starttag(self,tag,attrs):
        attrs=dict(attrs)
        if 'id' in attrs:
            self.ids.append(attrs['id'])
            if tag=='section':self.in_reference=attrs['id']=='cli-reference'
        if tag in ['a','link','script','img']:
            value=attrs.get('href') if tag in ['a','link'] else attrs.get('src')
            if value:self.links.append(value)
        for key in ['data-image','data-lightbox']:
            if key in attrs:self.links.append(attrs[key])
    def handle_endtag(self,tag):
        if tag=='section':self.in_reference=False
    def handle_data(self,data):
        if self.in_reference:self.reference.append(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path,nargs='?')
    parser.add_argument('--development-executable',type=Path,help='use this current QASR build for executable checks')
    parser.add_argument('--static-only',action='store_true')
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/website-validation/static-report.json')
    args=parser.parse_args();exe=args.development_executable or args.executable
    if not args.static_only and not exe:parser.error('provide a current qasr executable or --static-only')
    if exe:exe=exe.resolve()
    site=ROOT/'docs/website';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    documents={}
    for name in ['index.html','configuration.html','batch.html']:
        document=Document();document.feed((site/name).read_text());documents[name]=document
        assert len(document.ids)==len(set(document.ids)),name+': duplicate HTML IDs'
    checked=0
    for name,document in documents.items():
        for value in document.links:
            url=urlsplit(value)
            if url.scheme or url.netloc:continue
            if url.path:
                path=(site/unquote(url.path)).resolve()
                assert path.is_relative_to(site.resolve()) and path.is_file(),value
            if url.fragment:
                target=documents.get(unquote(url.path) if url.path else name)
                if target:assert unquote(url.fragment) in target.ids,value
            checked+=1
    for name in ['app.js','styles.css']:
        content=(site/name).read_text();assert 'https://' not in content and 'http://' not in content,'runtime network dependency'
    examples=json.loads((site/'examples-manifest.json').read_text())
    assert len(examples['images'])==12
    for record in examples['images']:
        assert sha(site/record['image'])==record['png_sha256']
        assert sha(site/'downloads'/record['source'])==record['input_sha256']
        assert Path(record['command'][0]).name=='qasr'
    for name,digest in examples['downloads'].items():assert sha(site/'downloads'/name)==digest,name
    with zipfile.ZipFile(site/'downloads/qasr-examples.zip') as z:
        for name in ['CONTCAR.co','CONTCAR.pd-ceria','POSCAR.skew','OUTCAR.relax','vasprun.example.xml','illustrated.json','publication.json','LICENSE']:
            assert z.read(name)==(site/'downloads'/name).read_bytes(),name
    source=json.loads((site/'downloads/source-manifest.json').read_text())
    def archive_check(filename,manifest,current):
        assert sha(site/'downloads'/filename)==manifest['sha256']
        found=set()
        with tarfile.open(site/'downloads'/filename) as archive:
            for member in archive:
                parts=Path(member.name).parts
                assert not Path(member.name).is_absolute() and '..' not in parts and not member.issym() and not member.islnk()
                assert not set(parts)&{'.git','.aws','.codex','.agents','artifacts','__pycache__'}
                if member.isfile():
                    relative=Path(*parts[1:]).as_posix();found.add(relative)
                    assert hashlib.sha256(archive.extractfile(member).read()).hexdigest()==manifest['files'][relative],relative
                    if current and not args.static_only:assert sha(ROOT/relative)==manifest['files'][relative],relative
        assert found==set(manifest['files'])
    archive_check('qasr-source.tar.gz',source,True)
    legacy=json.loads((site/'downloads/legacy-v0.3.0-source-manifest.json').read_text())
    archive_check('vasp-render-source.tar.gz',legacy,False)
    config=json.loads((site/'configuration-manifest.json').read_text())
    for name,digest in config['files'].items():assert sha(site/name)==digest,name
    flags=set()
    if not args.static_only:
        assert examples['executable_sha256']==config['executable_sha256']==sha(exe),'example executable differs'
        assert subprocess.check_output([exe,'--version'],text=True).startswith('qasr ')
        expected=json.loads(subprocess.check_output([exe,site/'downloads/vasprun.example.xml','--inspect','--no-user-config']))
        assert expected==json.loads((site/'downloads/inspect-example.json').read_text())
        for kind in ['config','user','job','worker']:
            assert json.loads(subprocess.check_output([exe,'--schema',kind]))==json.loads((site/'schemas'/f'{kind}.schema.json').read_text())
        flags=set(re.findall(r'--[a-z][a-z-]*',subprocess.check_output([exe,'--help'],text=True)))
        assert all(flag in ''.join(documents['index.html'].reference) for flag in flags),'undocumented CLI flag'
    report={'passed':True,'local_links_checked':checked,'images_with_verified_provenance':12,'styling_images':len(config['images']),
            'documented_cli_flags':None if args.static_only else len(flags),'static_only':args.static_only,'source_files':len(source['files']),
            'source_archive_sha256':source['sha256'],'legacy_v030_archive_verified':True}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print('QASR handbook checks passed:',checked,'local links, 14 images, current and legacy archives')


if __name__=='__main__':main()
