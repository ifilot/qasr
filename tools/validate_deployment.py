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
    installed=install/'bin/vasp-render'
    capabilities=json.loads(run('installed-build-info',[installed,'--build-info']))
    if not capabilities['openmp'] or not capabilities['libdeflate'] or capabilities['cairo']:
        raise RuntimeError('production feature configuration differs from selected defaults')
    runtime=work/'runtime case';runtime.mkdir()
    source=runtime/'CONTCAR';shutil.copyfile(ROOT/'tests/fixtures/slabs/18_pd_ceria_co_oxidation_ts/CONTCAR',source)
    flags=[source,'--labels','element','--legend','on','--axes','on','--scale-bar','on']
    installed_png=runtime/'installed.png'
    run('installed-render',[installed,*flags,'--config',install/'share/vasp-render/presets/illustrated.json','-o',installed_png],cwd=runtime)
    linked=run('installed-libraries',['ldd',installed])
    if 'not found' in linked:raise RuntimeError('unresolved installed shared library')
    if not (install/'share/doc/vasp_render/LICENSE').is_file():raise RuntimeError('installed GPL notice missing')
    archives=out.parent/'archives';archives.mkdir(exist_ok=True)
    run('binary-package',['cpack','--config',build/'CPackConfig.cmake','-B',archives])
    binary=next(archives.glob('vasp-render-*-Linux-*.tar.gz'))
    binary_hash=digest(binary)
    if binary.with_name(binary.name+'.sha256').read_text().split()[0]!=binary_hash:
        raise RuntimeError('binary package checksum mismatch')
    binary_root=work/'binary-unpacked';extract(binary,binary_root)
    packaged=next(binary_root.glob('*/bin/vasp-render'));pkg_prefix=packaged.parents[1]
    packaged_png=runtime/'packaged.png'
    run('packaged-render',[packaged,*flags,'--config',pkg_prefix/'share/vasp-render/presets/illustrated.json','-o',packaged_png],cwd=runtime)
    if decode_png(installed_png)!=decode_png(packaged_png):raise RuntimeError('installed/package images differ')
    run('source-package',['python3',ROOT/'tools/package_source.py','--output',archives])
    manifest_path=next(archives.glob('*-source-manifest.json'));manifest=json.loads(manifest_path.read_text())
    archive=archives/manifest['archive']
    if digest(archive)!=manifest['sha256']:raise RuntimeError('source manifest archive checksum mismatch')
    source_root=work/'source-unpacked';extract(archive,source_root)
    unpacked=next(source_root.glob('vasp-render-*'))
    for relative,expected in manifest['files'].items():
        if digest(unpacked/relative)!=expected:raise RuntimeError('source file hash mismatch: '+relative)
    for required in ['src/overlay.c','src/overlay.h','tests/test_overlays.py','docs/performance-targets.json',
                     'tools/check_performance.py','tools/validate_deployment.py','LICENSE','THIRD_PARTY_NOTICES.md']:
        if required not in manifest['files']:raise RuntimeError('required source content missing: '+required)
    clean_build=work/'source-build'
    run('source-configure',['cmake','-S',unpacked,'-B',clean_build,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON'])
    run('source-build',['cmake','--build',clean_build,'-j',args.jobs])
    run('source-tests',['ctest','--test-dir',clean_build,'--output-on-failure'])
    source_png=runtime/'source.png'
    run('source-render',[clean_build/'vasp-render',*flags,'--config',unpacked/'presets/illustrated.json','-o',source_png],cwd=runtime)
    if decode_png(source_png)!=decode_png(installed_png):raise RuntimeError('fresh source build image differs')
    minimal=work/'minimal-build'
    run('minimal-configure',['cmake','-S',unpacked,'-B',minimal,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON',
                             '-DENABLE_OPENMP=OFF','-DENABLE_LIBDEFLATE=OFF','-DENABLE_CAIRO_EXPERIMENTS=OFF'])
    run('minimal-build',['cmake','--build',minimal,'-j',args.jobs])
    run('minimal-tests',['ctest','--test-dir',minimal,'--output-on-failure'])
    minimal_cap=json.loads(run('minimal-build-info',[minimal/'vasp-render','--build-info']))
    if any(minimal_cap[key] for key in ['openmp','libdeflate','cairo']):raise RuntimeError('minimal features were not disabled')
    minimal_png=runtime/'minimal.png'
    run('minimal-render',[minimal/'vasp-render',*flags,'--config',unpacked/'presets/illustrated.json','-o',minimal_png],cwd=runtime)
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
