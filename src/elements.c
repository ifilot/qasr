/* SPDX-License-Identifier: GPL-3.0-only */
#include "elements.h"
#include <stdlib.h>
#include <string.h>
#include "element_defaults.inc"

void decode_color(const char *s, uint8_t rgba[4])
{
    rgba[3] = 255;
    for (size_t i = 0; i < (strlen(s) - 1) / 2; ++i) {
        char token[3] = {s[1 + 2*i], s[2 + 2*i], 0};
        rgba[i] = (uint8_t)strtoul(token, NULL, 16);
    }
}
double element_radius(const Config *c, unsigned z)
{
    return (c->element_radius[z] ? c->element_radius[z] : default_radii[z]) * c->sphere_scale;
}
void element_color(const Config *c, unsigned z, uint8_t rgba[4])
{
    decode_color(c->element_color[z][0] ? c->element_color[z] : default_colors[z], rgba);
}
double bond_cutoff(const Config *c, unsigned a, unsigned b)
{
    if (c->cutoffs[a][b]) return c->cutoffs[a][b];
    if ((a == 6 && b == 13) || (a == 13 && b == 6)) return 3.5;
    if (a == 1 || b == 1) return (a > 20 || b > 20) ? 2.0 : 1.2;
    if (a <= 20 && b <= 20) return 2.0;
    return (a <= 20 || b <= 20) ? 2.5 : 3.0;
}
