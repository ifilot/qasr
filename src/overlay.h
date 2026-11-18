/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_OVERLAY_H
#define VR_OVERLAY_H
#include "image.h"
#include "scene.h"
#define VR_LABEL_SAMPLES 9u
/* Output-pixel position (y downward), camera depth and fade 0..1. */
typedef struct { double x,y,depth,opacity; bool visible; } LabelAnchor;
/** Additional temporary label-order/collision storage for the aggregate budget. */
size_t overlay_label_scratch_bytes(const Scene *s);
/** Reserve legend/footer space; viewport coordinates use a lower-left origin. */
bool overlay_layout(const Config *c, const Scene *s, Camera *camera, Error *e);
/** Choose a 1/2/5 scale-bar length after the camera's pixels/angstrom fit. */
bool overlay_scale_bar(const Config *c, Camera *camera, Error *e);
/** Place the symmetry label in image pixels (y downward) as left, top, right,
 * bottom. anchored_only skips labels that do not follow world geometry
 * (identity and inversion). Returns false when no label is drawn. */
bool overlay_symmetry_label_box(const Config *c, const Scene *s, const Camera *camera, bool anchored_only, double box[4]);
/** Draw deterministic annotations; anchors contains one entry per primitive. */
bool overlay_render(const Config *c, const Scene *s, const Camera *camera,
                    const LabelAnchor *anchors, Image *image, Error *e);
#endif
