/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include "vec.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>

/* Camera rays run along -z. Return the frontmost finite-cylinder hit, testing
 * both the curved wall and flat end caps, including axes parallel to the ray. */
static bool cylinder_hit(Vec3 a, Vec3 b, double r, double x, double y, double *depth, Vec3 *normal)
{
    Vec3 axis = vsub(b,a);
    double len = length(axis);
    if (len <= 1e-14) return false;
    Vec3 u = vmul(axis,1/len);
    double dx=x-a.x[0], dy=y-a.x[1], xy=dx*u.x[0]+dy*u.x[1];
    double aa=u.x[0]*u.x[0]+u.x[1]*u.x[1];
    double bb=-2*u.x[2]*xy, cc=dx*dx+dy*dy-xy*xy-r*r;
    double best = -DBL_MAX;
    Vec3 n = {{0,0,0}};
    if (aa > 1e-24) {
        double disc=bb*bb-4*aa*cc;
        if (disc >= 0) {
            double root=sqrt(disc), q=-0.5*(bb+copysign(root,bb));
            double zs[2] = {q/aa, q != 0 ? cc/q : -bb/(2*aa)};
            for (int i=0;i<2;++i) {
                double z=zs[i], along=xy+z*u.x[2];
                if (along >= 0 && along <= len && a.x[2]+z > best) {
                    best=a.x[2]+z;
                    n=vmul(vsub((Vec3){{dx,dy,z}},vmul(u,along)),1/r);
                }
            }
        }
    }
    if (fabs(u.x[2]) > 1e-14) for (int end=0;end<2;++end) {
        double along=end ? len : 0, z=(along-xy)/u.x[2];
        Vec3 radial=vsub((Vec3){{dx,dy,z}},vmul(u,along));
        if (dot(radial,radial) <= r*r && a.x[2]+z > best) {
            best=a.x[2]+z; n=vmul(u,end ? 1 : -1);
        }
    }
    if (best == -DBL_MAX) return false;
    *depth=best; *normal=n;
    return true;
}

static uint8_t channel(double value)
{
    return (uint8_t)fmin(255,fmax(0,floor(value+0.5)));
}

/* Test the projected cylinder silhouette rather than normal thresholds, so cap
 * outlines work for axial and tilted bonds at the same output-pixel width. */
static bool cylinder_outline(Vec3 a, Vec3 b, double r, double x, double y, double width)
{
    static const double offsets[8][2]={{1,0},{-1,0},{0,1},{0,-1},
        {0.7071067811865475,0.7071067811865475},{-0.7071067811865475,0.7071067811865475},
        {0.7071067811865475,-0.7071067811865475},{-0.7071067811865475,-0.7071067811865475}};
    for (size_t i=0;i<8;++i) {
        double depth; Vec3 normal;
        if (!cylinder_hit(a,b,r,x+width*offsets[i][0],y+width*offsets[i][1],&depth,&normal)) return true;
    }
    return false;
}

bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *out, Error *e)
{
    if (c->threads > 1 || c->axes || c->legend || c->scale_bar || c->labels)
        return fail(e,3,"this version supports one thread and the cell outline; optional overlays are unavailable");
    size_t count=c->width*c->height, samples=count*4, retained=s->capacity*sizeof(Primitive)+source_bytes;
    size_t reserve=8u*1024u*1024u; /* PNG/zlib working-space allowance. */
    if (retained > VR_MEMORY_BUDGET-reserve || count > (VR_MEMORY_BUDGET-reserve-retained)/52)
        return fail(e,6,"scene and supersampled image exceed 1 GiB allocation budget");
    double *depth=malloc(samples*sizeof *depth);
    uint8_t *rgba=malloc(samples*4), *pixels=malloc(count*4);
    if (!depth || !rgba || !pixels) { free(depth);free(rgba);free(pixels); return fail(e,6,"image allocation failed"); }
    uint8_t background[4]; decode_color(c->background,background);
    for (size_t i=0;i<samples;++i) { depth[i]=-DBL_MAX; memcpy(rgba+4*i,background,4); }
    const size_t width=c->width*2, height=c->height*2;
    Vec3 light=unit((Vec3){{-1,-1,2}});
    double near=-DBL_MAX, far=DBL_MAX;
    for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE) {
        double z=dot(s->items[i].a,camera->view);
        near=fmax(near,z); far=fmin(far,z);
    }
    for (size_t i=0;i<s->count;++i) {
        const Primitive *p=&s->items[i];
        if (!p->color[3]) continue;
        Vec3 a=camera_project(camera,p->a), b=camera_project(camera,p->b);
        /* Optional depth cue uses each primitive's center, leaving flat bands
         * uniform within a disk. It changes colors, not visibility or connectivity. */
        double fade=near>far ? c->depth_fade*fmin(1,fmax(0,(near-(a.x[2]+b.x[2])/2)/(near-far))) : 0;
        /* Uniform scaling preserves intersection geometry and surface normals. */
        a.x[0]*=2; a.x[1]*=2; a.x[2]*=2*camera->scale;
        b.x[0]*=2; b.x[1]*=2; b.x[2]*=2*camera->scale;
        double r=p->kind==CELL_EDGE ? c->cell_width : p->radius*2*camera->scale;
        double left=fmax(0,floor(fmin(a.x[0],b.x[0])-r));
        double right=fmin((double)width,ceil(fmax(a.x[0],b.x[0])+r));
        double bottom=fmax(0,floor(fmin(a.x[1],b.x[1])-r));
        double top=fmin((double)height,ceil(fmax(a.x[1],b.x[1])+r));
        for (size_t y=(size_t)bottom;y<(size_t)top;++y) for (size_t x=(size_t)left;x<(size_t)right;++x) {
            double z; Vec3 normal;
            if (p->kind==SPHERE) {
                double dx=((double)x+0.5-a.x[0])/r, dy=((double)y+0.5-a.x[1])/r;
                double zz=1-dx*dx-dy*dy;
                if (zz < 0) continue;
                normal=(Vec3){{dx,dy,sqrt(zz)}};
                z=a.x[2]+r*normal.x[2];
            } else if (!cylinder_hit(a,b,r,(double)x+0.5,(double)y+0.5,&z,&normal)) continue;
            size_t index=(height-1-y)*width+x;
            /* Stable primitive order resolves numerically equal depths. */
            double tie = 32*DBL_EPSILON*fmax(1,fmax(fabs(z),fabs(depth[index])));
            if (z <= depth[index]+tie) continue;
            depth[index]=z;
            double shade=p->kind==CELL_EDGE ? 1 : 0.25+0.75*fmax(0,dot(normal,light));
            double white=0;
            bool ink=false;
            if (c->style==STYLE_ILLUSTRATED) {
                if (p->kind==SPHERE) {
                    double dx=((double)x+0.5-a.x[0])/r, dy=((double)y+0.5-a.x[1])/r;
                    /* Offset solid disks form a colored rim and a pale circular
                     * highlight. Intersection depth stays the actual sphere surface. */
                    shade=0.82;
                    if ((dx-0.10)*(dx-0.10)+(dy-0.10)*(dy-0.10)<0.88*0.88) {
                        shade=0.90; white=0.10;
                    }
                    if ((dx-0.16)*(dx-0.16)+(dy-0.18)*(dy-0.18)<0.47*0.47) {
                        shade=0.45; white=0.55;
                    }
                    double inner=fmax(0,1-2*c->outline_width/r);
                    ink=c->outline_width>0 && dx*dx+dy*dy>=inner*inner;
                } else if (p->kind==CYLINDER) {
                    shade=0.64;
                    ink=c->outline_width>0 && cylinder_outline(a,b,r,(double)x+0.5,(double)y+0.5,2*c->outline_width);
                }
            }
            shade*=1-fade; white=white*(1-fade)+fade;
            if (ink) { shade=0; white=0; }
            double alpha=(double)p->color[3]/255, bg_alpha=(double)background[3]/255;
            double result_alpha=alpha+bg_alpha*(1-alpha);
            for (int d=0;d<3;++d) rgba[4*index+(size_t)d]=result_alpha > 0 ?
                channel((alpha*((double)p->color[d]*shade+255*white)+(1-alpha)*bg_alpha*(double)background[d])/result_alpha) : 0;
            rgba[4*index+3]=channel(255*result_alpha);
        }
    }
    /* Average premultiplied colors to avoid white/dark halos on alpha edges. */
    for (size_t y=0;y<c->height;++y) for (size_t x=0;x<c->width;++x) {
        unsigned sum_alpha=0, sums[3]={0,0,0};
        for (size_t dy=0;dy<2;++dy) for (size_t dx=0;dx<2;++dx) {
            const uint8_t *p=rgba+4*((2*y+dy)*width+2*x+dx);
            sum_alpha+=p[3];
            for (int d=0;d<3;++d) sums[d]+=(unsigned)p[d]*(unsigned)p[3];
        }
        uint8_t *p=pixels+4*(y*c->width+x);
        p[3]=(uint8_t)((sum_alpha+2)/4);
        for (int d=0;d<3;++d) p[d]=(uint8_t)(sum_alpha ? (sums[d]+sum_alpha/2)/sum_alpha : 0u);
    }
    free(depth); free(rgba);
    out->width=c->width; out->height=c->height; out->pixels=pixels;
    return true;
}

void image_free(Image *image) { free(image->pixels); memset(image,0,sizeof *image); }
