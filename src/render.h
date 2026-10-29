/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_RENDER_H
#define VR_RENDER_H
#include "scene.h"
typedef struct { size_t width, height; uint8_t *pixels; } Image;
/** @brief Rasterize opaque sphere/capped-cylinder surfaces with per-sample depth.
 * @param[in] c Configuration; M3 supports one thread and no optional overlays.
 * @param[in] s Complete immutable scene.
 * @param[in] camera Fitted camera.
 * @param[in] source_bytes Memory retained by the parsed input.
 * @param[out] image Zero-initialized output; owns straight-alpha RGBA pixels.
 * @param[out] e Error on failure; image remains empty.
 * @return true on success. Uses fixed 2x supersampling and a 1 GiB total budget.
 */
bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *image, Error *e);
/** @brief Release pixels and zero an initialized image.
 * @param[in,out] image Non-null initialized image.
 */
void image_free(Image *image);
/** @brief Atomically replace a destination after successful PNG encoding/close.
 * @param[in] path Destination path; temporary file is created in its directory.
 * @param[in] image Complete RGBA image.
 * @param[out] e Error on failure; temporary files are removed.
 * @return true after rename. Existing destination is untouched on earlier errors.
 */
bool png_write_atomic(const char *path, const Image *image, Error *e);
#endif
