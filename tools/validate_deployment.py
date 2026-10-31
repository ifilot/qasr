# SPDX-License-Identifier: GPL-3.0-only
"""Validate private installs, binary/source archives and an optional-feature-free build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import gzip
from benchmark import ROOT, decode_png, digest


def extract(archive,destination):
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            parts=Path(member.name).parts
            if Path(member.name).is_absolute() or '..' in parts or not (member.isdir() or member.isfile()):
                raise RuntimeError('unsafe/unexpected archive member: '+member.name)
            if set(parts)&{'.git','.aws','.codex','.agents','artifacts','__pycache__'}:
                raise RuntimeError('private/generated archive member: '+member.name)
        tar.extractall(destination,filter='data')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/m4-m6/m6/deployment/report.json')
    parser.add_argument('--jobs',default='10')
    args=parser.parse_args();build=args.build.resolve();out=args.output.resolve()
    out.parent.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix='qasr-deployment-'))
    os.environ['XDG_CONFIG_HOME']=str(work/'isolated-user-config')
    report={'started_utc':datetime.now(timezone.utc).isoformat(),'work_dir':str(work),'checks':[]}
    def save():out.write_text(json.dumps(report,indent=2)+'\n')
    save()
    def run(name,command,cwd=None):
        result=subprocess.run(list(map(str,command)),cwd=cwd,capture_output=True,text=True,timeout=600)
        log=out.parent/(name+'.log');log.write_text(result.stdout+result.stderr)
        report['checks'].append({'name':name,'command':list(map(str,command)),
                                 'returncode':result.returncode,'log':str(log)})
        save()
        if result.returncode:raise RuntimeError(f'{name} failed; see {log}')
        print(name,'passed',flush=True)
        return result.stdout
    install=work/'installed tree'
    run('install',['cmake','--install',build,'--prefix',install])
    installed=install/'bin/qasr'
    capabilities=json.loads(run('installed-build-info',[installed,'--build-info']))
    if not capabilities['openmp'] or not capabilities['libdeflate'] or capabilities['cairo']:
        raise RuntimeError('production feature configuration differs from selected defaults')
    runtime=work/'runtime case';runtime.mkdir()
    source=runtime/'CONTCAR';shutil.copyfile(ROOT/'tests/fixtures/slabs/18_pd_ceria_co_oxidation_ts/CONTCAR',source)
    flags=[source,'--labels','element','--legend','on','--axes','on','--scale-bar','on']
    installed_png=runtime/'installed.png'
    run('installed-render',[installed,*flags,'--config',install/'share/qasr/presets/illustrated.json','-o',installed_png],cwd=runtime)
    if capabilities.get('xml'):
        xml_source=runtime/'vasprun.xml'
        xml_source.write_bytes(gzip.decompress((ROOT/'tests/fixtures/vasprun.md.xml.gz').read_bytes()))
        xml_info=json.loads(run('installed-xml',[installed,xml_source,'--inspect']))
        if xml_info['format']!='xml' or xml_info['frame_count']!=10 or xml_info['atom_count']!=64:
            raise RuntimeError('installed XML inspection differs from pinned MD fixture')
    linked=run('installed-libraries',['ldd',installed])
    if 'not found' in linked:raise RuntimeError('unresolved installed shared library')
    if not (install/'share/doc/qasr/LICENSE').is_file():raise RuntimeError('installed GPL notice missing')
    if (install/'share/doc/qasr/SKILL.md').read_bytes()!=(ROOT/'SKILL.md').read_bytes():
        raise RuntimeError('installed QASR usage skill differs from source')
    if capabilities.get('ffmpeg'):
        installed_movie=runtime/'installed.mp4'
        run('installed-movie',[installed,source,'--no-user-config','--movie','orbit',
                              '--movie-frames','2','--movie-codec','mpeg4','--width','64',
                              '--height','64','--aa','native','--cell','off','-o',installed_movie])
        if not installed_movie.stat().st_size:raise RuntimeError('installed movie is empty')
    archives=out.parent/'archives';archives.mkdir(exist_ok=True)
    run('binary-package',['cpack','--config',build/'CPackConfig.cmake','-B',archives])
    binary=next(archives.glob('qasr-*-Linux-*.tar.gz'))
    binary_hash=digest(binary)
    if binary.with_name(binary.name+'.sha256').read_text().split()[0]!=binary_hash:
        raise RuntimeError('binary package checksum mismatch')
    binary_root=work/'binary-unpacked';extract(binary,binary_root)
    packaged=next(binary_root.glob('*/bin/qasr'));pkg_prefix=packaged.parents[1]
    packaged_png=runtime/'packaged.png'
    run('packaged-render',[packaged,*flags,'--config',pkg_prefix/'share/qasr/presets/illustrated.json','-o',packaged_png],cwd=runtime)
    if decode_png(installed_png)!=decode_png(packaged_png):raise RuntimeError('installed/package images differ')
    run('source-package',['python3',ROOT/'tools/package_source.py','--output',archives])
    manifest_path=next(archives.glob('*-source-manifest.json'));manifest=json.loads(manifest_path.read_text())
    archive=archives/manifest['archive']
    if digest(archive)!=manifest['sha256']:raise RuntimeError('source manifest archive checksum mismatch')
    source_root=work/'source-unpacked';extract(archive,source_root)
    unpacked=next(source_root.glob('qasr-*'))
    for relative,expected in manifest['files'].items():
        if digest(unpacked/relative)!=expected:raise RuntimeError('source file hash mismatch: '+relative)
    for required in ['src/overlay.c','src/overlay.h','tests/test_overlays.py','docs/performance-targets.json',
                     'src/movie.c','src/movie.h','src/image.c','src/image.h','src/image_io.h','src/sha256.h',
                     'src/faces.c','src/face_render.c','schemas/faces.schema.json','tests/test_movies.py',
                     f"docs/release-v{capabilities['version']}.md",'docs/polygon-faces.md','docs/movies.md',
                     'tools/check_performance.py','tools/validate_deployment.py','SKILL.md','LICENSE','THIRD_PARTY_NOTICES.md']:
        if required not in manifest['files']:raise RuntimeError('required source content missing: '+required)
    clean_build=work/'source-build'
    feature_options=[f'-DENABLE_{name.upper()}={"ON" if capabilities.get(name) else "OFF"}'
                     for name in ['openmp','libdeflate','xml','jpeg','webp','ffmpeg']]
    run('source-configure',['cmake','-S',unpacked,'-B',clean_build,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON',*feature_options])
    run('source-build',['cmake','--build',clean_build,'-j',args.jobs])
    source_cap=json.loads(run('source-build-info',[clean_build/'qasr','--build-info']))
    if source_cap!=capabilities:raise RuntimeError('fresh source build capabilities differ from installed build')
    run('source-tests',['ctest','--test-dir',clean_build,'--output-on-failure'])
    source_png=runtime/'source.png'
    run('source-render',[clean_build/'qasr',*flags,'--config',unpacked/'presets/illustrated.json','-o',source_png],cwd=runtime)
    if decode_png(source_png)!=decode_png(installed_png):raise RuntimeError('fresh source build image differs')
    minimal=work/'minimal-build'
    run('minimal-configure',['cmake','-S',unpacked,'-B',minimal,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON',
                             '-DENABLE_OPENMP=OFF','-DENABLE_LIBDEFLATE=OFF','-DENABLE_CAIRO_EXPERIMENTS=OFF','-DENABLE_XML=OFF',
                             '-DENABLE_JPEG=OFF','-DENABLE_WEBP=OFF','-DENABLE_FFMPEG=OFF'])
    run('minimal-build',['cmake','--build',minimal,'-j',args.jobs])
    run('minimal-tests',['ctest','--test-dir',minimal,'--output-on-failure'])
    minimal_cap=json.loads(run('minimal-build-info',[minimal/'qasr','--build-info']))
    if any(minimal_cap.get(key,False) for key in ['openmp','libdeflate','cairo','xml','jpeg','webp','ffmpeg']):raise RuntimeError('minimal features were not disabled')
    minimal_png=runtime/'minimal.png'
    run('minimal-render',[minimal/'qasr',*flags,'--config',unpacked/'presets/illustrated.json','-o',minimal_png],cwd=runtime)
    if decode_png(minimal_png)!=decode_png(installed_png):raise RuntimeError('minimal build changes annotation pixels')
    # User thread limits also bound the automatic policy in the production build.
    limited=subprocess.run([str(installed),str(source),'-o',str(runtime/'limited.png'),'--benchmark-stages'],
                           env={**os.environ,'OMP_NUM_THREADS':'1'},capture_output=True,text=True,check=True,timeout=60)
    if 'render_threads=1' not in limited.stderr:raise RuntimeError('automatic threads ignored OpenMP limit')
    (out.parent/'thread-limit.log').write_text(limited.stderr)
    report.update(finished_utc=datetime.now(timezone.utc).isoformat(),passed=True,
                  binary_archive=str(binary),binary_sha256=binary_hash,source_archive=str(archive),
                  source_sha256=digest(archive),source_files=len(manifest['files']),
                  installed_binary_sha256=digest(installed),packaged_binary_sha256=digest(packaged),
                  annotated_rgba_sha256=hashlib.sha256(decode_png(installed_png)[2]).hexdigest())
    save();print('Deployment validation complete:',out,flush=True)


if __name__=='__main__':main()
