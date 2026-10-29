/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include "overlay.h"
#include <stdio.h>

int main(void)
{
    uint8_t pixels[4]={0,0,0,255};
    Image image={.width=1,.height=1,.pixels=pixels};Error e={0};
    for (int codec=ENCODER_LIBPNG;codec<=ENCODER_CAIRO;++codec) {
#ifndef VR_HAVE_LIBDEFLATE
        if (codec==ENCODER_LIBDEFLATE) continue;
#endif
#ifndef VR_HAVE_CAIRO
        if (codec==ENCODER_CAIRO) continue;
#endif
        PngOptions options={(PngEncoder)codec,FILTER_ADAPTIVE,1};
        e=(Error){0};
        if (png_write_options_atomic("/nonexistent-qasr-budget/output.png",&image,&options,VR_MEMORY_BUDGET,&e) || e.code!=6)
            return 1;
    }
#ifdef VR_HAVE_LIBDEFLATE
    /* The image and scene fit separately, but raw/filter/compressed buffers
     * must also fit together, before the encoder can read any pixel data. */
    image.width=16384;image.height=4096;
    PngOptions fast={ENCODER_LIBDEFLATE,FILTER_UP,1};e=(Error){0};
    if (png_write_options_atomic("/nonexistent-qasr-budget/output.png",&image,&fast,300u*1024u*1024u,&e) || e.code!=6)
        return 1;
#endif
    Config config={0};config.width=config.height=512;config.labels=2;
    Scene scene={0};scene.count=scene.atom_count=20001;
    Camera camera={0};e=(Error){0};
    if (overlay_layout(&config,&scene,&camera,&e) || e.code!=6) return 1;
    puts("PNG allocation and annotation work limits passed");return 0;
}
