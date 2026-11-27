/* SPDX-License-Identifier: GPL-3.0-only */
#include "surface_render.h"
#include <math.h>
#include <stdlib.h>
#ifdef _OPENMP
#include <omp.h>
#endif

/** @brief Select the smaller finite distance without a library minimum call.
 * @param[in] a Finite propagated distance.
 * @param[in] b Finite candidate distance.
 * @return The smaller value. Seeds and bounded propagation cannot produce NaN.
 */
static inline float nearer(float a, float b) {return a<b ? a : b;}

/** @brief Propagate finite contour distances with two in-place chamfer sweeps.
 * @param[in] width Positive supersampled row width.
 * @param[in] height Positive row count.
 * @param[in,out] distance Caller-owned width*height finite float distances in
 * top-to-bottom order; seed values and far sentinels are supplied by mesh_contours.
 */
static void contour_distance(size_t width, size_t height, float *distance)
{
    const float diagonal=1.41421356237f;
    for (size_t y=0;y<height;++y) {
        for (size_t x=0;x<width;++x) {
            size_t i=y*width+x;float d=distance[i];
            if (x) d=nearer(d,distance[i-1]+1);
            if (y) {
                d=nearer(d,distance[i-width]+1);
                if (x) d=nearer(d,distance[i-width-1]+diagonal);
                if (x+1<width) d=nearer(d,distance[i-width+1]+diagonal);
            }
            distance[i]=d;
        }
    }
    for (size_t y=height;y-->0;) {
        for (size_t x=width;x-->0;) {
            size_t i=y*width+x;float d=distance[i];
            if (x+1<width) d=nearer(d,distance[i+1]+1);
            if (y+1<height) {
                d=nearer(d,distance[i+width]+1);
                if (x) d=nearer(d,distance[i+width-1]+diagonal);
                if (x+1<width) d=nearer(d,distance[i+width+1]+diagonal);
            }
            distance[i]=d;
        }
    }
}

/* A chamfer distance transform gives continuous silhouettes in output pixels.
 * It uses coverage, rather than derivatives of noisy imported normals. Tiny
 * isolated holes in a tessellation do not seed dark rings. Phase boundaries
 * seed contours too. Comparing neighboring geometric depth planes also finds
 * self-occlusion within a phase, without responding to lighting-normal noise. */
static void contour_stripe(const ProjectedFace *projected, size_t width, size_t height,
                           size_t factor, double guard, const uint32_t *surface_ids,
                           size_t left, size_t right, size_t first, size_t last, float *distance)
{
    const float far=1e20f;
    const size_t stride=right-left;
    for (size_t y=first;y<last;++y) {
        for (size_t x=left;x<right;++x) {
            size_t i=y*width+x;uint32_t id=surface_ids[i];size_t neighbors[4];size_t count=0,empty=0;
            if (x) neighbors[count++]=i-1;
            if (x+1<width) neighbors[count++]=i+1;
            if (y) neighbors[count++]=i-width;
            if (y+1<height) neighbors[count++]=i+width;
            bool boundary=false;
            for (size_t k=0;k<count;++k) {
                uint32_t other=surface_ids[neighbors[k]];
                if (other==UINT32_MAX) ++empty;
                else if (id!=UINT32_MAX && projected[id].face->face!=projected[other].face->face) boundary=true;
            }
            distance[(y-first)*stride+x-left]=id==UINT32_MAX ? (empty>=3 || count<4 ? -.5f : far) : boundary ? .5f : far;
        }
    }
    contour_distance(stride,last-first,distance);
    /* Existing silhouettes already describe the grazing outer facets. Avoid
     * extending these strokes with curvature-induced depth gaps; only seed
     * additional occlusion curves beyond that stroke's immediate neighborhood. */
    for (size_t y=first;y<last;++y) {
        for (size_t x=left;x<right;++x) {
            size_t i=y*width+x, local=(y-first)*stride+x-left;
            if (surface_ids[i]==UINT32_MAX || distance[local]<=guard) continue;
            const ProjectedFace *a=&projected[surface_ids[i]];
            size_t neighbors[4],count=0;
            if (x) neighbors[count++]=i-1;
            if (x+1<width) neighbors[count++]=i+1;
            if (y) neighbors[count++]=i-width;
            if (y+1<height) neighbors[count++]=i+width;
            for (size_t k=0;k<count;++k) {
                size_t j=neighbors[k];uint32_t other=surface_ids[j];
                if (other==UINT32_MAX || other==surface_ids[i]) continue;
                const ProjectedFace *b=&projected[other];
                if (a->face->face!=b->face->face) continue;
                double separation=fmax(a->depth_min,b->depth_min)-fmin(a->depth_max,b->depth_max);
                if (separation<=2*(double)factor) continue;
                double ax=(double)x+.5,ay=(double)(height-1-y)+.5;
                double bx=(double)(j%width)+.5,by=(double)(height-1-j/width)+.5;
                double dx=a->depth_plane[0]-b->depth_plane[0],dy=a->depth_plane[1]-b->depth_plane[1];
                double dz=a->depth_plane[2]-b->depth_plane[2];
                /* Same-position plane gaps reject steep continuous slopes. */
                if (fmin(fabs(dx*ax+dy*ay+dz),fabs(dx*bx+dy*by+dz))>2*(double)factor) {
                    distance[local]=.5f;break;
                }
            }
        }
    }
    contour_distance(stride,last-first,distance);
}

bool mesh_contours(const ProjectedFace *projected, size_t width, size_t height,
                   size_t factor, double outline_width, const uint32_t *surface_ids,
                   const size_t bounds[4], int workers, size_t available, uint8_t *ink, Error *e)
{
    const size_t rows=128;
    double stroke=outline_width*(double)factor,guard=fmax(2,2*outline_width)*(double)factor;
    /* Only distances through guard or stroke matter. Every chamfer step costs
     * at least one pixel. Include both propagation radii and the seed offset
     * so each stripe produces the same threshold decisions as the full image. */
    size_t halo=(size_t)ceil(guard)+(size_t)ceil(stroke)+2;
    size_t left=bounds[0]>halo ? bounds[0]-halo : 0;
    size_t right=bounds[1]+halo<width ? bounds[1]+halo : width;
    size_t stripes=(bounds[3]-bounds[2]+rows-1)/rows;
    size_t scratch_rows=rows+2*halo<height ? rows+2*halo : height;
    size_t stride=right-left, scratch=stride*scratch_rows;
    if ((size_t)workers>stripes) workers=(int)stripes;
    if (scratch>available/sizeof(float))
        return fail(e,6,"orbital contour scratch exceeds 1 GiB allocation budget");
    if ((size_t)workers>available/sizeof(float)/scratch) workers=(int)(available/sizeof(float)/scratch);
    float *distance=malloc((size_t)workers*scratch*sizeof *distance);
    if (!distance) return fail(e,6,"cannot allocate orbital contour scratch");
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic,1) num_threads(workers) if(workers>1)
#endif
    for (size_t stripe=0;stripe<stripes;++stripe) {
        if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) continue;
        size_t first=bounds[2]+stripe*rows,last=first+rows;
        if (last>bounds[3]) last=bounds[3];
        size_t start=first>halo ? first-halo : 0;
        size_t end=last+halo<height ? last+halo : height;
        float *local=distance;
#ifdef _OPENMP
        local+=(size_t)omp_get_thread_num()*scratch;
#endif
        contour_stripe(projected,width,height,factor,guard,surface_ids,left,right,start,end,local);
        for (size_t y=first;y<last;++y) for (size_t x=bounds[0];x<bounds[1];++x)
            ink[y*width+x]=(uint8_t)(local[(y-start)*stride+x-left]<=stroke);
    }
    free(distance);
    return check_cancel(e);
}
