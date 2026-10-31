# SPDX-License-Identifier: GPL-3.0-only
"""Generate reproducible LTA polygon/half-bond examples from the included IZA CIF."""
import argparse
import copy
import hashlib
import itertools
import json
import math
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
DATA=ROOT/'examples/zeolite'


def dot(a,b):return sum(x*y for x,y in zip(a,b))
def minus(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def unit(a):return tuple(x/math.sqrt(dot(a,a)) for x in a)
def write(path,data):path.write_text(json.dumps(data,indent=2)+'\n')
def point_key(p):return tuple(round(v,8) for v in p)
def cycle_key(indices):
    return min(tuple(seq[i:]+seq[:i]) for seq in [indices,list(reversed(indices))] for i in range(len(seq)))


def bridge(a,b,oxygen,length):
    """Find the actual IZA oxygen shared by these neighboring silicon images."""
    middle=tuple((x+y)/2 for x,y in zip(a,b));matches=[]
    for i,p in enumerate(oxygen):
        offset=tuple(round(middle[d]-p[d]) for d in range(3))
        position=point_key(tuple(p[d]+offset[d] for d in range(3)))
        if all(math.sqrt(dot(minus(position,v),minus(position,v)))*length<1.8 for v in [a,b]):
            matches.append((i,offset,position))
    assert len(matches)==1,('expected one bridging O',a,b,matches)
    return matches[0]


def include_oxygen(face,si,oxygen,length):
    indices,offsets=[],[]
    vertices=[point_key(tuple(si[i][d]+offset[d] for d in range(3))) for i,offset in zip(face['atom_indices'],face['cell_offsets'])]
    for k,(a,b) in enumerate(zip(vertices,vertices[1:]+vertices[:1])):
        i,offset,_=bridge(a,b,oxygen,length)
        indices.extend([face['atom_indices'][k],len(si)+i]);offsets.extend([face['cell_offsets'][k],offset])
    return dict(face,atom_indices=indices,cell_offsets=offsets)


def cage_data(si,oxygen,templates,length):
    # Include the atoms on both sides of each periodic boundary. The primitive
    # cell stores only 24 Si; the complete alpha-cage boundary has 48 Si images.
    silicon=sorted({point_key(tuple(p[d]+shift[d] for d in range(3))) for p in si
                    for shift in itertools.product([-1,0,1],repeat=3)
                    if all(-1e-8<=p[d]+shift[d]<=1+1e-8 for d in range(3))})
    assert len(silicon)==48
    lookup=set(silicon);chosen={}
    for template in templates:
        for shift in itertools.product([-1,0,1],repeat=3):
            offsets=[tuple(offset[d]+shift[d] for d in range(3)) for offset in template['cell_offsets']]
            vertices=[point_key(tuple(si[i][d]+offset[d] for d in range(3))) for i,offset in zip(template['atom_indices'],offsets)]
            if not all(p in lookup for p in vertices):continue
            normal=cross(minus(vertices[1],vertices[0]),minus(vertices[2],vertices[0]))
            if dot(normal,normal)<1e-20:continue
            normal=unit(normal)
            if any(abs(dot(normal,minus(p,vertices[0])))>1e-6 for p in vertices):continue
            distances=[dot(normal,minus(p,vertices[0])) for p in silicon]
            if min(distances)<-1e-6 and max(distances)>1e-6:continue
            chosen[tuple(sorted(vertices))]=dict(template,cell_offsets=offsets)
    skeleton=list(chosen.values())
    assert len(skeleton)==26
    assert {n:sum(len(f['atom_indices'])==n for f in skeleton) for n in [4,6,8]}=={4:12,6:8,8:6}
    periodic=[include_oxygen(f,si,oxygen,length) for f in skeleton]
    base=si+oxygen
    boundaries=[[(('Si' if i<len(si) else 'O'),point_key(tuple(base[i][d]+offset[d] for d in range(3))))
                 for i,offset in zip(f['atom_indices'],f['cell_offsets'])] for f in periodic]
    oxygens=sorted({p for boundary in boundaries for element,p in boundary if element=='O'})
    assert len(oxygens)==72

    def export(name,shifts,face_file):
        atoms={element:sorted({point_key(tuple(p[d]+shift[d] for d in range(3))) for p in points for shift in shifts})
               for element,points in [('Si',silicon),('O',oxygens)]}
        ordered=[(element,p) for element in ['Si','O'] for p in atoms[element]]
        index={atom:i for i,atom in enumerate(ordered)}
        text=f'{len(ordered)}\nComplete LTA alpha-cage boundaries from periodic IZA coordinates\n'
        text+=''.join(element+' '+' '.join(f'{(v-.5)*length:.10f}' for v in p)+'\n' for element,p in ordered)
        (DATA/name).write_text(text)
        faces={}
        for shift in shifts:
            for f,boundary in zip(periodic,boundaries):
                ids=[index[(element,point_key(tuple(p[d]+shift[d] for d in range(3))))] for element,p in boundary]
                faces[cycle_key(ids)]={'atom_indices':ids,'repeat':False,'color':f['color'],
                                       'outline_width':f['outline_width'],'outline_color':f['outline_color']}
        write(DATA/face_file,{'schema_version':1,'faces':list(faces.values())})
        return ordered,list(faces.values())

    atoms,faces=export('cage.xyz',[(0,0,0)],'tiles.json')
    edges={}
    for f in faces:
        ids=f['atom_indices']
        for a,b in zip(ids,ids[1:]+ids[:1]):
            assert atoms[a][0]!=atoms[b][0]
            assert math.sqrt(dot(minus(atoms[a][1],atoms[b][1]),minus(atoms[a][1],atoms[b][1])))*length<1.8
            key=tuple(sorted([a,b]));edges[key]=edges.get(key,0)+1
    assert len(edges)==144 and set(edges.values())=={2}
    assert len(atoms)-len(edges)+len(faces)==2
    degree=[0]*len(atoms)
    for a,b in edges:degree[a]+=1;degree[b]+=1
    assert all(degree[i]==(3 if element=='Si' else 2) for i,(element,_) in enumerate(atoms))
    assert {i for edge in edges for i in edge}==set(range(len(atoms)))
    write(DATA/'tiles-periodic.json',{'schema_version':1,'faces':periodic})
    cluster,cluster_faces=export('cages-2x2.xyz',list(itertools.product([0,1],[0,1],[0])),'repeated-tiles.json')
    stats={'silicon':48,'oxygen':72,'atoms':120,'Si_edges':72,'SiO_edges':144,'faces':26,
           'Si_ring_counts':{'4':12,'6':8,'8':6},'polygon_vertices':{'8':12,'12':8,'16':6},
           'edge_face_incidence':2,'Euler_characteristic':2,'silicon_boundary_degree':3,'oxygen_boundary_degree':2,
           'cluster_atoms':len(cluster),'cluster_faces':len(cluster_faces)}
    write(DATA/'cage-topology.json',stats)
    return stats


def data():
    # Deliberately restricted to this cited CIF's signed coordinate permutations;
    # this example helper is not a general CIF importer and never evaluates code.
    text=(DATA/'LTA.cif').read_text()
    operations=re.findall(r"'([+xyz,-]+)'",text)
    assert len(operations)==48
    atoms={}
    for line in text.splitlines():
        row=line.split()
        if len(row)!=5 or row[0] not in ['O1','O2','O3','T1']:continue
        base=list(map(float,row[2:]));positions=atoms.setdefault(row[1],set())
        for operation in operations:
            components=operation.split(',')
            assert all(re.fullmatch(r'[+-][xyz]',c) for c in components)
            positions.add(tuple(round(((-1 if c[0]=='-' else 1)*base['xyz'.index(c[1])])%1,8) for c in components))
    si,oxygen=sorted(atoms['Si']),sorted(atoms['O'])
    assert (len(si),len(oxygen))==(24,48)
    length=11.919
    poscar='LTA idealized pure silica; IZA-SC\n1\n'+'\n'.join(' '.join(str(length if i==j else 0) for j in range(3)) for i in range(3))+'\nSi O\n24 48\nDirect\n'
    poscar+=''.join(' '.join(map(str,p))+'\n' for p in si+oxygen)
    (DATA/'POSCAR.LTA').write_text(poscar)
    neighbors={}
    for i,a in enumerate(si):
        neighbors[i]=[]
        for j,b in enumerate(si):
            for shift in itertools.product([-1,0,1],repeat=3):
                v=tuple(b[d]+shift[d]-a[d] for d in range(3));distance=math.sqrt(dot(v,v))*length
                if .01<distance<3.3:neighbors[i].append((j,shift))
        assert len(neighbors[i])==4

    def canonical(path):
        variants=[]
        for seq in [path,list(reversed(path))]:
            for i in range(len(path)):
                ring=seq[i:]+seq[:i];anchor=ring[0][1]
                variants.append(tuple((j,*(v[d]-anchor[d] for d in range(3))) for j,v in ring))
        return min(variants)

    rings={}
    for start in range(len(si)):
        first=(start,(0,0,0))
        def visit(path):
            i,origin=path[-1]
            for j,shift in neighbors[i]:
                node=(j,tuple(origin[d]+shift[d] for d in range(3)))
                if node==first:
                    if len(path) in [4,6,8]:
                        points=[tuple(si[k][d]+offset[d] for d in range(3)) for k,offset in path]
                        chord=False
                        for a in range(len(path)):
                            for b in range(a+2,len(path)):
                                if a==0 and b==len(path)-1:continue
                                delta=minus(points[a],points[b])
                                if math.sqrt(dot(delta,delta))*length<3.3:chord=True;break
                            if chord:break
                        if chord:continue
                        key=canonical(path)
                        if key not in rings:rings[key]=path[:]
                elif len(path)<8 and node not in path:
                    # Chords imply a smaller face rather than this perimeter.
                    point=tuple(si[j][d]+node[1][d] for d in range(3))
                    chord=False
                    for k,offset in path[1:-1]:
                        delta=tuple(point[d]-si[k][d]-offset[d] for d in range(3))
                        if math.sqrt(dot(delta,delta))*length<3.3:chord=True;break
                    if not chord:visit(path+[node])
        visit([first])
    polygons=[]
    colors={4:'#e7b34c58',6:'#57afcf68',8:'#a488d478'}
    for key in sorted(rings):
        path=rings[key];points=[tuple(si[i][d]+offset[d] for d in range(3)) for i,offset in path]
        center=tuple(sum(p[d] for p in points)/len(points) for d in range(3))
        shift=tuple(math.floor(v+1e-8) for v in center)
        offsets=[tuple(offset[d]-shift[d] for d in range(3)) for _,offset in path]
        polygons.append({'atom_indices':[i for i,_ in path],'cell_offsets':offsets,
                         'color':colors[len(path)],'outline_width':.65,'outline_color':'#285168b0'})
    assert {len(f['atom_indices']) for f in polygons}=={4,6,8}
    write(DATA/'all-ring-faces.json',{'schema_version':1,'faces':[include_oxygen(f,si,oxygen,length) for f in polygons]})
    cage=cage_data(si,oxygen,polygons,length)
    selected=next(f for f in polygons if len(f['atom_indices'])==8)
    points=[tuple((si[i][d]+offset[d])*length for d in range(3)) for i,offset in zip(selected['atom_indices'],selected['cell_offsets'])]
    center=tuple(sum(p[d] for p in points)/8 for d in range(3))
    normal=[0.,0.,0.]
    for a,b in zip(points,points[1:]+points[:1]):
        v=cross(minus(a,center),minus(b,center));normal=[x+y for x,y in zip(normal,v)]
    normal=unit(normal);right=unit(minus(points[0],center));up=unit(cross(normal,right))
    def rotate(p):
        v=minus(p,center);return tuple(dot(v,axis) for axis in [right,up,normal])
    oxygens=[]
    for a,b in zip(points,points[1:]+points[:1]):
        _,_,position=bridge(tuple(v/length for v in a),tuple(v/length for v in b),oxygen,length)
        oxygens.append(tuple(v*length for v in position))
    ring='16\nEight-membered LTA Si-O ring; rigidly rotated from IZA coordinates\n'
    ring+=''.join(element+' '+' '.join(f'{v:.10f}' for v in rotate(p))+'\n' for element,ps in [('Si',points),('O',oxygens)] for p in ps)
    (DATA/'ring.xyz').write_text(ring)
    single={'atom_indices':[i for k in range(8) for i in [k,k+8]],'repeat':False,'color':'#54b6d86c','outline_width':.8,'outline_color':'#257895'}
    write(DATA/'single-face.json',{'schema_version':1,'faces':[single]})
    return {'unit_cell':{'atoms':72,'silicon':24,'oxygen':48},'alpha_cage':cage,
            'rings':{str(n):sum(len(f['atom_indices'])==n for f in polygons) for n in [4,6,8]}}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--output',type=Path,default=DATA/'renders')
    args=parser.parse_args();stats=data();exe=args.executable.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    base={'image':{'width':900,'height':900,'style':'shaded','aa':'2x'},
          'elements':{'Si':{'color':'#70a9d2','radius_A':.32},'O':{'color':'#ed7667','radius_A':.23}},
          'geometry':{'bond_colors':'atoms','bond_radius_A':.075},
          'overlays':{'cell':False},'camera':{'view':[1.4,1,1.2]}}
    variants=[]
    for name,source,changes in [
        ('01-lta-framework','cage.xyz',{}),
        ('02-lta-polygon-tiles','cage.xyz',{'faces_file':'../tiles.json'}),
        ('03-single-face-translucent','ring.xyz',{'faces_file':'../single-face.json','camera':{'view':[.3,-.4,1],'up':[0,1,0]}}),
        ('04-single-face-opaque','ring.xyz',{'faces':[dict(json.loads((DATA/'single-face.json').read_text())['faces'][0],color='#54b6d8')],'camera':{'view':[.3,-.4,1],'up':[0,1,0]}}),
        ('05-uniform-bonds','ring.xyz',{'geometry':{'bond_colors':'uniform','bond_radius_A':.075},'camera':{'view':[0,0,1],'up':[0,1,0]}}),
        ('06-atom-colored-bonds','ring.xyz',{'camera':{'view':[0,0,1],'up':[0,1,0]}}),
        ('07-thin-bonds','ring.xyz',{'geometry':{'bond_radius_A':.035},'camera':{'view':[0,0,1],'up':[0,1,0]}}),
        ('08-thick-bonds','ring.xyz',{'geometry':{'bond_radius_A':.14},'camera':{'view':[0,0,1],'up':[0,1,0]}}),
        ('09-repeated-tiles','cages-2x2.xyz',{'faces_file':'../repeated-tiles.json','geometry':{'bond_radius_A':.06}}),
    ]:
        config=copy.deepcopy(base)
        for key,value in changes.items():
            if isinstance(value,dict) and key in config:config[key].update(value)
            else:config[key]=value
        if 'faces_file' in config:
            # Configs may be written to an alternate output directory.
            config['faces_file']=os.path.relpath(DATA/Path(config['faces_file']).name,out)
        path=out/(name+'.json');write(path,config)
        command=[str(exe),str(DATA/source),'--no-user-config','--config',str(path),'-o',str(out/(name+'.png')),'--report',str(out/(name+'.report.json'))]
        subprocess.run(command,check=True,capture_output=True)
        report=json.loads((out/(name+'.report.json')).read_text())
        variants.append({'name':name,'source':source,'command':command,'image':name+'.png','counts':report['counts'],'sha256':hashlib.sha256((out/(name+'.png')).read_bytes()).hexdigest()})
        print(name,report['counts'],flush=True)
    try:
        from PIL import Image,ImageDraw,ImageFont
        tiles=[('01-lta-framework','Complete LTA alpha cage'),('02-lta-polygon-tiles','Translucent faces / Si-O boundaries'),
               ('03-single-face-translucent','One face / translucent'),('04-single-face-opaque','One face / opaque'),
               ('05-uniform-bonds','Uniform grey bonds'),('06-atom-colored-bonds','Atom-colored half-bonds'),
               ('07-thin-bonds','Bond radius 0.035 A'),('08-thick-bonds','Bond radius 0.140 A')]
        try:font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',23)
        except OSError:font=ImageFont.load_default()
        def sheet_for(entries,name):
            sheet=Image.new('RGB',(1200,math.ceil(len(entries)/2)*660),(246,248,250));draw=ImageDraw.Draw(sheet)
            for i,(stem,label) in enumerate(entries):
                x=(i%2)*600;y=(i//2)*660
                picture=Image.open(out/(stem+'.png')).convert('RGB');picture.thumbnail((580,580),Image.Resampling.LANCZOS)
                sheet.paste(picture,(x+10,y+10));draw.text((x+24,y+610),label,font=font,fill=(30,46,62))
            sheet.save(out/name)
        sheet_for(tiles,'preview.png')
        sheet_for([('01-lta-framework','Complete cage / 48 Si + 72 O'),('02-lta-polygon-tiles','26 translucent Si-O faces'),
                   ('03-single-face-translucent','Boundary includes all 8 Si + 8 O'),('09-repeated-tiles','Four complete neighboring cages')],
                  'cage-review.png')
    except ImportError:
        pass # Individual QASR images do not require Pillow.
    write(out/'manifest.json',{'source':'https://europe.iza-structure.org/IZA-SC/download_cif.php?ID=138',
          'cage_topology_source':'https://america.iza-structure.org/IZA-SC/Tilings/LTA.pdf',
          'cif_sha256':hashlib.sha256((DATA/'LTA.cif').read_bytes()).hexdigest(),'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
          'input_sha256':{name:hashlib.sha256((DATA/name).read_bytes()).hexdigest() for name in
                          ['cage.xyz','cages-2x2.xyz','ring.xyz','tiles.json','repeated-tiles.json','single-face.json','tiles-periodic.json','all-ring-faces.json','cage-topology.json']},
          'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'structure':stats,'images':variants})


if __name__=='__main__':main()
