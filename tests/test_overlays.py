# SPDX-License-Identifier: GPL-3.0-only
import collections
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from png_support import decode_png

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
CAP=json.loads(subprocess.check_output([str(EXE),'--build-info'],text=True))


def poscar(elements,positions):
    return ('overlay\n1\n100 0 0\n0 100 0\n0 0 100\n'+' '.join(elements)+'\n'
            +' '.join('1' for _ in elements)+'\nCartesian\n'
            +'\n'.join(' '.join(map(str,p)) for p in positions)+'\n')


def pixel(image,x,y):
    w,h,p=image
    return tuple(p[4*(y*w+x):4*(y*w+x)+4])


class OverlayTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='qasr-overlay-');self.path=Path(self.tmp.name)
        self.source=self.path/'POSCAR';self.output=self.path/'image.png';self.config=self.path/'config.json'
        self.source.write_text(poscar(['C','O'],[[49,50,50],[51,50,50]]))
        self.settings={'image':{'width':384,'height':288,'background':'#ffffff00'},
                       'camera':{'view':[0,0,1],'up':[0,1,0]},
                       'overlays':{'cell':False},'bond_cutoffs_A':{'C-O':0.01}}

    def tearDown(self):self.tmp.cleanup()

    def run_render(self,*flags,code=0):
        self.config.write_text(json.dumps(self.settings))
        p=subprocess.run([str(EXE),str(self.source),'--config',str(self.config),*map(str,flags)],
                         capture_output=True,text=True,timeout=60)
        self.assertEqual(p.returncode,code,p.stderr)
        return p

    def render(self,*flags):
        p=self.run_render('-o',self.output,'--benchmark-stages',*flags)
        return decode_png(self.output),p.stderr

    def scene(self,*flags):return json.loads(self.run_render('--inspect-scene',*flags).stdout)

    def test_individual_overlays_and_disable_precedence(self):
        baseline,_=self.render()
        for flags in [('--labels','element'),('--labels','index'),('--axes','on'),
                      ('--legend','on'),('--scale-bar','on')]:
            with self.subTest(flags=flags):
                result,_=self.render(*flags)
                self.assertNotEqual(result[2],baseline[2])
                self.assertEqual(result[:2],baseline[:2])
        self.settings['overlays'].update(labels='index',axes=True,legend=True,scale_bar=True)
        restored,_=self.render('--labels','none','--axes','off','--legend','off','--scale-bar','off')
        self.assertEqual(restored,baseline)
        self.assertNotIn('viewport_px',self.scene('--labels','none','--axes','off','--legend','off','--scale-bar','off')['camera'])

    def test_labels_follow_surface_depth_and_element_identity(self):
        self.settings['elements']={'C':{'radius_A':0.6},'O':{'radius_A':0.6}}
        self.source.write_text(poscar(['C','O'],[[50,50,49],[50,50,51]]))
        image,stderr=self.render('--labels','element')
        self.assertIn('overlay_labels=1',stderr)
        self.source.write_text(poscar(['O','C'],[[50,50,51],[50,50,49]]))
        swapped,stderr=self.render('--labels','element')
        self.assertEqual(image,swapped)
        self.assertIn('overlay_labels=1',stderr)
        indexed,_=self.render('--labels','index')
        self.assertNotEqual(indexed[2],image[2])
        self.settings['elements']['O']['color']='#e8806900'
        _,stderr=self.render('--labels','element')
        self.assertIn('overlay_labels=1',stderr)  # hidden/transparent O does not hide C
        self.source.write_text(poscar(['C','O','O'],[[50,50,49],[49,50,51],[51,50,51]]))
        self.settings['elements']={'C':{'radius_A':0.1},'O':{'radius_A':0.5}}
        self.settings['geometry']={'bond_radius_A':0.35}
        self.settings['bond_cutoffs_A']={'C-O':0.01,'O-O':3}
        _,stderr=self.render('--labels','element')
        self.assertIn('overlay_labels=2',stderr)  # front cylinder hides C's center
        self.source.write_text(poscar(['C','O'],[[50,50,0.6],[50,50,0.6]]))
        self.settings['elements']={'C':{'radius_A':0.6},'O':{'radius_A':0.6}}
        self.settings['camera']['view']=[0,0,-1]
        self.settings['bond_cutoffs_A']={'C-O':0.01}
        _,stderr=self.render('--labels','element')
        self.assertIn('overlay_labels=1',stderr)  # coincident surfaces retain input-order ties

    def test_index_labels_repeat_and_thread_determinism(self):
        self.settings['overlays'].update(labels='index',axes=True,legend=True,scale_bar=True)
        self.source.write_text(poscar(['C'],[[50,50,50]]))
        scene=self.scene('--repeat','2,1,1')
        spheres=[p for p in scene['primitives'] if p['kind']=='sphere']
        self.assertEqual([p['instance_index'] for p in spheres],[0,1])
        self.assertEqual([p['element'] for p in spheres],['C','C'])
        baseline,_=self.render('--threads','1','--repeat','2,1,1')
        for workers in ([2,4,8] if CAP['openmp'] else [1]):
            for aa in ['2x','4x','adaptive']:
                for style in ['shaded','illustrated']:
                    first,_=self.render('--threads',str(workers),'--aa',aa,'--style',style)
                    again,_=self.render('--threads',str(workers),'--aa',aa,'--style',style)
                    self.assertEqual(first,again)
            same,_=self.render('--threads',str(workers),'--repeat','2,1,1')
            self.assertEqual(same,baseline)

    def test_periodic_caps_use_visible_surface_label_anchors(self):
        self.source.write_text('periodic\n1\n2.5 0 0\n0 2.5 0\n0 0 2.5\nCo\n1\nDirect\n0.5 0.5 0.5\n')
        self.settings.pop('bond_cutoffs_A',None)
        self.settings['camera']['view']='top'
        self.settings['elements']={'Co':{'radius_A':0.85}}
        for aa in ['native','2x','4x','adaptive']:
            image,stderr=self.render('--labels','element','--aa',aa)
            self.assertIn('overlay_labels=1',stderr)
            plain,_=self.render('--labels','none','--aa',aa)
            self.assertNotEqual(image[2],plain[2])

    def test_dense_label_collisions_are_filtered_deterministically(self):
        self.source.write_text(poscar(['C','O','H'],[[49.5,50,50],[50.5,50,50],[90,90,50]]))
        self.settings['elements']={s:{'radius_A':0.1} for s in ['C','O','H']}
        first,stderr=self.render('--labels','element')
        self.assertIn('overlay_labels=2',stderr)
        self.assertIn('overlay_labels_skipped=1',stderr)
        again,_=self.render('--labels','element')
        self.assertEqual(first,again)

    def test_viewport_fit_scale_bar_and_transparent_backing(self):
        self.settings['overlays'].update(axes=True,legend=True,scale_bar=True)
        self.settings['elements']={'C':{'color':'#123456'}}
        for view in ['top',[1,-1,0.4],[0,0,1]]:
            self.settings['camera']['view']=view
            scene=self.scene();camera=scene['camera'];vp=camera['viewport_px']
            self.assertGreater(vp['y'],0);self.assertLess(vp['width'],384)
            for p in scene['primitives']:
                for point in [p['a'],p['b']]:
                    x=(sum(a*b for a,b in zip(point,camera['right']))-camera['center'][0])*camera['scale']+384/2
                    y=(sum(a*b for a,b in zip(point,camera['up']))-camera['center'][1])*camera['scale']+288/2
                    r=p['radius']*camera['scale']
                    self.assertGreaterEqual(x-r,vp['x']-1e-8)
                    self.assertLessEqual(x+r,vp['x']+vp['width']+1e-8)
                    self.assertGreaterEqual(y-r,vp['y']-1e-8)
                    self.assertLessEqual(y+r,vp['y']+vp['height']+1e-8)
            bar=camera['scale_bar'];self.assertTrue(bar['label'].endswith('Å'))
            self.assertAlmostEqual(bar['pixels'],bar['angstrom']*camera['scale'])
            nice=bar['angstrom']/10**math.floor(math.log10(bar['angstrom']))
            self.assertTrue(any(abs(nice-n)<1e-9 for n in [1,2,5]))
            image,_=self.render()
            y=bar['y'];end=bar['x']+bar['pixels']
            xs=[x for x in range(8,int(math.ceil(end))+5) if max(pixel(image,x,y)[:3])<160]
            self.assertTrue(xs);self.assertLessEqual(abs((max(xs)-min(xs))-bar['pixels']),3.5)
            self.assertEqual(pixel(image,384-93,288-65),(255,255,255,224))
            legend_left=384-camera['legend']['width']-8
            self.assertEqual(pixel(image,legend_left+14,40),(18,52,86,255))

    def test_axes_cartesian_colors_and_line_of_sight_axis(self):
        image,_=self.render('--axes','on')
        ox,oy=384-48,288-36
        red=pixel(image,ox+14,oy);green=pixel(image,ox,oy-14);blue=pixel(image,ox,oy)
        self.assertGreater(red[0],red[1]+60);self.assertGreater(red[0],red[2]+60)
        self.assertGreater(green[1],green[0]+30);self.assertGreater(green[1],green[2]+30)
        self.assertGreater(blue[2],blue[0]+60);self.assertGreater(blue[2],blue[1]+30)
        self.settings['camera']['view']=[0,0,-1]
        image,_=self.render('--axes','on')
        red=pixel(image,ox-14,oy)
        self.assertGreater(red[0],red[1]+60)

    def test_legend_many_species_and_layout_errors_preserve_destination(self):
        atoms=json.loads((ROOT/'vendor/atom-architect/atoms.json').read_text())['atoms']['nr2element']
        symbols=[atoms[str(z)] for z in range(1,119)]
        self.source.write_text(poscar(symbols,[[5+8*(i%10),5+8*(i//10),50] for i in range(118)]))
        self.settings['image'].update(width=1024,height=512)
        self.settings['overlays']['legend']=True
        scene=self.scene();legend=scene['camera']['legend']
        self.assertGreater(legend['columns'],1)
        self.assertLessEqual(legend['height'],512-16)
        self.assertEqual(len(collections.Counter(p['element'] for p in scene['primitives'] if p['kind']=='sphere')),118)
        self.render()
        self.output.write_bytes(b'keep')
        self.run_render('-o',self.output,'--width','64','--height','64','--axes','on',code=2)
        self.assertEqual(self.output.read_bytes(),b'keep')
        self.assertEqual(list(self.path.glob('*.tmp.*')),[])

    def test_all_slab_annotations_render(self):
        self.settings.pop('bond_cutoffs_A',None)
        self.settings['image'].update(width=384,height=288,style='illustrated')
        self.settings['overlays'].update(labels='element',axes=True,legend=True,scale_bar=True)
        self.settings['camera']['view']='top'
        for source in sorted((ROOT/'tests/fixtures/slabs').glob('*/CONTCAR')):
            with self.subTest(source=source.parent.name):
                self.source.write_bytes(source.read_bytes())
                image,stderr=self.render()
                self.assertEqual(image[:2],(384,288))
                self.assertIsNotNone(re.search(r'overlay_labels=\d+',stderr))


if __name__=='__main__':unittest.main()
