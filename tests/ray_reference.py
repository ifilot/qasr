# SPDX-License-Identifier: GPL-3.0-only
"""Slow independent per-ray oracle in angstroms for opaque M3 test scenes."""
import math
import sys

sys_epsilon=sys.float_info.epsilon


def dot(a, b):
    return sum(x*y for x,y in zip(a,b))


def sub(a,b):
    return [x-y for x,y in zip(a,b)]


def norm(a):
    return math.sqrt(dot(a,a))


def reference_pixels(scene, supersampling=2):
    camera=scene["camera"]
    scale=camera["scale"]
    width,height=camera["width"],camera["height"]
    shapes=[]
    for p in scene["primitives"]:
        a=[dot(p["a"],camera[k]) for k in ["right","up","view"]]
        b=[dot(p["b"],camera[k]) for k in ["right","up","view"]]
        radius=p["radius"]
        shapes.append((p,a,b,radius))
    zfront=max(max(a[2],b[2])+r for _,a,b,r in shapes)+10
    direction=[0,0,-1]
    light=[-1/math.sqrt(6),-1/math.sqrt(6),2/math.sqrt(6)]
    result=bytearray()
    for y in range(height):
        for x in range(width):
            colors=[]
            offsets=[(k+0.5)/supersampling for k in range(supersampling)]
            for dy in offsets:
                for dx in offsets:
                    origin=[(x+dx-width/2)/scale+camera["center"][0],
                            (height-y-dy-height/2)/scale+camera["center"][1],zfront]
                    closest=math.inf
                    color=[255,255,255,255]
                    for p,a,b,r in shapes:
                        w=sub(origin,a)
                        hits=[]
                        if p["kind"]=="sphere":
                            discriminant=dot(w,direction)**2-dot(w,w)+r*r
                            if discriminant>=0:
                                t=-dot(w,direction)-math.sqrt(discriminant)
                                n=[(w[d]+t*direction[d])/r for d in range(3)]
                                hits.append((t,n))
                        else:
                            v=sub(b,a)
                            length=norm(v)
                            u=[d/length for d in v]
                            du,wu=dot(direction,u),dot(w,u)
                            aa=dot(direction,direction)-du*du
                            bb=2*(dot(w,direction)-wu*du)
                            cc=dot(w,w)-wu*wu-r*r
                            discriminant=bb*bb-4*aa*cc
                            if aa>1e-12 and discriminant>=0:
                                for sign in [-1,1]:
                                    t=(-bb+sign*math.sqrt(discriminant))/(2*aa)
                                    along=wu+t*du
                                    if 0<=along<=length:
                                        n=[(w[d]+t*direction[d]-u[d]*along)/r for d in range(3)]
                                        hits.append((t,n))
                            if abs(du)>1e-12:
                                for end in [0,length]:
                                    t=(end-wu)/du
                                    radial=[w[d]+t*direction[d]-u[d]*end for d in range(3)]
                                    if dot(radial,radial)<=r*r:
                                        hits.append((t,[d*(1 if end else -1) for d in u]))
                        for t,normal in hits:
                            tolerance=32*sys_epsilon*max(1,zfront,abs(t))
                            if t>=0 and (math.isinf(closest) or t<closest-tolerance):
                                closest=t
                                shade=1 if p["kind"]=="cell" else 0.25+0.75*max(0,dot(normal,light))
                                color=[int(math.floor(v*shade+0.5)) for v in p["rgba"][:3]]+[255]
                    colors.append(color)
            n=supersampling**2
            result.extend((sum(color[d] for color in colors)+n//2)//n for d in range(4))
    return bytes(result)
