/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SURFACE_RENDER_H
#define VR_SURFACE_RENDER_H
#include "scene.h"

/* Internal raster data shared by polygon composition and mesh contours.
 * x/y and z are in supersample pixels; z increases toward the camera.
 * face borrows a scene triangle until rasterization completes. */
typedef struct {
    Vec3 v[3];
    Vec3 normals[3];
    double a[3],b[3],c[3],outline[3],area;
    double depth_plane[3]; /* Projected geometric depth = ax + by + c. */
    double depth_min,depth_max;
    bool inclusive[3];
    uint8_t boundary;
    size_t left,right,bottom,top;
    const FaceTriangle *face;
} ProjectedFace;

/* Compute the contour ink mask within bounds (left, right, top, bottom;
 * half-open image coordinates). Only covered samples can consume the mask. */
bool mesh_contours(const ProjectedFace *projected, size_t width, size_t height,
                   size_t factor, double outline_width, const uint32_t *surface_ids,
                   const size_t bounds[4], int workers, size_t available, uint8_t *ink, Error *e);
#endif
