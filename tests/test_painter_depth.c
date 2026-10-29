/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include <stdio.h>
#include <string.h>

/* Independent gate scene with colored cylinders: ordinary CLI bonds are gray,
 * which would conflate depth mistakes with the painter's shading approximation.
 * At x~-0.23,y~-0.02 the red cylinder's front intersection is strictly closer.
 * Both center depths are zero, so whole-primitive ordering paints blue on top. */
int main(int argc, char **argv)
{
    Config c={0};c.width=c.height=96;c.aa_scale=2;c.style=STYLE_ILLUSTRATED;c.cell_width=2;
    strcpy(c.background,"#ffffff");
    Primitive primitives[2]={
        {.a={{-1,0,0.6}},.b={{1,0,-0.6}},.radius=0.35,.color={255,0,0,255},.kind=CYLINDER},
        {.a={{0,-1,-0.6}},.b={{0,1,0.6}},.radius=0.35,.color={0,0,255,255},.kind=CYLINDER}
    };
    Scene scene={.items=primitives,.count=2,.capacity=2};
    Camera camera={.right={{1,0,0}},.up={{0,1,0}},.view={{0,0,1}},
                   .scale=24,.width=96,.height=96};
    Image z={0},p={0};Error e={0};
    if (!render_image(&c,&scene,&camera,0,&z,&e)) goto error;
    c.backend=BACKEND_CAIRO;c.aa_scale=1;
    if (!render_image(&c,&scene,&camera,0,&p,&e)) goto error;
    size_t k=4*(48*96+42);
    if (!(z.pixels[k]>z.pixels[k+2] && p.pixels[k+2]>p.pixels[k])) {
        fprintf(stderr,"unexpected crossing-cylinder gate result\n");goto error;
    }
    if (argc==2) {
        char path[4096];
        if (snprintf(path,sizeof path,"%s/crossing-zbuffer.png",argv[1])>=(int)sizeof path ||
            !png_write_atomic(path,&z,&e)) goto error;
        if (snprintf(path,sizeof path,"%s/crossing-cairo.png",argv[1])>=(int)sizeof path ||
            !png_write_atomic(path,&p,&e)) goto error;
    }
    printf("{\"cairo_gate_passed\":false,\"reason\":\"crossing cylinders exchange surface depth order\","
           "\"witness_xy\":[42,48],\"zbuffer_rgba\":[%u,%u,%u,%u],\"cairo_rgba\":[%u,%u,%u,%u]}\n",
           (unsigned)z.pixels[k],(unsigned)z.pixels[k+1],(unsigned)z.pixels[k+2],(unsigned)z.pixels[k+3],
           (unsigned)p.pixels[k],(unsigned)p.pixels[k+1],(unsigned)p.pixels[k+2],(unsigned)p.pixels[k+3]);
    image_free(&z);image_free(&p);return 0;
error:
    fprintf(stderr,"%s\n",e.message);image_free(&z);image_free(&p);return 1;
}
