# SPDX-License-Identifier: GPL-3.0-only
"""Compare complete-endpoint bonds and solid/dashed outlined cell strokes."""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--output',type=Path,default=ROOT/'examples/cell-lines')
    args=parser.parse_args();exe=args.executable.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=ROOT/'tests/fixtures/slabs/01_co0001_clean/CONTCAR'
    base=json.loads((ROOT/'presets/illustrated.json').read_text())
    base['overlays'].update(cell_color='#000000',cell_width=2)
    variants=[
        ('01-xy-expansion','XY expansion / neighbor bonds',{'geometry':{'expand_xy':[1,1]}}),
        ('02-complete-endpoint-bonds','Both endpoint atoms present',{}),
        ('03-dashed-cell','Dashed black cell',{'overlays':{'cell_style':'dashed'}}),
        ('04-solid-white-contour','Solid black + white contour',{'overlays':{'cell_outline_width':1.5}}),
        ('05-dashed-white-contour','Dashed black + white contour',{'overlays':{'cell_style':'dashed','cell_outline_width':1.5}}),
        ('06-dark-background','Dashed + contour / dark background',{'image':{'background':'#526579'},'overlays':{'cell_style':'dashed','cell_outline_width':1.5}}),
    ]
    records=[]
    for name,label,changes in variants:
        config=copy.deepcopy(base)
        for group,values in changes.items():config[group].update(values)
        cfg=out/(name+'.json');cfg.write_text(json.dumps(config,indent=2)+'\n')
        output=out/(name+'.png')
        command=[str(exe),str(source),'--no-user-config','--config',str(cfg),'-o',str(output),'--report',str(out/(name+'.report.json'))]
        subprocess.run(command,check=True,capture_output=True)
        report=json.loads((out/(name+'.report.json')).read_text())
        scene=json.loads(subprocess.check_output([exe,source,'--no-user-config','--config',cfg,'--inspect-scene']))
        atoms={(p['source'][0],tuple(p['origin'])) for p in scene['primitives'] if p['kind']=='sphere'}
        missing=0
        for p in scene['primitives']:
            if p['kind']!='bond':continue
            a=(p['source'][0],tuple(p['origin']))
            b=(p['source'][1],tuple(x+y for x,y in zip(p['origin'],p['image'])))
            missing+=a not in atoms or b not in atoms
        assert missing==0
        records.append({'name':name,'label':label,'image':output.name,'command':command,
                        'counts':report['counts'],'bonds_with_missing_endpoint_atoms':missing,
                        'sha256':hashlib.sha256(output.read_bytes()).hexdigest()})
        print(name,report['counts']['bonds'],'bonds;',missing,'with missing endpoints',flush=True)
    try:
        from PIL import Image,ImageDraw,ImageFont
        sheet=Image.new('RGB',(1200,1980),(246,248,250));draw=ImageDraw.Draw(sheet)
        try:font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',23)
        except OSError:font=ImageFont.load_default()
        for i,r in enumerate(records):
            x=i%2*600;y=i//2*660
            picture=Image.open(out/r['image']).convert('RGB');picture.thumbnail((580,580),Image.Resampling.LANCZOS)
            sheet.paste(picture,(x+10,y+10));draw.text((x+18,y+610),r['label'],font=font,fill=(30,46,62))
        sheet.save(out/'comparison.png')
    except ImportError:pass
    (out/'manifest.json').write_text(json.dumps({'input':os.path.relpath(source,out),
        'input_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'generator_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'images':records},indent=2)+'\n')


if __name__=='__main__':main()
