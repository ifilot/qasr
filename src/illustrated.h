/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_ILLUSTRATED_H
#define VR_ILLUSTRATED_H
#include "config.h"

typedef struct {
    double x, y, radius;
    double diffuse_threshold, shade, white;
} IllustratedBand;

static inline double band_lerp(double a, double b, double t)
{
    if (t==1) return b;
    return a+(b-a)*t;
}

/* Sphere circles use fractions of the projected radius. Orbital meshes use
 * diffuse thresholds. Both use the same palette, anchored at the original
 * shadow, midtone and highlight. */
static inline size_t illustrated_bands(size_t count, IllustratedBand bands[VR_MAX_SHADE_BANDS])
{
    static const IllustratedBand shadow={0,0,1,0,.82,0};
    static const IllustratedBand midtone={.10,.10,.88,.50,.90,.10};
    static const IllustratedBand highlight={.16,.18,.47,.92,.45,.55};
    if (!count) count=VR_DEFAULT_SHADE_BANDS;
    bands[0]=count==1 ? midtone : shadow;
    for (size_t i=1;i<count;++i) {
        double position=count==2 ? .5 : (double)i/(double)(count-1);
        const IllustratedBand *from=position<=.5 ? &shadow : &midtone;
        const IllustratedBand *to=position<=.5 ? &midtone : &highlight;
        double t=position<=.5 ? 2*position : 2*position-1;
        bands[i]=(IllustratedBand){
            band_lerp(from->x,to->x,t),
            band_lerp(from->y,to->y,t),
            band_lerp(from->radius,to->radius,t),
            band_lerp(from->diffuse_threshold,to->diffuse_threshold,t),
            band_lerp(from->shade,to->shade,t),
            band_lerp(from->white,to->white,t)
        };
    }
    return count;
}
#endif
