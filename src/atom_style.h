/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_ATOM_STYLE_H
#define VR_ATOM_STYLE_H
#include "config.h"
typedef struct { size_t color, radius; } RuleStamp; /* SIZE_MAX means element/default style. */
typedef struct { double radius; uint8_t color[4]; RuleStamp rules; } AtomStyle;
/** Validate all selectors; resolve source rules once, with bounded work/state.
 * Caller owns *styles, which is NULL if there are no atom rules. */
bool atom_styles_resolve(const Config *c, const Structure *s, AtomStyle **styles, Error *e);
/** Apply explicitly cell-scoped rules after periodic RGB transformation. */
void atom_style_instance(const Config *c, size_t source, unsigned element, const int origin[3], AtomStyle *style);
#endif
