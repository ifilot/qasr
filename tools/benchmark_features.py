# SPDX-License-Identifier: GPL-3.0-only
"""Exact-output, interleaved benchmarks for faces, cells, movies and still controls."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile

from benchmark import ROOT, digest, environment
from benchmark_iteration import cli_cases, comparison, run


def cases(work):
    zeolite=ROOT/'examples/zeolite';cells=ROOT/'examples/cell-lines'
    result=[]
    for name,source,recipe in [
        ('cage-faces','cage.xyz','02-lta-polygon-tiles'),
        ('four-cages-faces','cages-2x2.xyz','09-repeated-tiles'),
        ('single-face','ring.xyz','03-single-face-translucent'),
        ('opaque-face-labels','ring.xyz','04-single-face-opaque'),
        ('half-bonds','cage.xyz','01-lta-framework')]:
        for threads in [1,8]:
            flags=['--config',zeolite/'renders'/(recipe+'.json'),'--width','640','--height','640','--threads',str(threads)]
            if 'labels' in name:flags+=['--labels','element']
            result.append((name+'-t'+str(threads),zeolite/source,flags,'png'))
    co=ROOT/'tests/fixtures/slabs/01_co0001_clean/CONTCAR'
    for recipe in ['02-complete-endpoint-bonds','03-dashed-cell','05-dashed-white-contour']:
        for threads in [1,8]:
            result.append((recipe+'-t'+str(threads),co,['--config',cells/(recipe+'.json'),'--width','768','--height','768','--threads',str(threads)],'png'))
    alpha=work/'alpha-faces.json'
    config=json.loads((zeolite/'renders/03-single-face-translucent.json').read_text())
    config['faces_file']=str(zeolite/'single-face.json');config['image'].update(background='#476c9100',width=512,height=512)
    config['elements']['Si']['color']='#70a9d280';alpha.write_text(json.dumps(config))
    result.append(('partial-alpha-faces',zeolite/'ring.xyz',['--config',alpha,'--threads','1'],'png'))
    movie=['--width','384','--height','384','--movie','orbit','--movie-frames','24','--movie-fps','24','--movie-codec','h264']
    for name,recipe in [('orbit-faces','02-lta-polygon-tiles'),('orbit-half-bonds','01-lta-framework')]:
        for threads in [1,8]:
            result.append((name+'-t'+str(threads),zeolite/'cage.xyz',['--config',zeolite/'renders'/(recipe+'.json'),*movie,'--threads',str(threads)],'mp4'))
    for interpolated in [0,3]:
        result.append(('reaction-k'+str(interpolated),ROOT/'examples/movies/reaction.xyz',
                       ['--config',ROOT/'examples/movies/reaction.json','--movie','trajectory','--movie-interpolate',str(interpolated),'--movie-codec','h264','--threads','1'],'mp4'))
    result.append(('orbit-simple-1024',ROOT/'examples/movies/reaction.xyz',
                   ['--movie','orbit','--movie-frames','16','--movie-codec','h264','--width','1024','--height','1024','--aa','2x','--cell','off','--threads','1'],'mp4'))
    # Substantial source trajectories expose observer selection/copying, while
    # fixed low output resolution keeps their rendering/encoding inexpensive.
    for fmt,count,nframes in [('xyz',512,20),('xml',128,64),('outcar',128,64)]:
        positions=[((i%8)*1.6,(i//8%8)*1.6,(i//64)*1.6) for i in range(count)]
        if fmt=='xyz':
            text=''.join(f'{count}\nSynthetic moving cluster\n'+''.join(f'C {x+.04*math.sin(j/8+i):.9f} {y:.9f} {z:.9f}\n' for i,(x,y,z) in enumerate(positions)) for j in range(nframes))
        elif fmt=='xml':
            text=f'<modeling><atominfo><atoms>{count}</atoms><types>1</types><array name="atoms"><set>'+('<rc><c>C</c><c>1</c></rc>'*count)+f'</set></array><array name="atomtypes"><set><rc><c>{count}</c><c>C</c></rc></set></array></atominfo>'
            for j in range(nframes):
                text+='<calculation><structure><crystal><varray name="basis"><v>20 0 0</v><v>0 20 0</v><v>0 0 20</v></varray></crystal><varray name="positions">'+''.join(f'<v>{(x+.04*math.sin(j/8+i))/20:.12f} {y/20:.12f} {z/20:.12f}</v>' for i,(x,y,z) in enumerate(positions))+'</varray></structure></calculation>\n'
            text+='</modeling>'
        else:
            text=f'vasp.6.4.3 synthetic benchmark\n VRHFIN =C: s2p2\n ions per type = {count}\n NIONS = {count}\n direct lattice vectors reciprocal lattice vectors\n20 0 0 .05 0 0\n0 20 0 0 .05 0\n0 0 20 0 0 .05\n'
            for j in range(nframes):
                text+=' POSITION TOTAL-FORCE (eV/Angst)\n --------------------------\n'+''.join(f'{x+.04*math.sin(j/8+i):.9f} {y:.9f} {z:.9f} 0 0 0\n' for i,(x,y,z) in enumerate(positions))+' --------------------------\n'
            text+=' General timing and accounting informations for this job:\n Total CPU time used (sec): 1.0\n Voluntary context switches: 1\n'
        source=work/('trajectory.'+fmt);source.write_text(text)
        for interpolation in ([0,3] if fmt=='xyz' else [0]):
            result.append((f'trajectory-{fmt}-k{interpolation}',source,['--movie','trajectory','--movie-interpolate',str(interpolation),'--movie-codec','mpeg4','--width','128','--height','128','--aa','native','--cell','off','--threads','1'],'mp4'))
    controls={'tiny-t1','dense-t1','dense-t8','dense4096-t1','dense4096-t8','annotated-t1','annotated-t8','ortep-dualtone-t1','balsac-dualtone-t1','alpha-adaptive-t8','libpng','jpg','webp'}
    result.extend(c for c in cli_cases(work) if c[0] in controls)
    return result


def movie_oracle(path):
    probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_streams','-show_frames','-of','json',path]))
    # Framehash uses a lossless raw RGBA decode; timestamps and frame inventory
    # are independently retained by ffprobe, outside the measured interval.
    frames=subprocess.check_output(['ffmpeg','-v','error','-i',path,'-map','0:v:0','-pix_fmt','rgba','-threads','1','-f','framehash','-hash','sha256','pipe:1'])
    return {'probe':probe,'decoded_framehash':frames.decode()}


def measure(executables,cases,args,work,report,save):
    for name,source,flags,suffix in cases:
        output=work/('output.'+suffix);sidecar=work/'report.json'
        commands={label:[exe,source,'--no-user-config','--benchmark-stages',*flags,'--png-compression','1','-o',output,*(['--report',sidecar] if suffix=='mp4' else [])] for label,exe in executables.items()}
        expected=None;expected_report=None;retained={};oracles={}
        def sample(label):
            nonlocal expected,expected_report
            proc,record=run(commands[label]);sha=digest(output)
            if expected is None:expected=sha
            if sha!=expected:raise ValueError(name+': exact encoded output changed in '+label)
            if suffix=='mp4':
                data=json.loads(sidecar.read_text());record['stages_ms']=data.pop('timings_ms')
                if expected_report is None:expected_report=data
                if data!=expected_report:raise ValueError(name+': camera/settings/report changed in '+label)
                if label not in retained:
                    (args.output.parent/(name+'-'+label+'.report.json')).write_text(json.dumps(data,indent=2)+'\n')
            if label not in retained:
                dest=args.output.parent/(name+'-'+label+'.'+suffix);shutil.copyfile(output,dest);retained[label]=dest
            record.update(output_sha256=sha,output_bytes=output.stat().st_size)
            return record
        for _ in range(args.warmups):
            for label in executables:sample(label)
        orders=[list(executables)[::(-1 if i%2 else 1)] for i in range(args.samples)];random.Random(args.seed).shuffle(orders)
        rows=[]
        for order in orders:
            row={'order':order}
            for label in order:row[label]=sample(label)
            rows.append(row)
        if suffix=='mp4':
            for label in executables:oracles[label]=movie_oracle(retained[label])
            if oracles['baseline']!=oracles['candidate']:raise ValueError(name+': decoded movie/timestamps changed')
        else:
            scenes=[run([exe,source,'--no-user-config',*flags,'--inspect-scene'])[0].stdout for exe in executables.values()]
            if scenes[0]!=scenes[1]:raise ValueError(name+': scene changed')
            oracles={'scene_sha256':hashlib.sha256(scenes[0]).hexdigest()}
        entry={'name':name,'commands':{k:list(map(str,v)) for k,v in commands.items()},'input_sha256':digest(source),
               'samples':rows,'comparison':comparison(rows),'exact_output':True,'oracle':oracles,'peak_rss_KiB':{}}
        for label,command in commands.items():
            rss=[]
            for i in range(args.memory_samples):
                memory=work/'rss.txt';subprocess.run(['/usr/bin/time','-f','%M','-o',str(memory),*map(str,command)],check=True,capture_output=True)
                rss.append(int(memory.read_text()))
            entry['peak_rss_KiB'][label]=rss
        report['cases'].append(entry);save()
        c=entry['comparison'];print(name,round(c['baseline_ms']['median'],2),round(c['candidate_ms']['median'],2),round(c['candidate_over_baseline'],3),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('baseline',type=Path);p.add_argument('candidate',type=Path)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--samples',type=int,default=30);p.add_argument('--warmups',type=int,default=3)
    p.add_argument('--seed',type=int,default=1729);p.add_argument('--memory-samples',type=int,default=3);p.add_argument('--cases',nargs='+');p.add_argument('--work-dir',type=Path)
    args=p.parse_args()
    if args.samples<2 or args.warmups<1 or args.memory_samples<1:p.error('require at least two pairs, one warmup and one memory sample')
    exes={'baseline':args.baseline.resolve(),'candidate':args.candidate.resolve()}
    executable_filesystems={k:subprocess.check_output(['stat','-f','-c','%T',v],text=True).strip() for k,v in exes.items()}
    if len(set(executable_filesystems.values()))!=1:
        p.error('Run both executables from the same filesystem type; mixed mounts confound startup timing')
    work=(args.work_dir or Path(tempfile.mkdtemp(prefix='qasr-features-'))).resolve();work.mkdir(parents=True,exist_ok=True)
    os.environ['XDG_CONFIG_HOME']=str(work/'isolated-user-config');args.output.parent.mkdir(parents=True,exist_ok=True)
    selected=cases(work)
    if args.cases:
        missing=set(args.cases)-{c[0] for c in selected}
        if missing:p.error('Unknown cases: '+str(missing))
        selected=[c for c in selected if c[0] in args.cases]
    source_hashes={str(path.relative_to(ROOT)):digest(path) for folder in ['src','tools','tests','schemas','presets','examples'] for path in sorted((ROOT/folder).rglob('*')) if path.is_file() and '__pycache__' not in path.parts}
    source_hashes['CMakeLists.txt']=digest(ROOT/'CMakeLists.txt')
    report={'started_utc':datetime.now(timezone.utc).isoformat(),'passed':False,'samples':args.samples,'warmups':args.warmups,'seed':args.seed,
            'scope':'Warm, balanced shuffled AB/BA pairs; CLI startup/output/exit included; verification excluded. Host load/frequency uncontrolled.',
            'source_hashes_including_untracked':source_hashes,'environment':{k:environment(v) for k,v in exes.items()},
            'executable_filesystems':executable_filesystems,
            'driver_sha256':digest(Path(__file__)),'work_dir':str(work),'expected_cases':[c[0] for c in selected],'cases':[]}
    report['build_capabilities']={k:json.loads(subprocess.check_output([exe,'--build-info'])) for k,exe in exes.items()}
    report['codec_library_versions']={name:subprocess.check_output(['pkg-config','--modversion',name],text=True).strip() for name in ['libavcodec','libavformat','libavutil','libswscale','libjpeg','libwebp']}
    def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
    save();shutil.copyfile(__file__,args.output.with_suffix('.driver.py'))
    measure(exes,selected,args,work,report,save)
    assert report['expected_cases']==[c['name'] for c in report['cases']]
    report.update(passed=True,finished_utc=datetime.now(timezone.utc).isoformat());save()


if __name__=='__main__':main()
