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

/** @brief Compute silhouette, phase and same-phase occlusion stroke distances.
 * @param[in] projected Projected scene triangles; indices in surface_ids must be
 * valid and refer to nondegenerate smooth triangles with finite depth planes.
 * @param[in] width Positive supersampled image width.
 * @param[in] height Positive supersampled image height.
 * @param[in] factor Positive samples per output pixel along each axis.
 * @param[in] outline_width Positive requested outline width in output pixels.
 * @param[in] surface_ids Nearest mesh triangle IDs, width*height entries in
 * top-to-bottom row order; UINT32_MAX denotes uncovered samples. Borrowed.
 * @param[out] distance Non-null caller-owned width*height floats, disjoint from
 * surface_ids; distances are in supersample pixels. Partial on cancellation.
 * @param[out] e Non-null error destination, set on cancellation.
 * @return true on success; false on cancellation. Allocates no storage and uses
 * only immutable scene data; independent caller buffers permit concurrent calls.
 */
bool mesh_contours(const ProjectedFace *projected, size_t width, size_t height,
                   size_t factor, double outline_width, const uint32_t *surface_ids,
                   float *distance, Error *e);
#endif
