# SPDX-License-Identifier: GPL-3.0-only
"""Generate deterministic, original POSCAR/XML workloads and M10 request plans."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil

GENERATOR_VERSION = 1
PROFILES = {
    'smoke': dict(atoms=32, frames=4, structures=4, variants=4, image_size=128),
    'standard': dict(atoms=512, frames=128, structures=12, variants=12, image_size=512),
    'stress': dict(atoms=4096, frames=512, structures=24, variants=24, image_size=768),
}


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(65536),b''): h.update(chunk)
    return h.hexdigest()


def number(value):
    return f'{value:.12f}'


def row(values):
    return ' '.join(number(v) for v in values)


def rounded(values):
    return [float(number(v)) for v in values]


def shape_for(count, slab=False):
    if slab:
        z=min(3,count);x=math.isqrt((count+z-1)//z)
        if x*x*z<count:x+=1
        return x,x,z
    x=1
    while x*x*x<count:x+=1
    return x,x,x


def geometry(count, kind, seed):
    if kind=='boundary':
        return {'basis':[[4.,0.,0.],[0.,4.,0.],[0.,0.,4.]],'elements':['H','H'],
                'fractional':[[-.025,.5,.5],[1.025,.5,.5]],'shape':[1,1,1]}
    slab=kind=='slab';shape=shape_for(count,slab);spacing=2.6 if slab else 1.8
    lengths=[spacing*n for n in shape]
    if slab:lengths[2]+=14
    basis=[[lengths[0],0.,0.],[.24*lengths[0] if kind=='skew' else 0.,lengths[1],0.],
           [.11*lengths[0] if kind=='skew' else 0.,.13*lengths[1] if kind=='skew' else 0.,lengths[2]]]
    atoms=[]
    for i in range(count):
        x=i%shape[0];y=(i//shape[0])%shape[1];z=i//(shape[0]*shape[1])
        # Integer hash, avoiding global RNG state or a third-party package.
        key=hashlib.sha256(f'{seed}:{i}'.encode()).digest()
        p=[(a+.5+(key[d]/255-.5)*.04)/shape[d] for d,a in enumerate([x,y,z])]
        if slab:p[2]=(7+spacing*(z+.5))/lengths[2]
        element='Co' if slab else 'Si' if (x+y+z)%2==0 else 'O'
        atoms.append((element,rounded(p)))
    # POSCAR requires contiguous species blocks. XML uses this same source order.
    atoms.sort(key=lambda item:0 if item[0] in ('Si','Co') else 1)
    return dict(basis=[rounded(v) for v in basis],elements=[a[0] for a in atoms],fractional=[a[1] for a in atoms],shape=list(shape))


def positions(g, frame=None):
    if frame is None:return g['fractional']
    def triangle(v):
        v%=34
        return v if v<=17 else 34-v
    return [rounded([p[d]+.003*(triangle(frame*(d+1)+i*7)-triangle(i*7))/g['shape'][d]
                     for d in range(3)]) for i,p in enumerate(g['fractional'])]


def poscar(g, frame=None):
    symbols=list(dict.fromkeys(g['elements']))
    return '\n'.join(['Original synthetic geometry; not a physical simulation','1',
        *[row(v) for v in g['basis']],' '.join(symbols),
        ' '.join(str(g['elements'].count(s)) for s in symbols),'Direct',
        *[row(v) for v in positions(g,frame)],''])


def write_xml(path,g,frames):
    symbols=list(dict.fromkeys(g['elements']))
    with path.open('w',encoding='utf-8',newline='\n') as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<modeling>\n<atominfo>')
        f.write(f'<atoms>{len(g["elements"])}</atoms><types>{len(symbols)}</types>\n')
        f.write('<array name="atoms"><set>\n')
        for element in g['elements']:f.write(f'<rc><c>{element}</c><c>{symbols.index(element)+1}</c></rc>\n')
        f.write('</set></array><array name="atomtypes"><set>\n')
        for element in symbols:f.write(f'<rc><c>{g["elements"].count(element)}</c><c>{element}</c></rc>\n')
        f.write('</set></array></atominfo>\n')
        def snapshot(frame,name=''):
            f.write(f'<structure name="{name}">\n<crystal><varray name="basis">\n')
            for v in g['basis']:f.write('<v>'+row(v)+'</v>\n')
            f.write('</varray></crystal><varray name="positions">\n')
            for v in positions(g,frame):f.write('<v>'+row(v)+'</v>\n')
            f.write('</varray></structure>\n')
        snapshot(0,'initialpos')
        for frame in range(frames):
            f.write('<calculation>\n');snapshot(frame);f.write('</calculation>\n')
        f.write('</modeling>\n')


def oracle(g,frame=None):
    return {'atoms':len(g['elements']),'elements':g['elements'],'lattice_A':g['basis'],
            'positions_A':[[sum(p[d]*g['basis'][d][axis] for d in range(3)) for axis in range(3)] for p in positions(g,frame)]}


def generate(output,profile='standard',seed=1729,**overrides):
    settings=dict(PROFILES[profile]);settings.update({k:v for k,v in overrides.items() if v is not None})
    limits={'atoms':(4,10000),'frames':(2,4096),'structures':(2,128),'variants':(2,128),'image_size':(64,2048)}
    for key,(low,high) in limits.items():
        value=settings[key]
        if not isinstance(value,int) or not low<=value<=high:raise ValueError(f'{key} must be within [{low},{high}]')
    if not isinstance(seed,int) or not 0<=seed<=2**32-1:raise ValueError('seed must be a 32-bit unsigned integer')
    # Conservative byte/work guard before creating anything (stream XML to disk).
    estimated=settings['atoms']*(settings['frames']*90+settings['structures']*300+settings['variants']*500)
    if estimated>512*1024*1024:raise ValueError('estimated generated dataset exceeds 512 MiB; reduce counts')
    output=Path(output).absolute();output.parent.mkdir(parents=True,exist_ok=True)
    output.mkdir()  # Deliberately refuse existing destinations, including symlinks.
    try:
        for folder in ['inputs','oracles','plans']:(output/folder).mkdir()
        inputs={};plans={};geometries={}
        def save_json(path,value):
            (output/path).write_text(json.dumps(value,indent=2,sort_keys=True)+'\n',encoding='utf-8')
        def add(name,g,frame=None):
            path='inputs/'+name
            (output/path).write_text(poscar(g,frame),encoding='utf-8')
            op='oracles/'+name+'.json';save_json(op,oracle(g,frame))
            inputs[path]={'atoms':len(g['elements']),'frames':1,'format':'poscar','oracle':op}
            geometries[path]=g
            return path
        n=settings['atoms']
        bulk=add('POSCAR.bulk',geometry(n,'bulk',seed))
        skew=add('POSCAR.skew',geometry(n,'skew',seed+1))
        slab=add('POSCAR.slab',geometry(n,'slab',seed+2))
        boundary=add('POSCAR.boundary',geometry(2,'boundary',seed))
        independent=[]
        for i in range(settings['structures']):
            count=max(4,n//(1+i%3));kind=['bulk','skew','slab'][i%3]
            independent.append(add(f'POSCAR.independent-{i:03}',geometry(count,kind,seed+100+i)))
        trajectory='inputs/vasprun.synthetic.xml';g=geometries[skew];frames=settings['frames']
        write_xml(output/trajectory,g,frames)
        frame_oracles={}
        for frame in sorted({0,frames//2,frames-1}):
            name=f'oracles/frame-{frame:04}.json';save_json(name,oracle(g,frame));frame_oracles[str(frame)]=name
            add(f'POSCAR.frame-{frame:04}',g,frame)
        inputs[trajectory]={'atoms':n,'frames':frames,'format':'xml','frame_oracles':frame_oracles}
        # Same-length variants make same-size, timestamp-preserving invalidation testable.
        altered=json.loads(json.dumps(geometries[bulk]));altered['fractional'][0][0]+=.125/altered['shape'][0];altered['basis'][0][0]+=.125
        replacement=add('POSCAR.replacement',altered)
        (output/'inputs/POSCAR.truncated').write_text('\n'.join(poscar(geometry(4,'bulk',seed)).splitlines()[:-2])+'\n',encoding='utf-8')
        inputs['inputs/POSCAR.truncated']={'expected_exit':4,'format':'poscar'}
        def request(family,index,source,config=None,frame='final',expected_exit=0,requires=None,equivalent=None):
            identifier=f'{family}-{index:03}'
            job={'schema_version':1,'input':source,'output':f'outputs/{identifier}.png','report':'-',
                 'threads':1,'no_user_config':True,'json_errors':True,'frame':frame,
                 'config':{'image':{'width':settings['image_size'],'height':settings['image_size'],'png_encoder':'libpng','png_compression':1}}}
            for group,value in (config or {}).items():
                if isinstance(value,dict):job['config'].setdefault(group,{}).update(value)
                else:job['config'][group]=value
            expected={'exit_code':expected_exit}
            if not expected_exit:expected.update(atoms=inputs[source]['atoms'],frame=inputs[source]['frames']-1 if frame=='final' else frame)
            if equivalent is not None:expected['same_pixels_as']=equivalent
            return {'id':identifier,'action':'render','requires':requires or [],'job':job,'expected':expected}
        plans['independent']=[request('independent',i,p) for i,p in enumerate(independent)]
        views=[[1,1,1],[1,0,1],[0,1,1],[-1,1,1],'top',[1,-1,2]]
        plans['views']=[request('views',i,skew,{'camera':{'view':views[i%len(views)]}}) for i in range(settings['variants'])]
        plans['styles']=[]
        for i in range(settings['variants']):
            cfg={'image':{'style':'illustrated' if i%2 else 'shaded','background':'#ffffff00' if i%3==0 else '#ffffff'},
                 'geometry':{'sphere_scale':.7+.1*(i%4)},
                 'atom_overrides':[{'select':{'source_indices':[i%n]},'color':['#ee6633','#18a88b','#6644dd'][i%3],'radius_A':.7}]}
            plans['styles'].append(request('styles',i,bulk,cfg))
        plans['frames']=[request('frames',i,trajectory,frame=frame,requires=['xml']) for i,frame in enumerate(sorted({0,frames//2,frames-1}))]
        # Repeat frame 0 after other frames: catches a cache keyed only by filename.
        plans['frames'].append(request('frames',3,trajectory,frame=0,requires=['xml'],equivalent='frames-000'))
        plans['frames'].append(request('frames',4,trajectory,frame=frames,requires=['xml'],expected_exit=4))
        plans['frames'].append(request('frames',5,trajectory,frame=0,requires=['xml'],equivalent='frames-000'))
        plans['periodic']=[request('periodic',i,source,{'geometry':{'expand_xy':[1,1],'image_colors':mode},'camera':{'view':'top'}})
                           for i,(source,mode) in enumerate([(slab,'muted'),(slab,'greyscale'),(boundary,'normal')])]
        base=request('isolation',0,bulk)
        plans['isolation']=[base,request('isolation',1,bulk,{'image':{'width':settings['image_size']+16,'style':'illustrated'},'geometry':{'repeat':[2,1,1]},'overlays':{'cell':False}}),
            request('isolation',2,'inputs/POSCAR.truncated',expected_exit=4),
            request('isolation',3,bulk,{'atom_overrides':[{'select':{'source_indices':[n]},'color':'#ff0000'}]},expected_exit=2),
            request('isolation',4,bulk,equivalent=base['id'])]
        # The driver substitutes a private active path; the source corpus stays immutable.
        first=request('invalidation',0,bulk);first['active_input']='same-path/POSCAR'
        second=request('invalidation',1,replacement);second['active_input']='same-path/POSCAR';second['expected']['different_pixels_from']=first['id']
        third=request('invalidation',2,bulk,equivalent=first['id']);third['active_input']='same-path/POSCAR'
        plans['invalidation']=[first,second,third]
        for name,entries in plans.items():
            (output/f'plans/{name}.jsonl').write_text(''.join(json.dumps(v,sort_keys=True,separators=(',',':'))+'\n' for v in entries),encoding='utf-8')
        files={str(p.relative_to(output)):{'sha256':digest(p),'bytes':p.stat().st_size} for p in sorted(output.rglob('*')) if p.is_file()}
        manifest={'schema_version':1,'generator_version':GENERATOR_VERSION,'generator_sha256':digest(Path(__file__)),'seed':seed,'profile':profile,'settings':settings,
                  'provenance':'Original algorithmic fixtures, GPL-3.0-only; no downloaded data or physical/DFT validity claim.',
                  'path_base':'suite root; plans are driver envelopes, not native renderer JSONL',
                  'inputs':inputs,'plans':{name:{'path':f'plans/{name}.jsonl','requests':len(entries)} for name,entries in plans.items()},'files':files}
        save_json('manifest.json',manifest)
        (output/'README.txt').write_text('Synthetic M10 benchmark corpus. See manifest.json for parameters, hashes and independent geometry oracles.\nPlans contain driver envelopes; use tools/run_m10_workloads.py with the current single-job executable.\nNo physical simulation or real VASP execution produced these inputs.\n',encoding='utf-8')
        return manifest
    except BaseException:
        shutil.rmtree(output)
        raise


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True,help='new directory; never overwrite an existing dataset')
    p.add_argument('--profile',choices=PROFILES,default='standard');p.add_argument('--seed',type=int,default=1729)
    for key in ['atoms','frames','structures','variants','image_size']:p.add_argument('--'+key.replace('_','-'),type=int)
    args=vars(p.parse_args());output=args.pop('output');profile=args.pop('profile');seed=args.pop('seed')
    try:manifest=generate(output,profile,seed,**args)
    except (ValueError,OSError) as e:p.error(str(e))
    print(f'Generated {len(manifest["inputs"])} inputs and {sum(v["requests"] for v in manifest["plans"].values())} requests in {output}')


if __name__=='__main__':main()
