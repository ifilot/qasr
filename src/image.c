/* SPDX-License-Identifier: GPL-3.0-only */
#include "image.h"
#include <stdlib.h>
#include <string.h>

void image_free(Image *image) { free(image->pixels); memset(image,0,sizeof *image); }
