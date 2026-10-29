# SPDX-License-Identifier: GPL-3.0-only
"""Build post-v0.3.0 M9 examples; keep the stable release download unchanged."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable',type=Path)
    parser.add_argument('--source-archive',type=Path,required=True)
    args=parser.parse_args();exe=args.executable.resolve()
    site=ROOT/'docs/website';downloads=site/'downloads';schemas=site/'schemas'
    schemas.mkdir(exist_ok=True)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for schema in (ROOT/'schemas').glob('*.json'):shutil.copyfile(schema,schemas/schema.name)
    common={'schema_version':1,'image':{'width':900,'height':700,'style':'illustrated','aa':'2x','background':'#ffffff','png_encoder':'libpng','png_compression':6},'camera':{'view':'top'},'geometry':{'expand_xy':[1,1],'image_colors':'greyscale'},'overlays':{'legend':True,'axes':True}}
    source=json.loads(json.dumps(common));source['atom_overrides']=[{'select':{'source_indices':[6,34]},'color':'#ee6633','radius_A':0.95}]
    selected=json.loads(json.dumps(source));selected['atom_overrides'].append({'select':{'source_indices':[6],'cell':[1,0,0]},'color':'#18a88b','radius_A':1.15})
    records=[]
    for name,config in [('source-highlight',source),('cell-highlight',selected)]:
        recipe=downloads/(name+'.json');recipe.write_text(json.dumps(config,indent=2)+'\n')
        image=site/'assets'/(name+'.png')
        cmd=[str(exe),str(downloads/'CONTCAR.co'),'--config',str(recipe),'--no-user-config','-o',str(image)]
        subprocess.run(cmd,check=True,capture_output=True)
        inspection=json.loads(subprocess.check_output([exe,downloads/'CONTCAR.co','--config',recipe,'--no-user-config','--inspect-scene']))
        records.append({'image':'assets/'+image.name,'recipe':'downloads/'+recipe.name,'png_sha256':sha(image),'config_sha256':sha(recipe),'input_sha256':sha(downloads/'CONTCAR.co'),'instances':inspection['instance_count']})
    job={'schema_version':1,'input':'CONTCAR.co','output':'highlight.png','report':'highlight-report.json','no_user_config':True,'json_errors':True,'config':selected}
    (downloads/'highlight-job.json').write_text(json.dumps(job,indent=2)+'\n')
    shutil.copyfile(args.source_archive,downloads/'qasr-source.tar.gz')
    source_manifest=args.source_archive.with_name(args.source_archive.name.replace('.tar.gz','-source-manifest.json'))
    shutil.copyfile(source_manifest,downloads/'source-manifest.json')
    files=[site/'batch.html',downloads/'worker-requests.jsonl',*schemas.glob('*.json'),*[downloads/n for n in ['source-highlight.json','cell-highlight.json','highlight-job.json','qasr-source.tar.gz','source-manifest.json']],*[site/r['image'] for r in records]]
    (site/'configuration-manifest.json').write_text(json.dumps({'availability':'QASR development after v0.3.0, including M9–M11; executable qasr','executable_sha256':sha(exe),'build_info':json.loads(subprocess.check_output([exe,'--build-info'])),'files':{str(p.relative_to(site)):sha(p) for p in files},'images':records},indent=2)+'\n')
    print('Built two M9 styling examples, schemas and separate development source download')


if __name__=='__main__':main()
