/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include "surface_render.h"
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
typedef struct {double z;size_t instance;const uint8_t *color;} FaceHit;
static uint8_t channel(double v) {return (uint8_t)fmin(255,fmax(0,floor(v+0.5)));}

/** @brief Prepare an immutable triangle for supersampled coverage and lighting.
 * @param[in] f Borrowed world-space triangle, retained by p until rendering ends.
 * @param[in] camera Fitted camera with pixels-per-angstrom scale.
 * @param[in] factor Positive supersampling factor along each image axis.
 * @param[out] p Non-null destination; owns no storage. x/y and depth are in
 * supersample pixels, with y upward and depth increasing toward the camera.
 */
static void project(const FaceTriangle *f, const Camera *camera, size_t factor, ProjectedFace *p)
{
    p->face=f;p->boundary=f->edges;
    for (size_t k=0;k<3;++k) {
        p->v[k]=camera_project(camera,f->vertices[k]);
        p->v[k].x[0]*=(double)factor;p->v[k].x[1]*=(double)factor;p->v[k].x[2]*=(double)factor*camera->scale;
        Vec3 n=f->normals[k];p->normals[k]=(Vec3){{dot(n,camera->right),dot(n,camera->up),dot(n,camera->view)}};
    }
    double area=(p->v[1].x[0]-p->v[0].x[0])*(p->v[2].x[1]-p->v[0].x[1])-
                (p->v[1].x[1]-p->v[0].x[1])*(p->v[2].x[0]-p->v[0].x[0]);
    if (area<0) {
        Vec3 temp=p->v[1];p->v[1]=p->v[2];p->v[2]=temp;
        temp=p->normals[1];p->normals[1]=p->normals[2];p->normals[2]=temp;
        p->boundary=(uint8_t)(((p->boundary&1u)<<2)|(p->boundary&2u)|((p->boundary&4u)>>2));
    }
    for (size_t k=0;k<3;++k) {
        Vec3 a=p->v[k],b=p->v[(k+1)%3];
        p->a[k]=a.x[1]-b.x[1];p->b[k]=b.x[0]-a.x[0];p->c[k]=a.x[0]*b.x[1]-b.x[0]*a.x[1];
        p->outline[k]=hypot(p->a[k],p->b[k])*f->outline_width*(double)factor;
        p->inclusive[k]=b.x[1]>a.x[1] || (b.x[1]==a.x[1] && b.x[0]<a.x[0]);
    }
    p->area=fabs(area);
    p->depth_min=fmin(p->v[0].x[2],fmin(p->v[1].x[2],p->v[2].x[2]));
    p->depth_max=fmax(p->v[0].x[2],fmax(p->v[1].x[2],p->v[2].x[2]));
    if (p->area>=1e-20) {
        for (size_t k=0;k<3;++k) {
            const double *coefficient=k==0 ? p->a : k==1 ? p->b : p->c;
            p->depth_plane[k]=(coefficient[1]*p->v[0].x[2]+coefficient[2]*p->v[1].x[2]+coefficient[0]*p->v[2].x[2])/p->area;
        }
    }
    double left=p->v[0].x[0],right=left,bottom=p->v[0].x[1],top=bottom;
    for (size_t k=1;k<3;++k) {left=fmin(left,p->v[k].x[0]);right=fmax(right,p->v[k].x[0]);bottom=fmin(bottom,p->v[k].x[1]);top=fmax(top,p->v[k].x[1]);}
    double width=(double)(camera->width*factor),height=(double)(camera->height*factor);
    p->left=(size_t)fmin(width,fmax(0,floor(left)));p->right=(size_t)fmin(width,fmax(0,ceil(right)));
    p->bottom=(size_t)fmin(height,fmax(0,floor(bottom)));p->top=(size_t)fmin(height,fmax(0,ceil(top)));
}
/** @brief Shade the nearest opaque mesh hit with interpolated unit normals.
 * @param[in] c Validated style and antialiasing settings.
 * @param[in] p Nondegenerate projected mesh triangle covering x/y.
 * @param[in] x Sample center in supersampled pixels.
 * @param[in] y Upward sample center in supersampled pixels.
 * @param[in] ink Whether the independently computed contour covers this sample.
 * @param[out] out Four caller-owned straight-alpha channels; alpha is 255.
 */
static void surface_color(const Config *c, const ProjectedFace *p, double x, double y, bool ink, uint8_t out[4])
{
    double w[3];
    for (size_t k=0;k<3;++k) {size_t edge=(k+1)%3;w[k]=(p->a[edge]*x+p->b[edge]*y+p->c[edge])/p->area;}
    Vec3 n={{0,0,0}};
    for (size_t k=0;k<3;++k) n=vadd(n,vmul(p->normals[k],w[k]));
    double norm=length(n);
    if (norm<1e-12) n=(Vec3){{0,0,1}};else n=vmul(n,1/norm);
    if (n.x[2]<0) n=vmul(n,-1);
    double diffuse=fmax(0,dot(n,unit((Vec3){{-.4,.6,1}}))),shade=.25+.75*diffuse,white=0;
    if (c->style==STYLE_ILLUSTRATED) {
        shade=.82;
        if (diffuse>.5) {shade=.90;white=.10;}
        if (diffuse>.92) {shade=.45;white=.55;}
    }
    for (size_t d=0;d<3;++d) out[d]=ink ? 0 : channel(p->face->color[d]*shade+255*white);
    out[3]=255;
}
/** @brief Test top-left-rule triangle coverage and interpolate geometric depth.
 * @param[in] p Initialized projected triangle, borrowed throughout.
 * @param[in] x Sample center in supersampled pixels.
 * @param[in] y Upward sample center in supersampled pixels.
 * @param[out] z Projected depth in supersample pixels on a covered sample.
 * @param[out] color Borrowed scene color or polygon outline color on coverage;
 * valid until the scene is released. Mesh color is shaded separately.
 * @return true on nontransparent coverage; false otherwise. Outputs are not
 * specified on a miss and must only be consumed after a true result.
 */
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
    *color=outline && !p->face->smooth ? p->face->outline_color : p->face->color;
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
    size_t tile_pixels=c->orbital_count ? 8 : 32;
    size_t factor=c->aa_scale,width=c->width*factor,height=c->height*factor,tile_size=tile_pixels*factor;
    size_t columns=(c->width+tile_pixels-1)/tile_pixels,rows=(c->height+tile_pixels-1)/tile_pixels,tiles=columns*rows;
    if (c->labels && s->triangle_count && s->atom_count>VR_MAX_CANDIDATES/VR_LABEL_SAMPLES/s->triangle_count)
        return fail(e,6,"polygon label visibility exceeds work budget");
    bool mesh=c->orbital_count!=0;
    bool outlined=mesh && c->style!=STYLE_SHADED && c->outline_width>0;
    size_t bytes=s->triangle_count*sizeof(ProjectedFace)+(tiles+1)*sizeof(size_t)+tiles*sizeof(size_t)+
                 (mesh ? width*height*sizeof(uint32_t) : 0)+(outlined ? width*height*sizeof(float) : 0);
    if (retained>VR_MEMORY_BUDGET || bytes>VR_MEMORY_BUDGET-retained) return fail(e,6,"projected polygons exceed 1 GiB allocation budget");
    ProjectedFace *projected=calloc(s->triangle_count,sizeof *projected);
    size_t *offsets=calloc(tiles+1,sizeof *offsets),*cursor=malloc(tiles*sizeof *cursor);
    uint32_t *indices=NULL;bool ok=false;size_t references=0,work=0;
    uint32_t *surface_ids=NULL;float *contours=NULL;
    if (!projected || !offsets || !cursor) {fail(e,6,"cannot allocate polygon raster bins");goto done;}
    for (size_t i=0;i<s->triangle_count;++i) {
        if (!check_cancel(e)) goto done;
        ProjectedFace *p=&projected[i];project(&s->faces[i],camera,factor,p);
        if (p->area<1e-20 || p->left>=p->right || p->bottom>=p->top || (!p->face->color[3] && (!p->face->outline_width || !p->face->outline_color[3]))) continue;
        size_t x0=p->left/tile_size,x1=(p->right-1)/tile_size,y0=p->bottom/tile_size,y1=(p->top-1)/tile_size;
        size_t n=(x1-x0+1)*(y1-y0+1),cost=n*tile_size*tile_size*(mesh ? 2u : 1u);
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
    if (mesh) {
        surface_ids=malloc(width*height*sizeof *surface_ids);
        if (outlined) contours=malloc(width*height*sizeof *contours);
        if (!surface_ids || (outlined && !contours)) {fail(e,6,"cannot allocate mesh coverage and contours");goto done;}
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1) num_threads(workers) if(workers>1)
#endif
        for (size_t tile=0;tile<tiles;++tile) {
            size_t x0=tile%columns*tile_size,y0=tile/columns*tile_size;
            size_t x1=x0+tile_size<width ? x0+tile_size : width,y1=y0+tile_size<height ? y0+tile_size : height;
            for (size_t y=y0;y<y1;++y) {
                if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) break;
                for (size_t x=x0;x<x1;++x) {
                    uint32_t nearest=UINT32_MAX;double front=-DBL_MAX;
                    for (size_t k=offsets[tile];k<offsets[tile+1];++k) {
                        uint32_t id=indices[k];const ProjectedFace *p=&projected[id];
                        if (!p->face->smooth || x<p->left || x>=p->right || y<p->bottom || y>=p->top) continue;
                        double z;const uint8_t *color;
                        if (hit(p,(double)x+.5,(double)y+.5,&z,&color) && z>front) {front=z;nearest=id;}
                    }
                    surface_ids[(height-1-y)*width+x]=nearest;
                }
            }
        }
        if (!check_cancel(e)) goto done;
        if (outlined && !mesh_contours(projected,width,height,factor,c->outline_width,surface_ids,contours,e)) goto done;
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
                const ProjectedFace *surface=NULL;double surface_z=-DBL_MAX;uint8_t surface_rgba[4];
                if (mesh && surface_ids[index]!=UINT32_MAX) {
                    surface=&projected[surface_ids[index]];const uint8_t *color;
                    hit(surface,(double)x+.5,(double)y+.5,&surface_z,&color);
                }
                /* Atom samples are uncomposited in a scene with faces, allowing
                 * transparent faces on either side of the nearest atom surface. */
                if (depth[index]!=-DBL_MAX) hits[count++]=(FaceHit){depth[index],SIZE_MAX,rgba+index*4};
                for (size_t k=offsets[tile];k<offsets[tile+1];++k) {
                    const ProjectedFace *p=&projected[indices[k]];
                    if (p->face->smooth || x<p->left || x>=p->right || y<p->bottom || y>=p->top) continue;
                    double z;const uint8_t *color;
                    if (!hit(p,(double)x+0.5,(double)y+0.5,&z,&color)) continue;
                    bool duplicate=false;
                    for (size_t h=0;h<count;++h) if (hits[h].instance==p->face->instance) {duplicate=true;break;}
                    if (duplicate) continue;
                    if (face_hits++==FACE_LAYERS) {atomic_store_explicit(&overflow,1,memory_order_relaxed);break;}
                    hits[count++]=(FaceHit){z,p->face->instance,color};
                }
                if (surface) {
                    bool ink=outlined && contours[index]<=c->outline_width*(double)factor;
                    surface_color(c,surface,(double)x+.5,(double)y+.5,ink,surface_rgba);
                    /* An opaque mesh replaces all deeper layers. */
                    size_t kept=0;
                    for (size_t h=0;h<count;++h) if (hits[h].z>=surface_z) hits[kept++]=hits[h];
                    count=kept;
                    if (count<FACE_LAYERS+1) hits[count++]=(FaceHit){surface_z,SIZE_MAX-1,surface_rgba};
                    else atomic_store_explicit(&overflow,1,memory_order_relaxed);
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
    free(projected);free(offsets);free(cursor);free(indices);free(surface_ids);free(contours);return ok;
}
