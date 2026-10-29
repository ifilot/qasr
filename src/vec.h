/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_VEC_H
#define VR_VEC_H
#include "structure.h"
#include <math.h>
/* Small value operations shared by geometry and rasterization; no owned state. */
static inline Vec3 vadd(Vec3 a, Vec3 b) { return (Vec3){{a.x[0]+b.x[0],a.x[1]+b.x[1],a.x[2]+b.x[2]}}; }
static inline Vec3 vsub(Vec3 a, Vec3 b) { return (Vec3){{a.x[0]-b.x[0],a.x[1]-b.x[1],a.x[2]-b.x[2]}}; }
static inline Vec3 vmul(Vec3 a, double s) { return (Vec3){{a.x[0]*s,a.x[1]*s,a.x[2]*s}}; }
static inline double dot(Vec3 a, Vec3 b) { return a.x[0]*b.x[0]+a.x[1]*b.x[1]+a.x[2]*b.x[2]; }
static inline Vec3 cross(Vec3 a, Vec3 b) { return (Vec3){{a.x[1]*b.x[2]-a.x[2]*b.x[1],a.x[2]*b.x[0]-a.x[0]*b.x[2],a.x[0]*b.x[1]-a.x[1]*b.x[0]}}; }
static inline double length(Vec3 a) { return hypot(hypot(a.x[0],a.x[1]),a.x[2]); }
static inline Vec3 unit(Vec3 a) { return vmul(a,1/length(a)); }
static inline Vec3 cartesian(const Vec3 basis[3], Vec3 f) { return vadd(vadd(vmul(basis[0],f.x[0]),vmul(basis[1],f.x[1])),vmul(basis[2],f.x[2])); }
#endif
