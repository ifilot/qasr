# SPDX-License-Identifier: GPL-3.0-only
"""New drawing styles: geometry, hidden arcs, alpha and machine interfaces."""
import json
import math
from pathlib import Path
import subprocess
import unittest
import test_scene_render as support

EXE = support.EXE
ROOT = support.ROOT


class StyleTests(unittest.TestCase):
    setUp = support.SceneTests.setUp
    tearDown = support.SceneTests.tearDown
    invoke = support.SceneTests.invoke
    scene = support.SceneTests.scene
    render = support.SceneTests.render

    def sphere(self):
        return support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C"],[[10,10,10]],"Cartesian")

    def config(self, style):
        return {"image":{"width":128,"height":128,"style":style,"aa":"native","background":"#ffffff00"},
                "camera":{"view":[1,2,3],"up":[0,1,0]},
                "overlays":{"cell":False},"elements":{"C":{"radius_A":1,"color":"#f0a020"}}}

    def test_great_circles_against_projected_curve_oracle(self):
        text=self.sphere()
        for view in [[1,2,3],[0,0,1],[1,1,0]]:
            cfg=self.config("ortep");cfg["camera"]["view"]=view
            scene=self.scene(text,config=cfg);camera=scene["camera"]
            radius=camera["scale"]
            # Independent oracle: densely sample actual 3-D great circles,
            # project their FRONT halves, and measure screen-space distance.
            points=[]
            for axis in range(3):
                other=[k for k in range(3) if k!=axis]
                for j in range(6000):
                    angle=2*math.pi*j/6000
                    p=[0.,0.,0.];p[other[0]]=math.cos(angle);p[other[1]]=math.sin(angle)
                    if support.dot(p,camera["view"])<=0:continue
                    points.append((64+radius*support.dot(p,camera["right"]),
                                   64-radius*support.dot(p,camera["up"])))
            pixels=self.render(text,cfg)[2]
            checked=0
            for y in range(18,110,3):
                for x in range(18,110,3):
                    if math.hypot(x+.5-64,y+.5-64)>.80*radius:continue
                    distance=min(math.hypot(x+.5-px,y+.5-py) for px,py in points)
                    # Exclude the line's boundary, where the analytic distance
                    # approximation and finite curve sampling can differ.
                    if .30<distance<.85:continue
                    expected=b"\0\0\0\xff" if distance<=.30 else b"\xff\xff\xff\xff"
                    self.assertEqual(pixels[4*(y*128+x):4*(y*128+x)+4],expected,(view,x,y,distance))
                    checked+=1
            self.assertGreater(checked,100)

    def test_fill_contours_width_and_sampling(self):
        text=self.sphere()
        for style in ["ortep","balsac"]:
            cfg=self.config(style)
            pixels=self.render(text,cfg)[2]
            colors={pixels[k:k+4] for k in range(0,len(pixels),4)}
            self.assertIn(b"\0\0\0\xff",colors)
            self.assertEqual(pixels[:4],b"\0\0\0\0")
            if style=="balsac":
                opaque={p for p in colors if p[3]==255 and p[:3]!=b"\0\0\0"}
                self.assertGreater(len(opaque),100)  # smooth color, not flat bands
                self.assertTrue(all(p[0]>=p[1]>=p[2] for p in opaque))
            cfg["image"]["outline_width"]=0
            plain=self.render(text,cfg)[2]
            self.assertNotIn(b"\0\0\0\xff",{plain[k:k+4] for k in range(0,len(plain),4)})
            if style=="ortep":
                self.assertEqual({plain[k:k+4] for k in range(0,len(plain),4)},
                                 {b"\0\0\0\0",b"\xff\xff\xff\xff"})
            cfg["image"]["outline_width"]=2
            self.assertNotEqual(pixels,self.render(text,cfg)[2])
            for aa in ["native","2x","4x","adaptive"]:
                cfg["image"]["aa"]=aa
                expected=self.render(text,cfg,"--threads","1")[2]
                if aa=="2x":two=expected
                if aa=="adaptive":self.assertEqual(two,expected)
                if json.loads(subprocess.check_output([EXE,"--build-info"]))["openmp"]:
                    self.assertEqual(expected,self.render(text,cfg,"--threads","4")[2])

    def test_overlap_order_and_hidden_sphere(self):
        cfg=self.config("ortep");cfg["camera"].update(view=[0,0,1],up=[0,1,0])
        # A completely hidden atom must add no ink, even when drawn last.
        text=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C","H"],[[10,10,11],[10,10,9]],"Cartesian")
        cfg["elements"]["H"]={"radius_A":.7}
        cfg["bond_cutoffs_A"]={"C-H":.01}
        for style in ["ortep","balsac"]:
            cfg["image"]["style"]=style
            single=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C"],[[10,10,11]],"Cartesian")
            self.assertEqual(self.render(single,cfg)[2],self.render(text,cfg)[2])
            overlap=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C","H"],[[10,10,10],[10.7,10,10.3]],"Cartesian")
            reverse=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["H","C"],[[10.7,10,10.3],[10,10,10]],"Cartesian")
            self.assertEqual(self.render(overlap,cfg)[2],self.render(reverse,cfg)[2])

    def test_alpha_geometry_and_annotations(self):
        text=self.sphere()
        base=self.scene(text,config=self.config("shaded"))["primitives"]
        for style in ["ortep","balsac"]:
            cfg=self.config(style);cfg["elements"]["C"]["color"]="#f0a08080"
            pixels=self.render(text,cfg)[2]
            self.assertEqual(pixels[4*(64*128+64)+3],128)
            cfg["elements"]["C"]["color"]="#f0a020"
            self.assertEqual(self.scene(text,config=cfg)["primitives"],base)
            cfg["overlays"].update(labels="element",axes=True,scale_bar=True)
            cfg["image"].update(width=256,height=256)
            self.render(text,cfg)

    def test_bond_fill_and_surface_occlusion(self):
        text=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C","C"],[[7,10,10],[13,10,10]],"Cartesian")
        for style in ["ortep","balsac"]:
            cfg=self.config(style)
            cfg["camera"].update(view=[0,0,1],up=[0,1,0])
            cfg["bond_cutoffs_A"]={"C-C":6.1}
            cfg["geometry"]={"bond_radius_A":.25}
            pixels=self.render(text,cfg)[2]
            center=pixels[4*(64*128+64):4*(64*128+64)+4]
            self.assertEqual(center,b"\0\0\0\xff" if style=="ortep" else bytes([212,212,212,255]))
            # Front sphere covers the same crossing bond at the image center.
            blocked=support.poscar([[20,0,0],[0,20,0],[0,0,20]],["C","C","H"],[[7,10,10],[13,10,10],[10,10,12]],"Cartesian")
            cfg["elements"]["H"]={"radius_A":1,"color":"#ff0000"}
            cfg["bond_cutoffs_A"].update({"C-H":.01,"H-H":.01})
            front=self.render(blocked,cfg)[2]
            center=front[4*(64*128+64):4*(64*128+64)+4]
            if style=="ortep":
                # At this axial view the crossing great circles are black,
                # so check a point inside the white sphere off both arcs.
                self.assertEqual(front[4*(61*128+67):4*(61*128+67)+4],b"\xff\xff\xff\xff")
            else:self.assertGreater(center[0],center[1])

    def test_config_job_worker_and_backend_contract(self):
        source=self.path/"POSCAR";source.write_text(self.sphere())
        for style in ["ortep","balsac"]:
            cfg=self.config(style)
            self.assertEqual(json.loads(self.invoke(self.sphere(),"--inspect",config=cfg).stdout)["settings"]["image"]["style"],style)
            self.assertEqual(json.loads(self.invoke(self.sphere(),"--inspect","--style","shaded",config=cfg).stdout)["settings"]["image"]["style"],"shaded")
            path=self.path/"dump.json"
            dumped=json.loads(self.invoke(self.sphere(),"--dump-config",config=cfg).stdout)["config"]
            path.write_text(json.dumps(dumped))
            subprocess.run([EXE,source,"--no-user-config","--config",path,"--validate-config"],check=True,capture_output=True)
            expected=self.render(self.sphere(),cfg)[2]
            out=self.path/"job.png"
            job={"input":str(source),"output":str(out),"no_user_config":True,"config":cfg}
            result=subprocess.run([EXE,"--job","-"],input=json.dumps(job),text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(support.decode_png(out)[2],expected)
            requests=[{"id":str(i),"op":"render","job":job} for i in range(2)]
            result=subprocess.run([EXE,"--batch"],input="".join(json.dumps(r)+"\n" for r in requests),text=True,capture_output=True)
            results=[json.loads(line) for line in result.stdout.splitlines()]
            self.assertTrue(all(r["ok"] for r in results),result.stdout+result.stderr)
            self.assertTrue(results[1]["result"]["cache"]["scene_hit"])
            self.assertEqual(support.decode_png(out)[2],expected)
            rejected=self.invoke(self.sphere(),"--inspect","--backend","cairo","--json-errors",config=cfg,code=3)
            self.assertEqual(json.loads(rejected.stderr)["error"]["path"],"image.backend")
        for name in ["config","user","job","worker"]:
            schema=json.loads(subprocess.check_output([EXE,"--schema",name]))
            def enums(node):
                if isinstance(node,dict):
                    if "enum" in node and "shaded" in node["enum"]:
                        self.assertIn("ortep",node["enum"]);self.assertIn("balsac",node["enum"])
                    for v in node.values():enums(v)
                elif isinstance(node,list):
                    for v in node:enums(v)
            enums(schema)

    def test_cached_worker_can_switch_styles(self):
        source=self.path/"POSCAR";source.write_text(self.sphere())
        requests=[];expected=[]
        for i,style in enumerate(["shaded","ortep","balsac","illustrated","ortep"]):
            cfg=self.config(style)
            expected.append(self.render(self.sphere(),cfg)[2])
            job={"input":str(source),"output":str(self.path/("batch-"+str(i)+".png")),
                 "no_user_config":True,"config":cfg}
            requests.append({"id":str(i),"op":"render","job":job})
        result=subprocess.run([EXE,"--batch"],input="".join(json.dumps(r)+"\n" for r in requests),text=True,capture_output=True)
        results=[json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(results),len(requests),result.stderr)
        for i,(request,response) in enumerate(zip(requests,results)):
            self.assertTrue(response["ok"],response)
            if i:self.assertTrue(response["result"]["cache"]["scene_hit"])
            self.assertEqual(support.decode_png(Path(request["job"]["output"]))[2],expected[i])


if __name__=="__main__":
    unittest.main(verbosity=2)
