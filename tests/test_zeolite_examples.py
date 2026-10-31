# SPDX-License-Identifier: GPL-3.0-only
"""Verify cage closure and Si-O polygon boundaries in the scientific examples."""
from collections import Counter
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

EXE=Path(sys.argv.pop(1)).resolve()
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import render_zeolite_examples as generator


def xyz(path):
    lines=path.read_text().splitlines();atoms=[]
    for line in lines[2:]:
        row=line.split();atoms.append((row[0],tuple(map(float,row[1:]))))
    assert len(atoms)==int(lines[0])
    return atoms


class ZeoliteExamples(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory();cls.root=Path(cls.tmp.name)
        shutil.copyfile(ROOT/'examples/zeolite/LTA.cif',cls.root/'LTA.cif')
        cls.original=generator.DATA;generator.DATA=cls.root
        cls.stats=generator.data()

    @classmethod
    def tearDownClass(cls):
        generator.DATA=cls.original;cls.tmp.cleanup()

    def boundaries(self,source,face_file):
        atoms=xyz(self.root/source)
        faces=json.loads((self.root/face_file).read_text())['faces']
        edges=Counter()
        for face in faces:
            ids=face['atom_indices']
            self.assertEqual(len(ids)%2,0)
            self.assertEqual([atoms[i][0] for i in ids],['Si','O']*(len(ids)//2))
            for a,b in zip(ids,ids[1:]+ids[:1]):
                distance=math.dist(atoms[a][1],atoms[b][1])
                self.assertGreater(distance,1.4);self.assertLess(distance,1.8)
                edges[tuple(sorted([a,b]))]+=1
        return atoms,faces,edges

    def test_complete_alpha_cage_is_a_closed_connected_surface(self):
        atoms,faces,edges=self.boundaries('cage.xyz','tiles.json')
        self.assertEqual(Counter(a[0] for a in atoms),{'Si':48,'O':72})
        self.assertEqual(Counter(len(f['atom_indices']) for f in faces),{8:12,12:8,16:6})
        self.assertEqual(len(edges),144);self.assertEqual(set(edges.values()),{2})
        self.assertEqual(len(atoms)-len(edges)+len(faces),2)
        adjacent=[set() for _ in atoms]
        for a,b in edges:adjacent[a].add(b);adjacent[b].add(a)
        for i,(element,_) in enumerate(atoms):self.assertEqual(len(adjacent[i]),3 if element=='Si' else 2)
        visited={0};pending=[0]
        while pending:
            for vertex in adjacent[pending.pop()]-visited:visited.add(vertex);pending.append(vertex)
        self.assertEqual(visited,set(range(120)))

    def test_single_face_includes_every_ring_atom(self):
        atoms,faces,edges=self.boundaries('ring.xyz','single-face.json')
        self.assertEqual(len(atoms),16);self.assertEqual(len(faces),1)
        self.assertEqual(set(faces[0]['atom_indices']),set(range(16)))
        self.assertEqual(len(edges),16)
        self.assertEqual(faces[0]['color'],'#54b6d86c')

    def test_neighbor_cages_share_faces_without_duplicates(self):
        atoms,faces,_=self.boundaries('cages-2x2.xyz','repeated-tiles.json')
        self.assertEqual(len(atoms),416);self.assertEqual(len(faces),100)
        self.assertEqual(len({generator.cycle_key(f['atom_indices']) for f in faces}),100)

    def test_periodic_catalog_alternates_both_atom_species(self):
        for name in ['all-ring-faces.json','tiles-periodic.json']:
            faces=json.loads((self.root/name).read_text())['faces']
            for f in faces:
                ids=f['atom_indices']
                self.assertEqual([i<24 for i in ids],[True,False]*(len(ids)//2))
                self.assertEqual(len(f['cell_offsets']),len(ids))

    def test_renderer_has_no_clipped_bonds_and_renders_all_cage_faces(self):
        command=[EXE,self.root/'cage.xyz','--no-user-config','--faces',self.root/'tiles.json',
                 '--bond-colors','atoms','--cell','off']
        result=subprocess.run([*command,'--inspect-scene'],check=True,capture_output=True,text=True,timeout=30)
        scene=json.loads(result.stdout)
        self.assertEqual((scene['instance_count'],scene['bond_count'],scene['face_count']),(120,144,26))
        atoms=xyz(self.root/'cage.xyz')
        degree=Counter()
        for p in scene['primitives']:
            if p['kind']!='bond':continue
            a,b=p['source'];degree[a]+=1;degree[b]+=1
            self.assertLess(math.dist(p['a'],atoms[a][1]),1e-8)
            self.assertLess(math.dist(p['b'],atoms[b][1]),1e-8)
        for i,(element,_) in enumerate(atoms):self.assertEqual(degree[i],3 if element=='Si' else 2)
        output=self.root/'cage.png'
        subprocess.run([*command,'--width','256','--height','256','--sphere-scale','.45','-o',output],
                       check=True,capture_output=True,timeout=30)
        self.assertGreater(output.stat().st_size,1000)


if __name__=='__main__':unittest.main()
