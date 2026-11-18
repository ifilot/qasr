# SPDX-License-Identifier: GPL-3.0-only
"""Render the point-group symmetry gallery from its recipes."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BASE={'schema_version':1,
      'image':{'width':800,'height':800,'background':'#ffffff','style':'illustrated','outline_width':1.5},
      'overlays':{'cell':False}}
# name, molecule, caption, view, up, symmetry
RECIPES=[
    ('01-water-c2','h2o.xyz','Water: C2 axis',[5,-2,1.5],None,{'operation':'C2','axis':[0,0,1]}),
    ('02-water-sigma-v','h2o.xyz','Water: mirror plane bisecting H-O-H',[5,-2.6,1.5],None,{'operation':'sigma_v','normal':[0,1,0]}),
    ('03-water-sigma-v-molecular','h2o.xyz','Water: molecular mirror plane, from its three atoms',[4,-3,1.5],None,
     {'operation':'sigma_v','plane_atoms':[0,1,2],'label':"σ_v'"}),
    ('04-ammonia-c3','nh3.xyz','Ammonia: C3 axis',[3,-5,2],None,{'operation':'C3','axis':[0,0,1]}),
    ('05-ammonia-sigma-v','nh3.xyz','Ammonia: one of three vertical mirror planes',[4,-4,2],None,{'operation':'sigma_v','normal':[1,0,0]}),
    ('06-ammonia-identity','nh3.xyz','Ammonia: identity',[3,-5,2],None,{'operation':'E'}),
    ('07-benzene-c6','benzene.xyz','Benzene: principal C6 axis',[3,-5,3],None,{'operation':'C6','axis':[0,0,1]}),
    ('08-benzene-sigma-h','benzene.xyz','Benzene: horizontal mirror plane',[3,-5,3],None,{'operation':'sigma_h','normal':[0,0,1]}),
    ('09-benzene-c2-dashed','benzene.xyz',"Benzene: in-plane C2' through two carbon atoms, dashed",[2,-5,4],None,
     {'operation':'C2','axis_atoms':[3,0],'axis_style':'dashed','label':"C_2'"}),
    ('10-benzene-inversion','benzene.xyz','Benzene: inversion centre',[3,-5,3],None,{'operation':'i'}),
    ('11-ethane-s6','ethane.xyz','Staggered ethane: S6 improper rotation',[3,-5,1.5],None,{'operation':'S6','axis':[0,0,1]}),
    ('12-ethane-inversion','ethane.xyz','Staggered ethane: inversion centre',[3,-5,1.5],None,{'operation':'i'}),
    ('13-methane-s4','methane.xyz','Methane: S4 improper rotation',[5,-3,2],None,{'operation':'S4','axis':[0,0,1]}),
    ('14-bf3-sigma-h','bf3.xyz','Boron trifluoride: horizontal mirror plane, sans label',[3,-5,3],None,
     {'operation':'sigma_h','normal':[0,0,1],'label_font':'sans'}),
]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'examples/symmetry')
    args=parser.parse_args();exe=args.executable.resolve();out=args.output.resolve()
    (out/'recipes').mkdir(parents=True,exist_ok=True);(out/'renders').mkdir(parents=True,exist_ok=True)
    manifest=[]
    for name,molecule,caption,view,up,symmetry in RECIPES:
        config=json.loads(json.dumps(BASE));config['camera']={'view':view}
        if up:config['camera']['up']=up
        config['symmetry']=symmetry
        recipe=out/'recipes'/(name+'.json');recipe.write_text(json.dumps(config,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
        image=out/'renders'/(name+'.png');report=out/'renders'/(name+'.report.json')
        subprocess.run([exe,ROOT/'examples/symmetry'/molecule,'--no-user-config','--config',recipe,'-o',image,'--report',report],check=True)
        deviation=json.loads(report.read_text())['symmetry']['max_deviation_A'];report.unlink()
        manifest.append({'name':name,'molecule':molecule,'caption':caption,'recipe':f'recipes/{name}.json','image':f'renders/{name}.png',
                         'operation':symmetry['operation'],'max_deviation_A':deviation})
    (out/'manifest.json').write_text(json.dumps({'images':manifest},indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    try:
        from PIL import Image
    except ImportError:
        print('Pillow is unavailable; skipped comparison.png');return
    columns=4;rows=(len(RECIPES)+columns-1)//columns;cell=400
    sheet=Image.new('RGB',(columns*cell,rows*cell),'white')
    for index,record in enumerate(manifest):
        tile=Image.open(out/record['image']).convert('RGB').resize((cell,cell),Image.LANCZOS)
        sheet.paste(tile,((index%columns)*cell,(index//columns)*cell))
    sheet.save(out/'comparison.png',optimize=True)


if __name__=='__main__':
    main()
