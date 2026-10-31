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
typedef struct {
    double outline,inner_radius,root,projected,period;
    size_t count;
    bool end_on;
    Cylinder *inner,*outer;
} CellStroke;

static bool primitive_visible(const Config *c, const Primitive *p)
{
    return p->color[3] || (p->kind==CYLINDER && c->bond_colors==BONDS_ATOMS && p->color_b[3]);
}
static const uint8_t *surface_color(const Config *c, const Primitive *p, const Cylinder *cylinder,
                                  double x, double y, double z)
{
    if (p->kind==CYLINDER && c->bond_colors==BONDS_ATOMS) {
        double along=(x-cylinder->a.x[0])*cylinder->u.x[0]+(y-cylinder->a.x[1])*cylinder->u.x[1]+(z-cylinder->a.x[2])*cylinder->u.x[2];
        if (along>p->color_split*cylinder->len) return p->color_b;
    }
    return p->color;
}

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

/* Screen-sized cell strokes retain ray-tested surface depth. Each dash is a
 * finite cylinder. Its white surround extends over both sides and both ends,
 * while the black center keeps the requested width and dash interval. */
static bool cell_line_hit(const Config *c, const Primitive *p, const Cylinder *full, const CellStroke *stroke,
                          double x, double y, const uint8_t white[4], double *depth,
                          Vec3 *normal, const uint8_t **color)
{
    *color=p->color;
    if (c->cell_style==CELL_SOLID && c->cell_outline_width==0)
        return cylinder_hit(full,x,y,depth,normal);
    double scale=(double)c->aa_scale,outline=stroke ? stroke->outline : c->cell_outline_width*scale;
    double inner_radius=stroke ? stroke->inner_radius : c->cell_width*scale/2;
    double projected=stroke ? stroke->projected : full->len*sqrt(full->aa);
    if (stroke ? stroke->end_on : projected<fmax(1e-6,fmax(c->cell_width*scale,2*outline))) {
        Cylinder inner=*full;inner.r=inner_radius;
        if (cylinder_hit(&inner,x,y,depth,normal)) return true;
        *color=white;return outline>0 && cylinder_hit(full,x,y,depth,normal);
    }
    double period=stroke ? stroke->period : (c->cell_dash_length+c->cell_dash_gap)*scale;
    size_t first=0,last=0;
    if (c->cell_style==CELL_DASHED) {
        size_t count=stroke ? stroke->count : (size_t)ceil(projected/period);
        double along=((x-full->a.x[0])*full->u.x[0]+(y-full->a.x[1])*full->u.x[1])/(stroke ? stroke->root : sqrt(full->aa));
        double low=floor((along-full->r-outline)/period)-1;
        double high=floor((along+full->r+outline)/period)+1;
        first=(size_t)fmax(0,fmin((double)(count-1),low));
        last=(size_t)fmax(0,fmin((double)(count-1),high));
    }
    double best=-DBL_MAX;Vec3 best_normal={{0,0,0}};
    for (size_t dash=first;dash<=last;++dash) {
        double begin=c->cell_style==CELL_DASHED ? (double)dash*period : 0;
        double end=c->cell_style==CELL_DASHED ? fmin(projected,begin+c->cell_dash_length*scale) : projected;
        if (end<=begin) continue;
        Cylinder inner_value;const Cylinder *inner;
        if (stroke) inner=&stroke->inner[dash];
        else {
            inner_value=*full;
            inner_value.a=vadd(full->a,vmul(full->u,full->len*begin/projected));
            inner_value.len=full->len*(end-begin)/projected;inner_value.r=inner_radius;inner=&inner_value;
        }
        double z;Vec3 n;bool hit=cylinder_hit(inner,x,y,&z,&n),halo=false;
        if (!hit && outline>0) {
            Cylinder outer_value;const Cylinder *outer;
            if (stroke) outer=&stroke->outer[dash];
            else {
                outer_value=*full;
                outer_value.a=vadd(full->a,vmul(full->u,full->len*(begin-outline)/projected));
                outer_value.len=full->len*(end-begin+2*outline)/projected;outer=&outer_value;
            }
            hit=cylinder_hit(outer,x,y,&z,&n);halo=hit;
        }
        if (hit && z>best) {best=z;best_normal=n;*color=halo ? white : p->color;}
    }
    if (best==-DBL_MAX) return false;
    *depth=best;*normal=best_normal;return true;
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
        if (!primitive_visible(c,p)) continue;
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
    const CellStroke *stroke;
    double radius,fade;
    size_t left,right,bottom,top;
} RasterPrimitive;

typedef struct {CellStroke *strokes;Cylinder *segments;} CellCache;
/* Optional frame-local cache. Its complete allocation must fit the remaining
 * render budget; otherwise use the original per-sample path. Publish pointers
 * only after preparation succeeds, preserving that fallback on malloc failure. */
static CellCache cell_cache_prepare(const Config *c,RasterPrimitive *items,size_t count,size_t available)
{
    CellCache cache={0};size_t cells=0,references=0;double scale=(double)c->aa_scale;
    for (size_t i=0;i<count;++i) if (items[i].primitive->kind==CELL_EDGE) ++cells;
    if (!cells || cells>available/sizeof(CellStroke)) return cache;
    available-=cells*sizeof(CellStroke);
    cache.strokes=calloc(cells,sizeof *cache.strokes);
    if (!cache.strokes) return cache;
    size_t index=0;
    for (size_t i=0;i<count;++i) if (items[i].primitive->kind==CELL_EDGE) {
        const Cylinder *full=&items[i].cylinder;CellStroke *p=&cache.strokes[index++];
        p->outline=c->cell_outline_width*scale;p->inner_radius=c->cell_width*scale/2;
        p->root=sqrt(full->aa);p->projected=full->len*p->root;
        p->end_on=p->projected<fmax(1e-6,fmax(c->cell_width*scale,2*p->outline));
        p->period=(c->cell_dash_length+c->cell_dash_gap)*scale;
        if (p->end_on) continue;
        double n=c->cell_style==CELL_DASHED ? ceil(p->projected/p->period) : 1;
        size_t copies=p->outline>0 ? 2 : 1;
        if (!isfinite(n) || n>(double)(available/sizeof(Cylinder)/copies)) goto fallback;
        p->count=(size_t)n;size_t extra=p->count*copies;
        if (extra>available/sizeof(Cylinder)-references) goto fallback;
        references+=extra;
    }
    if (!references && available<sizeof *cache.segments) goto fallback;
    cache.segments=malloc((references ? references : 1)*sizeof *cache.segments);
    if (!cache.segments) goto fallback;
    index=0;size_t cursor=0;
    for (size_t i=0;i<count;++i) if (items[i].primitive->kind==CELL_EDGE) {
        const Cylinder *full=&items[i].cylinder;CellStroke *p=&cache.strokes[index++];
        p->inner=cache.segments+cursor;cursor+=p->count;
        if (p->outline>0) {p->outer=cache.segments+cursor;cursor+=p->count;}
        for (size_t dash=0;dash<p->count;++dash) {
            double begin=c->cell_style==CELL_DASHED ? (double)dash*p->period : 0;
            double end=c->cell_style==CELL_DASHED ? fmin(p->projected,begin+c->cell_dash_length*scale) : p->projected;
            p->inner[dash]=*full;
            p->inner[dash].a=vadd(full->a,vmul(full->u,full->len*begin/p->projected));
            p->inner[dash].len=full->len*(end-begin)/p->projected;p->inner[dash].r=p->inner_radius;
            if (p->outer) {
                p->outer[dash]=*full;
                p->outer[dash].a=vadd(full->a,vmul(full->u,full->len*(begin-p->outline)/p->projected));
                p->outer[dash].len=full->len*(end-begin+2*p->outline)/p->projected;
            }
        }
        items[i].stroke=p;
    }
    return cache;
fallback:
    free(cache.strokes);return (CellCache){0};
}

static bool label_sample_visible(const Config *c, const RasterPrimitive *items, size_t count, size_t index,
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
        } else if (p->primitive->kind==CELL_EDGE) {
            uint8_t white[4]={255,255,255,p->primitive->color[3]};const uint8_t *color;
            if (!cell_line_hit(c,p->primitive,&p->cylinder,p->stroke,x,y,white,&z,&normal,&color)) continue;
        } else if (!cylinder_hit(&p->cylinder,x,y,&z,&normal)) continue;
        if (!surface_color(c,p->primitive,&p->cylinder,x,y,z)[3]) continue;
        double tie=32*DBL_EPSILON*fmax(1,fmax(fabs(z),fabs(front)));
        if (z>front+tie || (fabs(z-front)<=tie && j<index)) return false;
    }
    return true;
}

static LabelAnchor label_anchor(const Config *c, const Scene *s, const Camera *camera, const RasterPrimitive *items, size_t count, size_t index,
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
        if (label_sample_visible(c,items,count,index,x,y,z) && !faces_occlude_label(s,camera,factor,x,y,z))
            return (LabelAnchor){x/(double)factor,(double)height-y/(double)factor,z,true};
    }
    return (LabelAnchor){0,0,0,false};
}

void render_workspace_free(RenderWorkspace *workspace)
{
    if (!workspace) return;
    free(workspace->depth);free(workspace->rgba);memset(workspace,0,sizeof *workspace);
}
static void release_samples(RenderWorkspace *workspace,double *depth,uint8_t *rgba)
{
    if (!workspace) {free(depth);free(rgba);}
}
bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *out, Error *e)
{
    return render_image_workspace(c,s,camera,source_bytes,NULL,out,e);
}
bool render_image_workspace(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes,
                            RenderWorkspace *workspace, Image *out, Error *e)
{
#ifndef _OPENMP
    if (c->threads>1) return fail(e,3,"multiple threads require ENABLE_OPENMP");
#endif
    if (c->backend==BACKEND_CAIRO) {
        render_workspace_free(workspace);
        if (s->triangle_count || c->bond_colors==BONDS_ATOMS)
            return fail(e,3,"polygon faces and atom-colored bonds require the Z-buffer backend");
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
    const bool styled_cell=c->cell && (s->cell_present[0] || s->cell_present[1] || s->cell_present[2]) &&
        (c->cell_style==CELL_DASHED || c->cell_outline_width>0);
    const bool adaptive=c->adaptive_aa && !s->triangle_count && !styled_cell;
    const double scale=(double)factor;
    size_t count=c->width*c->height, samples=count*per_pixel, retained=scene_memory_bytes(s)+source_bytes;
    size_t reserve=8u*1024u*1024u; /* PNG/zlib working-space allowance. */
    if (c->labels) reserve+=s->count*sizeof(LabelAnchor)+overlay_label_scratch_bytes(s);
    if (adaptive)
        reserve+=((c->width+AA_TILE-1)/AA_TILE)*((c->height+AA_TILE-1)/AA_TILE)*(sizeof(size_t)+2*sizeof(double)+1);
    if (retained>VR_MEMORY_BUDGET-reserve || s->count>(VR_MEMORY_BUDGET-reserve-retained)/sizeof(RasterPrimitive))
        return fail(e,6,"projected scene exceeds 1 GiB allocation budget");
    retained+=s->count*sizeof(RasterPrimitive);
    if (retained > VR_MEMORY_BUDGET-reserve || count > (VR_MEMORY_BUDGET-reserve-retained)/(12*per_pixel+4))
        return fail(e,6,"scene and supersampled image exceed 1 GiB allocation budget");
    if (workspace && workspace->samples!=samples) render_workspace_free(workspace);
    double *depth=workspace && workspace->depth ? workspace->depth : malloc(samples*sizeof *depth);
    uint8_t *rgba=workspace && workspace->rgba ? workspace->rgba : malloc(samples*4);
    if (workspace) {workspace->depth=depth;workspace->rgba=rgba;workspace->samples=samples;}
    uint8_t *pixels=malloc(count*4);
    RasterPrimitive *projected=calloc(s->count,sizeof *projected);
    LabelAnchor *visible=c->labels ? calloc(s->count,sizeof *visible) : NULL;
    if (!depth || !rgba || !pixels || !projected || (c->labels && !visible)) {
        release_samples(workspace,depth,rgba);free(pixels);free(projected);free(visible);
        return fail(e,6,"image allocation failed");
    }
    uint8_t *native_tiles=adaptive ? adaptive_tiles(c,s,camera,e) : NULL;
    if (adaptive && !native_tiles) {release_samples(workspace,depth,rgba);free(pixels);free(projected);free(visible);return false; }
    if (c->adaptive_aa && !adaptive && c->benchmark) fprintf(stderr,"adaptive_native_tiles=0 adaptive_total_tiles=%zu\n",((c->width+AA_TILE-1)/AA_TILE)*((c->height+AA_TILE-1)/AA_TILE));
    const size_t tile_columns=(c->width+AA_TILE-1)/AA_TILE;
    uint8_t background[4]; decode_color(c->background,background);
    int workers=1;
#ifdef _OPENMP
    int automatic=omp_get_max_threads();
    if (automatic>omp_get_num_procs()) automatic=omp_get_num_procs();
    if (automatic>8) automatic=8;
    if (count<512u*512u) automatic=1;
    workers=c->threads ? (int)c->threads : automatic;
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
        if (!primitive_visible(c,p)) continue;
        Vec3 a=camera_project(camera,p->a), b=camera_project(camera,p->b);
        /* Optional depth cue uses each primitive's center, leaving flat bands
         * uniform within a disk. It changes colors, not visibility or connectivity. */
        double fade=near>far ? c->depth_fade*fmin(1,fmax(0,(near-(a.x[2]+b.x[2])/2)/(near-far))) : 0;
        /* Uniform scaling preserves intersection geometry and surface normals. */
        a.x[0]*=scale; a.x[1]*=scale; a.x[2]*=scale*camera->scale;
        b.x[0]*=scale; b.x[1]*=scale; b.x[2]*=scale*camera->scale;
        double r=p->kind==CELL_EDGE ? (c->cell_width/2+c->cell_outline_width)*scale : p->radius*scale*camera->scale;
        double padding=p->kind==CELL_EDGE ? r+c->cell_outline_width*scale : r;
        double left=fmax(0,floor(fmin(a.x[0],b.x[0])-padding));
        double right=fmin((double)width,ceil(fmax(a.x[0],b.x[0])+padding));
        double bottom=fmax(0,floor(fmin(a.x[1],b.x[1])-padding));
        double top=fmin((double)height,ceil(fmax(a.x[1],b.x[1])+padding));
        rp->a=a;rp->radius=r;rp->fade=fade;
        rp->left=(size_t)left;rp->right=(size_t)right;rp->bottom=(size_t)bottom;rp->top=(size_t)top;
        if (p->kind!=SPHERE) rp->cylinder=cylinder_prepare(a,b,r);
    }
    /* Each task owns a disjoint 32x32 output-pixel tile. Primitive order within
     * a tile is unchanged, so depth ties and RGBA rounding are deterministic. */
    const size_t raster_columns=(c->width+31)/32, raster_rows=(c->height+31)/32;
    CellCache cell_cache={0};
    if (styled_cell) cell_cache=cell_cache_prepare(c,projected,s->count,VR_MEMORY_BUDGET-retained-reserve-samples*12-count*4);
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
            double inner=fmax(0,1-scale*c->outline_width/r);
            double alpha=(double)p->color[3]/255, bg_alpha=(double)background[3]/255;
            double result_alpha=alpha+bg_alpha*(1-alpha);
            uint8_t cell_white[4]={255,255,255,p->color[3]};
            for (size_t y=bottom;y<top;++y) for (size_t x=left;x<right;++x) {
                bool native=native_tiles && native_tiles[(y/2/AA_TILE)*tile_columns+x/2/AA_TILE];
                if (native && ((x%2) || (y%2))) continue;
                double sx=(double)x+(native ? 1 : 0.5), sy=(double)y+(native ? 1 : 0.5);
                double z; Vec3 normal;const uint8_t *color=p->color;
                if (p->kind==SPHERE) {
                    double dx=(sx-a.x[0])/r, dy=(sy-a.x[1])/r;
                    double zz=1-dx*dx-dy*dy;
                    if (zz < 0) continue;
                    normal=(Vec3){{dx,dy,sqrt(zz)}};
                    z=a.x[2]+r*normal.x[2];
                } else if (p->kind==CELL_EDGE) {
                    if (!cell_line_hit(c,p,cylinder,rp->stroke,sx,sy,cell_white,&z,&normal,&color)) continue;
                } else {
                    if (!cylinder_hit(cylinder,sx,sy,&z,&normal)) continue;
                    color=surface_color(c,p,cylinder,sx,sy,z);
                }
                if (!color[3]) continue;
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
                        ink=c->outline_width>0 && dx*dx+dy*dy>=inner*inner;
                    } else if (p->kind==CYLINDER) {
                        shade=0.64;
                        ink=c->outline_width>0 && cylinder_outline(cylinder,sx,sy,scale*c->outline_width);
                    }
                }
                if (c->style==STYLE_ORTEP || c->style==STYLE_BALSAC) {
                    if (p->kind==SPHERE) {
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
                        if (c->style==STYLE_ORTEP) {shade=c->bond_colors==BONDS_ATOMS ? 0.64 : 0;white=0;}
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
                if (color[3]==255 || s->triangle_count) {
                    for (size_t d=0;d<3;++d) rgba[4*index+d]=toned ? tone_rgb[d] :
                        channel((double)color[d]*shade+255*white);
                    rgba[4*index+3]=color[3];
                } else {
                    double sample_alpha=color==p->color ? alpha : (double)color[3]/255;
                    double sample_result_alpha=color==p->color ? result_alpha : sample_alpha+bg_alpha*(1-sample_alpha);
                    for (int d=0;d<3;++d) rgba[4*index+(size_t)d]=sample_result_alpha > 0 ?
                        channel((sample_alpha*(toned ? (double)tone_rgb[d] : (double)color[d]*shade+255*white)+(1-sample_alpha)*bg_alpha*(double)background[d])/sample_result_alpha) : 0;
                    rgba[4*index+3]=channel(255*sample_result_alpha);
                }
            }
        }
    }
    if (c->benchmark) fprintf(stderr,"render_threads=%zu raster_tiles=%zu\n",used_threads,raster_columns*raster_rows);
    /* Release optional cache before polygon scratch allocation; label visibility
     * can use the fallback without extending these buffers' memory lifetime. */
    if (cell_cache.strokes) {
        for (size_t i=0;i<s->count;++i) projected[i].stroke=NULL;
        free(cell_cache.segments);free(cell_cache.strokes);
    }
    if (s->triangle_count && !faces_render_samples(c,s,camera,workers,retained+reserve+samples*12+count*4,depth,rgba,e)) {
        free(projected);free(native_tiles);release_samples(workspace,depth,rgba);free(pixels);free(visible);return false;
    }
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
        if (s->items[i].kind==SPHERE) visible[i]=label_anchor(c,s,camera,projected,s->count,i,factor,c->height);
    free(projected);free(native_tiles);release_samples(workspace,depth,rgba);
    out->width=c->width; out->height=c->height; out->pixels=pixels;out->threads_used=used_threads;
    bool ok=overlay_render(c,s,camera,visible,out,e);
    free(visible);
    if (!ok) image_free(out);
    return ok;
}
