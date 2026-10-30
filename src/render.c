/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include "overlay.h"
#include "vec.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#ifdef _OPENMP
#include <omp.h>
#endif

typedef struct { Vec3 a,u; double len,r,aa; } Cylinder;

static Cylinder cylinder_prepare(Vec3 a, Vec3 b, double r)
{
    Vec3 axis=vsub(b,a);
    double len=length(axis);
    Vec3 u=len>1e-14 ? vmul(axis,1/len) : (Vec3){{0,0,0}};
    return (Cylinder){a,u,len,r,u.x[0]*u.x[0]+u.x[1]*u.x[1]};
}

/* Camera rays run along -z. Return the frontmost finite-cylinder hit, testing
 * both the curved wall and flat end caps, including axes parallel to the ray. */
static bool cylinder_hit(const Cylinder *cylinder, double x, double y, double *depth, Vec3 *normal)
{
    Vec3 a=cylinder->a, u=cylinder->u;
    double len=cylinder->len, r=cylinder->r;
    if (len <= 1e-14) return false;
    double dx=x-a.x[0], dy=y-a.x[1], xy=dx*u.x[0]+dy*u.x[1];
    double aa=cylinder->aa;
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
static bool cylinder_outline(const Cylinder *cylinder, double x, double y, double width)
{
    static const double offsets[8][2]={{1,0},{-1,0},{0,1},{0,-1},
        {0.7071067811865475,0.7071067811865475},{-0.7071067811865475,0.7071067811865475},
        {0.7071067811865475,-0.7071067811865475},{-0.7071067811865475,-0.7071067811865475}};
    for (size_t i=0;i<8;++i) {
        double depth; Vec3 normal;
        if (!cylinder_hit(cylinder,x+width*offsets[i][0],y+width*offsets[i][1],&depth,&normal)) return true;
    }
    return false;
}

#define AA_TILE 4u

/* Output-pixel ordered dithering: every supersample of one output pixel shares
 * a threshold. This retains dots after AA and is independent of tile/thread
 * scheduling, primitive order, and camera scale. Full/empty coverage are exact. */
static double dither_threshold(size_t x, size_t y)
{
    static const unsigned char bayer[8][8]={
        {0,48,12,60,3,51,15,63},{32,16,44,28,35,19,47,31},
        {8,56,4,52,11,59,7,55},{40,24,36,20,43,27,39,23},
        {2,50,14,62,1,49,13,61},{34,18,46,30,33,17,45,29},
        {10,58,6,54,9,57,5,53},{42,26,38,22,41,25,37,21}};
    return ((double)bayer[y%8][x%8]+0.5)/64;
}

/* Principal sections of a sphere are three world-axis great circles. Use only
 * the front surface normal, so back arcs are never drawn. The plane residual
 * divided by its projected gradient approximates distance in screen pixels;
 * unlike an angular band, this keeps the line thin through foreshortening. */
static bool sphere_sections(Vec3 normal, double radius, const Vec3 axes[3], double width)
{
    if (width<=0) return false;
    for (size_t k=0;k<3;++k) {
        Vec3 axis=axes[k];
        double gx=axis.x[0]*normal.x[2]-axis.x[2]*normal.x[0];
        double gy=axis.x[1]*normal.x[2]-axis.x[2]*normal.x[1];
        double gradient=hypot(gx,gy);
        if (gradient>1e-12 && fabs(dot(axis,normal))*radius*normal.x[2]<=0.5*width*gradient)
            return true;
    }
    return false;
}

/* A circle intersects the closed tile rectangle's interior/exterior boundary.
 * This conservative interval test includes small circles missed by a center ray. */
static bool circle_crosses(double x, double y, double r, double x0, double y0, double x1, double y1)
{
    if (r<=0) return false;
    double dx=x-fmax(x0,fmin(x1,x)), dy=y-fmax(y0,fmin(y1,y));
    double fx=fmax(fabs(x-x0),fabs(x-x1)), fy=fmax(fabs(y-y0),fabs(y-y1));
    return dx*dx+dy*dy<=r*r && fx*fx+fy*fy>=r*r;
}

/* Only sphere interiors with proven surface-depth separation qualify for one
 * center ray. Every ambiguous overlap, bond, cell edge, silhouette or illustrated
 * band edge keeps all four independent depth samples. Primitive center ordering
 * never proves visibility. Shaded interior approximation is experimental. */
static uint8_t *adaptive_tiles(const Config *c, const Scene *s, const Camera *camera, Error *e)
{
    size_t columns=(c->width+AA_TILE-1)/AA_TILE, rows=(c->height+AA_TILE-1)/AA_TILE;
    size_t count=columns*rows;
    size_t *owner=malloc(count*sizeof *owner);
    double *front=malloc(count*sizeof *front), *second=malloc(count*sizeof *second);
    uint8_t *native=calloc(count,1);
    if (!owner || !native || !front || !second) {
        free(owner);free(native);free(front);free(second);
        fail(e,6,"adaptive tile allocation failed");return NULL;
    }
    for (size_t k=0;k<count;++k) { owner[k]=SIZE_MAX;front[k]=second[k]=-DBL_MAX; }
    for (size_t i=0;i<s->count;++i) {
        const Primitive *p=&s->items[i];
        if (!p->color[3]) continue;
        Vec3 a=camera_project(camera,p->a), b=camera_project(camera,p->b);
        double r=p->kind==CELL_EDGE ? c->cell_width/2 : p->radius*camera->scale;
        double left=fmax(0,fmin((double)c->width,fmin(a.x[0],b.x[0])-r));
        double right=fmax(0,fmin((double)c->width,fmax(a.x[0],b.x[0])+r));
        double bottom=fmax(0,fmin((double)c->height,fmin(a.x[1],b.x[1])-r));
        double top=fmax(0,fmin((double)c->height,fmax(a.x[1],b.x[1])+r));
        if (left>=right || bottom>=top) continue;
        size_t x0=(size_t)floor(left/AA_TILE), x1=(size_t)ceil(right/AA_TILE);
        size_t y0=(size_t)floor(bottom/AA_TILE), y1=(size_t)ceil(top/AA_TILE);
        for (size_t y=y0;y<y1;++y) for (size_t x=x0;x<x1;++x) {
            size_t k=y*columns+x;
            double upper=fmax(a.x[2],b.x[2])+r/camera->scale;
            if (p->kind==SPHERE) {
                double tx=(double)(x*AA_TILE), ty=(double)(y*AA_TILE);
                double dx=a.x[0]-fmax(tx,fmin(tx+AA_TILE,a.x[0]));
                double dy=a.x[1]-fmax(ty,fmin(ty+AA_TILE,a.x[1]));
                double zz=r*r-dx*dx-dy*dy;
                if (zz<0) continue;
                upper=a.x[2]+sqrt(zz)/camera->scale;
            }
            if (upper>front[k]) {
                second[k]=front[k];front[k]=upper;owner[k]=i;
            } else second[k]=fmax(second[k],upper);
        }
    }
    size_t selected=0;
    for (size_t y=0;y<rows;++y) for (size_t x=0;x<columns;++x) {
        size_t k=y*columns+x;
        if (owner[k]>=s->count) continue;
        const Primitive *p=&s->items[owner[k]];
        if (p->kind!=SPHERE) continue;
        /* Fine arcs and smooth highlights need all supersamples. Adaptive mode
         * conservatively falls back to ordinary 2x sampling for these styles. */
        if (c->style==STYLE_ORTEP || c->style==STYLE_BALSAC) continue;
        Vec3 a=camera_project(camera,p->a);
        double r=p->radius*camera->scale;
        double x0=(double)(x*AA_TILE), x1=fmin((double)c->width,x0+AA_TILE);
        double y0=(double)(y*AA_TILE), y1=fmin((double)c->height,y0+AA_TILE);
        double dx=fmax(fabs(a.x[0]-x0),fabs(a.x[0]-x1));
        double dy=fmax(fabs(a.x[1]-y0),fabs(a.x[1]-y1));
        if (r<=1 || dx*dx+dy*dy>=(r-1)*(r-1)) continue;
        double lower=a.x[2]+sqrt(r*r-dx*dx-dy*dy)/camera->scale;
        double tie=32*DBL_EPSILON*fmax(1,fmax(fabs(lower),fabs(second[k])));
        if (lower<=second[k]+tie) continue;
        if (c->style==STYLE_ILLUSTRATED &&
            (circle_crosses(a.x[0],a.x[1],fmax(0,r-c->outline_width),x0,y0,x1,y1) ||
             circle_crosses(a.x[0]+0.10*r,a.x[1]+0.10*r,0.88*r,x0,y0,x1,y1) ||
             circle_crosses(a.x[0]+0.16*r,a.x[1]+0.18*r,0.47*r,x0,y0,x1,y1))) continue;
        native[k]=1;++selected;
    }
    if (c->benchmark) fprintf(stderr,"adaptive_native_tiles=%zu adaptive_total_tiles=%zu\n",selected,count);
    free(owner);free(front);free(second);return native;
}

typedef struct {
    const Primitive *primitive;
    Vec3 a;
    Cylinder cylinder;
    double radius,fade;
    size_t left,right,bottom,top;
} RasterPrimitive;

static bool label_sample_visible(const RasterPrimitive *items, size_t count, size_t index,
                                 double x, double y, double front)
{
    const RasterPrimitive *target=&items[index];
    if (!target->primitive->color[3]) return false;
    for (size_t j=0;j<count;++j) {
        const RasterPrimitive *p=&items[j];
        if (j==index || x<(double)p->left || x>=(double)p->right ||
            y<(double)p->bottom || y>=(double)p->top) continue;
        double z;Vec3 normal;
        if (p->primitive->kind==SPHERE) {
            double dx=(x-p->a.x[0])/p->radius,dy=(y-p->a.x[1])/p->radius;
            double zz=1-dx*dx-dy*dy;
            if (zz<0) continue;
            z=p->a.x[2]+p->radius*sqrt(zz);
        } else if (!cylinder_hit(&p->cylinder,x,y,&z,&normal)) continue;
        double tie=32*DBL_EPSILON*fmax(1,fmax(fabs(z),fabs(front)));
        if (z>front+tie || (fabs(z-front)<=tie && j<index)) return false;
    }
    return true;
}

static LabelAnchor label_anchor(const RasterPrimitive *items, size_t count, size_t index,
                                size_t factor, size_t height)
{
    /* Center first, then eight points on a 55%-radius ring. These clear narrow
     * periodic bond caps while staying away from silhouette sampling uncertainty. */
    static const double offsets[VR_LABEL_SAMPLES][2]={{0,0},{0.55,0},{-0.55,0},{0,0.55},{0,-0.55},
        {0.388908729652601,0.388908729652601},{-0.388908729652601,0.388908729652601},
        {0.388908729652601,-0.388908729652601},{-0.388908729652601,-0.388908729652601}};
    const RasterPrimitive *p=&items[index];
    if (!p->primitive->color[3] || p->radius<=0) return (LabelAnchor){0,0,0,false};
    for (size_t k=0;k<VR_LABEL_SAMPLES;++k) {
        double dx=offsets[k][0],dy=offsets[k][1];
        double x=p->a.x[0]+p->radius*dx,y=p->a.x[1]+p->radius*dy;
        double nx=(x-p->a.x[0])/p->radius,ny=(y-p->a.x[1])/p->radius;
        double zz=1-nx*nx-ny*ny;
        if (zz<0) continue;
        double z=p->a.x[2]+p->radius*sqrt(zz);
        if (label_sample_visible(items,count,index,x,y,z))
            return (LabelAnchor){x/(double)factor,(double)height-y/(double)factor,z,true};
    }
    return (LabelAnchor){0,0,0,false};
}

bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *out, Error *e)
{
#ifndef _OPENMP
    if (c->threads>1) return fail(e,3,"multiple threads require ENABLE_OPENMP");
#endif
    if (c->backend==BACKEND_CAIRO) {
        if (c->style==STYLE_ORTEP || c->style==STYLE_BALSAC)
            return fail(e,3,"ortep and balsac styles require the accurate Z-buffer backend");
        if (c->axes || c->legend || c->scale_bar || c->labels)
            return fail(e,3,"annotations require the accurate Z-buffer backend");
        if (c->threads>1) return fail(e,3,"the experimental Cairo painter supports one thread");
#ifdef VR_HAVE_CAIRO
        return render_cairo(c,s,camera,source_bytes,out,e);
#else
        return fail(e,3,"Cairo painter requires ENABLE_CAIRO_EXPERIMENTS");
#endif
    }
    const size_t factor=c->aa_scale, per_pixel=factor*factor;
    const double scale=(double)factor;
    size_t count=c->width*c->height, samples=count*per_pixel, retained=scene_memory_bytes(s)+source_bytes;
    size_t reserve=8u*1024u*1024u; /* PNG/zlib working-space allowance. */
    if (c->labels) reserve+=s->count*sizeof(LabelAnchor)+overlay_label_scratch_bytes(s);
    if (c->adaptive_aa)
        reserve+=((c->width+AA_TILE-1)/AA_TILE)*((c->height+AA_TILE-1)/AA_TILE)*(sizeof(size_t)+2*sizeof(double)+1);
    if (retained>VR_MEMORY_BUDGET-reserve || s->count>(VR_MEMORY_BUDGET-reserve-retained)/sizeof(RasterPrimitive))
        return fail(e,6,"projected scene exceeds 1 GiB allocation budget");
    retained+=s->count*sizeof(RasterPrimitive);
    if (retained > VR_MEMORY_BUDGET-reserve || count > (VR_MEMORY_BUDGET-reserve-retained)/(12*per_pixel+4))
        return fail(e,6,"scene and supersampled image exceed 1 GiB allocation budget");
    double *depth=malloc(samples*sizeof *depth);
    uint8_t *rgba=malloc(samples*4), *pixels=malloc(count*4);
    RasterPrimitive *projected=calloc(s->count,sizeof *projected);
    LabelAnchor *visible=c->labels ? calloc(s->count,sizeof *visible) : NULL;
    if (!depth || !rgba || !pixels || !projected || (c->labels && !visible)) {
        free(depth);free(rgba);free(pixels);free(projected);free(visible);
        return fail(e,6,"image allocation failed");
    }
    uint8_t *native_tiles=c->adaptive_aa ? adaptive_tiles(c,s,camera,e) : NULL;
    if (c->adaptive_aa && !native_tiles) { free(depth);free(rgba);free(pixels);free(projected);free(visible);return false; }
    const size_t tile_columns=(c->width+AA_TILE-1)/AA_TILE;
    uint8_t background[4]; decode_color(c->background,background);
#ifdef _OPENMP
    int automatic=omp_get_max_threads();
    if (automatic>omp_get_num_procs()) automatic=omp_get_num_procs();
    if (automatic>8) automatic=8;
    if (count<512u*512u) automatic=1;
    int workers=c->threads ? (int)c->threads : automatic;
#pragma omp parallel for schedule(static) num_threads(workers) if(workers>1 && samples>=1048576)
#endif
    for (size_t i=0;i<samples;++i) { depth[i]=-DBL_MAX; memcpy(rgba+4*i,background,4); }
    const size_t width=c->width*factor, height=c->height*factor;
    Vec3 light=unit((Vec3){{-1,-1,2}});
    Vec3 balsac_light=unit((Vec3){{1,1,2}});
    Vec3 section_axes[3];
    for (size_t k=0;k<3;++k)
        section_axes[k]=(Vec3){{camera->right.x[k],camera->up.x[k],camera->view.x[k]}};
    double near=-DBL_MAX, far=DBL_MAX;
    for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE) {
        double z=dot(s->items[i].a,camera->view);
        near=fmax(near,z); far=fmin(far,z);
    }
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(workers) if(workers>1 && s->count>=4096)
#endif
    for (size_t i=0;i<s->count;++i) {
        const Primitive *p=&s->items[i];
        RasterPrimitive *rp=&projected[i];rp->primitive=p;
        if (!p->color[3]) continue;
        Vec3 a=camera_project(camera,p->a), b=camera_project(camera,p->b);
        /* Optional depth cue uses each primitive's center, leaving flat bands
         * uniform within a disk. It changes colors, not visibility or connectivity. */
        double fade=near>far ? c->depth_fade*fmin(1,fmax(0,(near-(a.x[2]+b.x[2])/2)/(near-far))) : 0;
        /* Uniform scaling preserves intersection geometry and surface normals. */
        a.x[0]*=scale; a.x[1]*=scale; a.x[2]*=scale*camera->scale;
        b.x[0]*=scale; b.x[1]*=scale; b.x[2]*=scale*camera->scale;
        double r=p->kind==CELL_EDGE ? c->cell_width*scale/2 : p->radius*scale*camera->scale;
        double left=fmax(0,floor(fmin(a.x[0],b.x[0])-r));
        double right=fmin((double)width,ceil(fmax(a.x[0],b.x[0])+r));
        double bottom=fmax(0,floor(fmin(a.x[1],b.x[1])-r));
        double top=fmin((double)height,ceil(fmax(a.x[1],b.x[1])+r));
        rp->a=a;rp->radius=r;rp->fade=fade;
        rp->left=(size_t)left;rp->right=(size_t)right;rp->bottom=(size_t)bottom;rp->top=(size_t)top;
        if (p->kind!=SPHERE) rp->cylinder=cylinder_prepare(a,b,r);
    }
    /* Each task owns a disjoint 32x32 output-pixel tile. Primitive order within
     * a tile is unchanged, so depth ties and RGBA rounding are deterministic. */
    const size_t raster_columns=(c->width+31)/32, raster_rows=(c->height+31)/32;
    size_t used_threads=1;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1) num_threads(workers) if(workers>1)
#endif
    for (size_t tile=0;tile<raster_columns*raster_rows;++tile) {
        if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) continue;
#ifdef _OPENMP
        if (tile==0) used_threads=(size_t)omp_get_num_threads();
#endif
        size_t x0=(tile%raster_columns)*32*factor, y0=(tile/raster_columns)*32*factor;
        size_t x1=x0+32*factor<width ? x0+32*factor : width;
        size_t y1=y0+32*factor<height ? y0+32*factor : height;
        for (size_t i=0;i<s->count;++i) {
            const RasterPrimitive *rp=&projected[i];
            const Primitive *p=rp->primitive;
            size_t left=rp->left>x0 ? rp->left : x0, right=rp->right<x1 ? rp->right : x1;
            size_t bottom=rp->bottom>y0 ? rp->bottom : y0, top=rp->top<y1 ? rp->top : y1;
            if (left>=right || bottom>=top) continue;
            Vec3 a=rp->a;double r=rp->radius,fade=rp->fade;
            const Cylinder *cylinder=&rp->cylinder;
            for (size_t y=bottom;y<top;++y) for (size_t x=left;x<right;++x) {
                bool native=native_tiles && native_tiles[(y/2/AA_TILE)*tile_columns+x/2/AA_TILE];
                if (native && ((x%2) || (y%2))) continue;
                double sx=(double)x+(native ? 1 : 0.5), sy=(double)y+(native ? 1 : 0.5);
                double z; Vec3 normal;
                if (p->kind==SPHERE) {
                    double dx=(sx-a.x[0])/r, dy=(sy-a.x[1])/r;
                    double zz=1-dx*dx-dy*dy;
                    if (zz < 0) continue;
                    normal=(Vec3){{dx,dy,sqrt(zz)}};
                    z=a.x[2]+r*normal.x[2];
                } else if (!cylinder_hit(cylinder,sx,sy,&z,&normal)) continue;
                size_t index=(height-1-y)*width+x;
                /* Stable primitive order resolves numerically equal depths. */
                double tie = 32*DBL_EPSILON*fmax(1,fmax(fabs(z),fabs(depth[index])));
                if (z <= depth[index]+tie) continue;
                depth[index]=z;
                double shade=p->kind==CELL_EDGE ? 1 : 0.25+0.75*fmax(0,dot(normal,light));
                double white=0;
                double tone_level=0;
                bool ink=false;
                if (c->style==STYLE_ILLUSTRATED) {
                    if (p->kind==SPHERE) {
                        double dx=(sx-a.x[0])/r, dy=(sy-a.x[1])/r;
                        /* Offset solid disks form a colored rim and a pale circular
                         * highlight. Intersection depth stays the actual sphere surface. */
                        shade=0.82;
                        if ((dx-0.10)*(dx-0.10)+(dy-0.10)*(dy-0.10)<0.88*0.88) {
                            shade=0.90; white=0.10;
                        }
                        if ((dx-0.16)*(dx-0.16)+(dy-0.18)*(dy-0.18)<0.47*0.47) {
                            shade=0.45; white=0.55;
                        }
                        double inner=fmax(0,1-scale*c->outline_width/r);
                        ink=c->outline_width>0 && dx*dx+dy*dy>=inner*inner;
                    } else if (p->kind==CYLINDER) {
                        shade=0.64;
                        ink=c->outline_width>0 && cylinder_outline(cylinder,sx,sy,scale*c->outline_width);
                    }
                }
                if (c->style==STYLE_ORTEP || c->style==STYLE_BALSAC) {
                    if (p->kind==SPHERE) {
                        double inner=fmax(0,1-scale*c->outline_width/r);
                        ink=c->outline_width>0 && normal.x[0]*normal.x[0]+normal.x[1]*normal.x[1]>=inner*inner;
                        if (c->style==STYLE_ORTEP) {
                            shade=0;white=1;
                            tone_level=0.45+0.50*fmax(0,dot(normal,balsac_light));
                            ink=ink || sphere_sections(normal,r,section_axes,scale*c->outline_width);
                        } else {
                            /* Broad white highlight over smoothly lit element color. */
                            double diffuse=fmax(0,dot(normal,balsac_light));
                            double highlight=pow(diffuse,5)*0.75;
                            shade=(0.78+0.22*diffuse)*(1-highlight);white=highlight;
                            tone_level=0.28+0.50*diffuse+0.22*pow(diffuse,5);
                        }
                    } else if (p->kind==CYLINDER) {
                        ink=c->outline_width>0 && cylinder_outline(cylinder,sx,sy,scale*c->outline_width);
                        if (c->style==STYLE_ORTEP) {shade=0;white=0;}
                        else {shade=0.45;white=0.55;tone_level=0.80;}
                    }
                }
                shade*=1-fade; white=white*(1-fade)+fade;
                if (ink) { shade=0; white=0; }
                uint8_t tone_rgb[3];
                bool toned=c->tone!=TONE_COLOR && p->kind!=CELL_EDGE;
                if (toned) {
                    static const uint8_t dark[3]={32,48,80}, pale[3]={244,189,105};
                    double level=ink ? 0 : tone_level*(1-fade)+fade;
                    bool light_pixel=level>dither_threshold(x/factor,(height-1-y)/factor);
                    for (size_t d=0;d<3;++d)
                        tone_rgb[d]=c->tone==TONE_MONOCOLOR ? (light_pixel ? 255 : 0) : (light_pixel ? pale[d] : dark[d]);
                }
                double alpha=(double)p->color[3]/255, bg_alpha=(double)background[3]/255;
                double result_alpha=alpha+bg_alpha*(1-alpha);
                for (int d=0;d<3;++d) rgba[4*index+(size_t)d]=result_alpha > 0 ?
                    channel((alpha*(toned ? (double)tone_rgb[d] : (double)p->color[d]*shade+255*white)+(1-alpha)*bg_alpha*(double)background[d])/result_alpha) : 0;
                rgba[4*index+3]=channel(255*result_alpha);
            }
        }
    }
    if (c->benchmark) fprintf(stderr,"render_threads=%zu raster_tiles=%zu\n",used_threads,raster_columns*raster_rows);
    /* Average premultiplied colors to avoid white/dark halos on alpha edges. */
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(workers) if(workers>1 && count>=262144)
#endif
    for (size_t y=0;y<c->height;++y) for (size_t x=0;x<c->width;++x) {
        if (native_tiles && native_tiles[((c->height-1-y)/AA_TILE)*tile_columns+x/AA_TILE]) {
            memcpy(pixels+4*(y*c->width+x),rgba+4*((2*y+1)*width+2*x),4);
            continue;
        }
        unsigned sum_alpha=0, sums[3]={0,0,0};
        for (size_t dy=0;dy<factor;++dy) for (size_t dx=0;dx<factor;++dx) {
            const uint8_t *p=rgba+4*((factor*y+dy)*width+factor*x+dx);
            sum_alpha+=p[3];
            for (int d=0;d<3;++d) sums[d]+=(unsigned)p[d]*(unsigned)p[3];
        }
        uint8_t *p=pixels+4*(y*c->width+x);
        p[3]=(uint8_t)((sum_alpha+per_pixel/2)/per_pixel);
        for (int d=0;d<3;++d) p[d]=(uint8_t)(sum_alpha ? (sums[d]+sum_alpha/2)/sum_alpha : 0u);
    }
    if (c->labels) for (size_t i=0;i<s->count;++i)
        if (s->items[i].kind==SPHERE) visible[i]=label_anchor(projected,s->count,i,factor,c->height);
    free(projected);free(native_tiles);free(depth); free(rgba);
    out->width=c->width; out->height=c->height; out->pixels=pixels;out->threads_used=used_threads;
    bool ok=overlay_render(c,s,camera,visible,out,e);
    free(visible);
    if (!ok) image_free(out);
    return ok;
}

void image_free(Image *image) { free(image->pixels); memset(image,0,sizeof *image); }
