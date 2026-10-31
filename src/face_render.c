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

#define FACE_LAYERS 64u
#define FACE_WORK 200000000u
typedef struct {
    Vec3 v[3];
    double a[3],b[3],c[3],outline[3],area;
    bool inclusive[3];
    uint8_t boundary;
    size_t left,right,bottom,top;
    const FaceTriangle *face;
} ProjectedFace;
typedef struct {double z;size_t instance;const uint8_t *color;} FaceHit;
static uint8_t channel(double v) {return (uint8_t)fmin(255,fmax(0,floor(v+0.5)));}

static void project(const FaceTriangle *f, const Camera *camera, size_t factor, ProjectedFace *p)
{
    p->face=f;p->boundary=f->edges;
    for (size_t k=0;k<3;++k) {
        p->v[k]=camera_project(camera,f->vertices[k]);
        p->v[k].x[0]*=(double)factor;p->v[k].x[1]*=(double)factor;p->v[k].x[2]*=(double)factor*camera->scale;
    }
    double area=(p->v[1].x[0]-p->v[0].x[0])*(p->v[2].x[1]-p->v[0].x[1])-
                (p->v[1].x[1]-p->v[0].x[1])*(p->v[2].x[0]-p->v[0].x[0]);
    if (area<0) {
        Vec3 temp=p->v[1];p->v[1]=p->v[2];p->v[2]=temp;
        p->boundary=(uint8_t)(((f->edges&1u)<<2)|(f->edges&2u)|((f->edges&4u)>>2));
    }
    for (size_t k=0;k<3;++k) {
        Vec3 a=p->v[k],b=p->v[(k+1)%3];
        p->a[k]=a.x[1]-b.x[1];p->b[k]=b.x[0]-a.x[0];p->c[k]=a.x[0]*b.x[1]-b.x[0]*a.x[1];
        p->outline[k]=hypot(p->a[k],p->b[k])*f->outline_width*(double)factor;
        p->inclusive[k]=b.x[1]>a.x[1] || (b.x[1]==a.x[1] && b.x[0]<a.x[0]);
    }
    p->area=fabs(area);
    double left=p->v[0].x[0],right=left,bottom=p->v[0].x[1],top=bottom;
    for (size_t k=1;k<3;++k) {left=fmin(left,p->v[k].x[0]);right=fmax(right,p->v[k].x[0]);bottom=fmin(bottom,p->v[k].x[1]);top=fmax(top,p->v[k].x[1]);}
    double width=(double)(camera->width*factor),height=(double)(camera->height*factor);
    p->left=(size_t)fmin(width,fmax(0,floor(left)));p->right=(size_t)fmin(width,fmax(0,ceil(right)));
    p->bottom=(size_t)fmin(height,fmax(0,floor(bottom)));p->top=(size_t)fmin(height,fmax(0,ceil(top)));
}
static bool hit(const ProjectedFace *p, double x, double y, double *z, const uint8_t **color)
{
    if (p->area<1e-20) return false;
    double edges[3];bool outline=false;
    for (size_t k=0;k<3;++k) {
        edges[k]=p->a[k]*x+p->b[k]*y+p->c[k];
        if (edges[k]<0 || (edges[k]==0 && !p->inclusive[k])) return false;
        if ((p->boundary&(1u<<k)) && edges[k]<=p->outline[k]) outline=true;
    }
    *z=(edges[1]*p->v[0].x[2]+edges[2]*p->v[1].x[2]+edges[0]*p->v[2].x[2])/p->area;
    *color=outline ? p->face->outline_color : p->face->color;
    return (*color)[3]!=0;
}
bool faces_occlude_label(const Scene *s, const Camera *camera, size_t factor, double x, double y, double z)
{
    for (size_t i=0;i<s->triangle_count;++i) {
        const FaceTriangle *f=&s->faces[i];
        if (f->color[3]!=255 && (!f->outline_width || f->outline_color[3]!=255)) continue;
        ProjectedFace p={0};project(f,camera,factor,&p);double front;const uint8_t *color;
        if (hit(&p,x,y,&front,&color) && color[3]==255 && front>z+32*DBL_EPSILON*fmax(1,fmax(fabs(front),fabs(z)))) return true;
    }
    return false;
}

bool faces_render_samples(const Config *c, const Scene *s, const Camera *camera, int workers,
                          size_t retained, double *depth, uint8_t *rgba, Error *e)
{
    size_t factor=c->aa_scale,width=c->width*factor,height=c->height*factor,tile_size=32*factor;
    size_t columns=(c->width+31)/32,rows=(c->height+31)/32,tiles=columns*rows;
    if (c->labels && s->triangle_count && s->atom_count>VR_MAX_CANDIDATES/VR_LABEL_SAMPLES/s->triangle_count)
        return fail(e,6,"polygon label visibility exceeds work budget");
    size_t bytes=s->triangle_count*sizeof(ProjectedFace)+(tiles+1)*sizeof(size_t)+tiles*sizeof(size_t);
    if (retained>VR_MEMORY_BUDGET || bytes>VR_MEMORY_BUDGET-retained) return fail(e,6,"projected polygons exceed 1 GiB allocation budget");
    ProjectedFace *projected=calloc(s->triangle_count,sizeof *projected);
    size_t *offsets=calloc(tiles+1,sizeof *offsets),*cursor=malloc(tiles*sizeof *cursor);
    uint32_t *indices=NULL;bool ok=false;size_t references=0,work=0;
    if (!projected || !offsets || !cursor) {fail(e,6,"cannot allocate polygon raster bins");goto done;}
    for (size_t i=0;i<s->triangle_count;++i) {
        if (!check_cancel(e)) goto done;
        ProjectedFace *p=&projected[i];project(&s->faces[i],camera,factor,p);
        if (p->area<1e-20 || p->left>=p->right || p->bottom>=p->top || (!p->face->color[3] && (!p->face->outline_width || !p->face->outline_color[3]))) continue;
        size_t x0=p->left/tile_size,x1=(p->right-1)/tile_size,y0=p->bottom/tile_size,y1=(p->top-1)/tile_size;
        size_t n=(x1-x0+1)*(y1-y0+1),cost=n*tile_size*tile_size;
        if (cost>FACE_WORK-work) {fail(e,6,"polygon raster work exceeds 200 million sample candidates");goto done;}
        work+=cost;references+=n;
        for (size_t y=y0;y<=y1;++y) for (size_t x=x0;x<=x1;++x) ++offsets[y*columns+x+1];
    }
    if (references>(VR_MEMORY_BUDGET-retained-bytes)/sizeof *indices) {fail(e,6,"polygon bins exceed 1 GiB allocation budget");goto done;}
    indices=malloc((references ? references : 1)*sizeof *indices);
    if (!indices) {fail(e,6,"cannot allocate polygon bin entries");goto done;}
    for (size_t i=0;i<tiles;++i) offsets[i+1]+=offsets[i];
    memcpy(cursor,offsets,tiles*sizeof *cursor);
    for (size_t i=0;i<s->triangle_count;++i) {
        const ProjectedFace *p=&projected[i];
        if (p->area<1e-20 || p->left>=p->right || p->bottom>=p->top || (!p->face->color[3] && (!p->face->outline_width || !p->face->outline_color[3]))) continue;
        for (size_t y=p->bottom/tile_size;y<=(p->top-1)/tile_size;++y) for (size_t x=p->left/tile_size;x<=(p->right-1)/tile_size;++x)
            indices[cursor[y*columns+x]++]=(uint32_t)i;
    }
    uint8_t background[4];decode_color(c->background,background);
    _Atomic int overflow=0;
#ifndef _OPENMP
    (void)workers;
#else
#pragma omp parallel for schedule(dynamic,1) num_threads(workers) if(workers>1)
#endif
    for (size_t tile=0;tile<tiles;++tile) {
        size_t x0=tile%columns*tile_size,y0=tile/columns*tile_size,x1=x0+tile_size<width ? x0+tile_size : width,y1=y0+tile_size<height ? y0+tile_size : height;
        for (size_t y=y0;y<y1;++y) {
            if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) break;
            for (size_t x=x0;x<x1;++x) {
                size_t index=(height-1-y)*width+x;FaceHit hits[FACE_LAYERS+1];size_t count=0,face_hits=0;
                /* Atom samples are uncomposited in a scene with faces, allowing
                 * transparent faces on either side of the nearest atom surface. */
                if (depth[index]!=-DBL_MAX) hits[count++]=(FaceHit){depth[index],SIZE_MAX,rgba+index*4};
                for (size_t k=offsets[tile];k<offsets[tile+1];++k) {
                    const ProjectedFace *p=&projected[indices[k]];
                    if (x<p->left || x>=p->right || y<p->bottom || y>=p->top) continue;
                    double z;const uint8_t *color;
                    if (!hit(p,(double)x+0.5,(double)y+0.5,&z,&color)) continue;
                    bool duplicate=false;
                    for (size_t h=0;h<count;++h) if (hits[h].instance==p->face->instance) {duplicate=true;break;}
                    if (duplicate) continue;
                    if (face_hits++==FACE_LAYERS) {atomic_store_explicit(&overflow,1,memory_order_relaxed);break;}
                    hits[count++]=(FaceHit){z,p->face->instance,color};
                }
                /* An uncovered sample is already the background; a sole opaque
                 * hit replaces it exactly. Keep gathering every layer above so
                 * duplicate detection and the overlap limit stay unchanged. */
                if (!count) continue;
                if (count==1 && hits[0].color[3]==255) {
                    if (hits[0].color!=rgba+index*4) memcpy(rgba+index*4,hits[0].color,4);
                    continue;
                }
                for (size_t k=1;k<count;++k) {
                    FaceHit h=hits[k];size_t j=k;
                    /* Far to near. At exact depth ties atoms win, and earlier
                     * face instances paint last. Independent of tile scheduling. */
                    while (j && (hits[j-1].z>h.z || (hits[j-1].z==h.z &&
                           (hits[j-1].instance==SIZE_MAX || (h.instance!=SIZE_MAX && hits[j-1].instance<h.instance))))) {hits[j]=hits[j-1];--j;}
                    hits[j]=h;
                }
                size_t first=0;
                for (size_t k=0;k<count;++k) if (hits[k].color[3]==255) first=k;
                double alpha=(double)background[3]/255,rgb[3];
                for (size_t d=0;d<3;++d) rgb[d]=(double)background[d]*alpha;
                for (size_t k=first;k<count;++k) {
                    double a=(double)hits[k].color[3]/255;
                    for (size_t d=0;d<3;++d) rgb[d]=(double)hits[k].color[d]*a+rgb[d]*(1-a);
                    alpha=a+alpha*(1-a);
                }
                for (size_t d=0;d<3;++d) rgba[index*4+d]=alpha>0 ? channel(rgb[d]/alpha) : 0;
                rgba[index*4+3]=channel(255*alpha);
            }
        }
    }
    if (atomic_load_explicit(&overflow,memory_order_relaxed)) fail(e,6,"more than 64 polygon layers overlap one sample");
    else ok=check_cancel(e);
done:
    free(projected);free(offsets);free(cursor);free(indices);return ok;
}
