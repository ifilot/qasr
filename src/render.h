/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_RENDER_H
#define VR_RENDER_H
#include "scene.h"
typedef struct { size_t width, height; uint8_t *pixels; size_t threads_used; } Image;
/** @brief Rasterize opaque sphere/capped-cylinder surfaces with per-sample depth.
 * @param[in] c Validated rendering, sampling and threading configuration.
 * @param[in] s Complete immutable scene.
 * @param[in] camera Fitted camera.
 * @param[in] source_bytes Memory retained by the parsed input.
 * @param[out] image Zero-initialized output; owns straight-alpha RGBA pixels.
 * @param[out] e Error on failure; image remains empty.
 * @return true on success. Uses configured sampling and a 1 GiB total budget.
 */
bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *image, Error *e);
#ifdef VR_HAVE_CAIRO
/** Experimental painter renderer; shares scene/camera and returns straight RGBA. */
bool render_cairo(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *image, Error *e);
#endif
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
/** Encode using selected options; retained_bytes accounts for live scene/input. */
bool png_write_options_atomic(const char *path, const Image *image, const PngOptions *options,
                              size_t retained_bytes, Error *e);
/** Resolve automatic PNG settings for the destination filesystem. */
PngOptions png_resolve_options(const char *path, PngOptions options);
/** Case insensitive .jpg/.jpeg and .webp suffixes select optional codecs;
 * other paths retain legacy PNG behavior. */
const char *image_format_name(const char *path);
bool image_output_validate(const char *path, Error *e);
/** Atomic selected-format output; PNG options affect PNG only.
 * JPEG quality 90, 4:4:4, opaque pixels required; WebP preserves RGBA losslessly. */
bool image_write_atomic(const char *path, const Image *image, const PngOptions *png,
                        size_t retained_bytes, Error *e);
#endif
