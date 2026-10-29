/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_OVERLAY_H
#define VR_OVERLAY_H
#include "render.h"
#define VR_LABEL_SAMPLES 9u
typedef struct { double x,y,depth; bool visible; } LabelAnchor;
/** Additional temporary label-order/collision storage for the aggregate budget. */
size_t overlay_label_scratch_bytes(const Scene *s);
/** Reserve legend/footer space; viewport coordinates use a lower-left origin. */
bool overlay_layout(const Config *c, const Scene *s, Camera *camera, Error *e);
/** Choose a 1/2/5 scale-bar length after the camera's pixels/angstrom fit. */
bool overlay_scale_bar(const Config *c, Camera *camera, Error *e);
/** Draw deterministic annotations; anchors contains one entry per primitive. */
bool overlay_render(const Config *c, const Scene *s, const Camera *camera,
                    const LabelAnchor *anchors, Image *image, Error *e);
#endif
