/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SURFACE_H
#define VR_SURFACE_H
#include "structure.h"
#include <stdint.h>

/* Shared world-space value type: configuration owns imported mesh triangles;
 * scenes own independent copies. This header has no configuration/render API. */
typedef struct FaceTriangle {
    Vec3 vertices[3]; /* Cartesian angstroms. */
    Vec3 normals[3]; /* Unit world normals for smooth PLY surfaces. */
    bool smooth; /* Opaque orbital shading; false selects flat polygon layers. */
    uint8_t color[4], outline_color[4], edges;
    double outline_width;
    size_t face, instance; /* Definition/layer ID and unique scene instance ID. */
    int origin[3];
} FaceTriangle;
#endif
