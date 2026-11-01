/* SPDX-License-Identifier: GPL-3.0-only */
#include "surface_render.h"
#include <math.h>

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
 * @param[out] e Non-null diagnostic destination, set on cancellation.
 * @return true on completion; false with partial distances on cancellation.
 */
static bool contour_distance(size_t width, size_t height, float *distance, Error *e)
{
    const float diagonal=1.41421356237f;
    for (size_t y=0;y<height;++y) {
        if (!check_cancel(e)) return false;
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
        if (!check_cancel(e)) return false;
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
    return true;
}

/* A chamfer distance transform gives continuous silhouettes in output pixels.
 * It uses coverage, rather than derivatives of noisy imported normals. Tiny
 * isolated holes in a tessellation do not seed dark rings. Phase boundaries
 * seed contours too. Comparing neighboring geometric depth planes also finds
 * self-occlusion within a phase, without responding to lighting-normal noise. */
bool mesh_contours(const ProjectedFace *projected, size_t width, size_t height,
                          size_t factor, double outline_width, const uint32_t *surface_ids, float *distance, Error *e)
{
    const float far=1e20f;
    for (size_t y=0;y<height;++y) {
        if (!check_cancel(e)) return false;
        for (size_t x=0;x<width;++x) {
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
            distance[i]=id==UINT32_MAX ? (empty>=3 || count<4 ? -.5f : far) : boundary ? .5f : far;
        }
    }
    if (!contour_distance(width,height,distance,e)) return false;
    /* Existing silhouettes already describe the grazing outer facets. Avoid
     * extending these strokes with curvature-induced depth gaps; only seed
     * additional occlusion curves beyond that stroke's immediate neighborhood. */
    double guard=fmax(2,2*outline_width)*(double)factor;
    for (size_t y=0;y<height;++y) {
        if (!check_cancel(e)) return false;
        for (size_t x=0;x<width;++x) {
            size_t i=y*width+x;
            if (surface_ids[i]==UINT32_MAX || distance[i]<=guard) continue;
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
                    distance[i]=.5f;break;
                }
            }
        }
    }
    return contour_distance(width,height,distance,e);
}
