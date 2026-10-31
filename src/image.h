/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_IMAGE_H
#define VR_IMAGE_H
#include <stddef.h>
#include <stdint.h>

/** Contiguous width*height*4 straight-alpha RGBA bytes in output row order.
 * Renderers return independently owned pixels. Encoders may also borrow an Image
 * wrapping caller storage; only malloc-compatible owned pixels may be freed. */
typedef struct { size_t width, height; uint8_t *pixels; size_t threads_used; } Image;

/** @brief Release owned pixels and reset all fields; safe repeatedly.
 * @param[in,out] image Non-null initialized image owning pixels, or a zero image.
 */
void image_free(Image *image);
#endif
