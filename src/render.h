/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_RENDER_H
#define VR_RENDER_H
#include "scene.h"
#include "image.h"
/** Optional sampling buffers with exclusive caller ownership. Zero-initialize,
 * use sequentially, and release with render_workspace_free even after failure.
 * Image pixels remain separately owned. samples is the allocated sample count,
 * permitting reuse across dimensions with the same width*height*aa_scale^2. */
typedef struct {double *depth;uint8_t *rgba;size_t samples;} RenderWorkspace;
/** @brief Release sampling buffers and reset fields; safe repeatedly.
 * @param[in,out] workspace Initialized workspace, or NULL for a no-op.
 */
void render_workspace_free(RenderWorkspace *workspace);
/** @brief Render an image, optionally retaining sampling buffers between calls.
 * @param[in] c Non-null validated rendering and threading configuration.
 * @param[in] s Non-null complete immutable scene.
 * @param[in] camera Non-null fitted camera.
 * @param[in] source_bytes Retained input/configuration and other external bytes;
 * excludes scene allocations and sampling buffers, which are charged internally.
 * @param[in,out] workspace Exclusively owned initialized workspace, or NULL for
 * per-call buffers. May retain allocations after failure; free when finished.
 * @param[out] image Non-null zero image; owns independent RGBA pixels on success,
 * remains empty on failure. Release with image_free before another render.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on invalid backend, budget, allocation or render
 * failure. Reuse does not bypass aggregate budget checks; Cairo releases buffers.
 */
bool render_image_workspace(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes,
                            RenderWorkspace *workspace, Image *image, Error *e);
/** @brief Rasterize atom/bond surfaces and polygon layers with per-sample depth.
 * @param[in] c Validated rendering, sampling and threading configuration.
 * @param[in] s Complete immutable scene.
 * @param[in] camera Fitted camera.
 * @param[in] source_bytes Retained input/configuration and external allocation bytes.
 * @param[out] image Zero-initialized output; owns straight-alpha RGBA pixels.
 * @param[out] e Error on failure; image remains empty.
 * @return true on success. Uses configured sampling and a 1 GiB total budget.
 */
bool render_image(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *image, Error *e);
/** @brief Composite polygon layers into the existing surface samples.
 * @param[in] c Non-null validated render configuration.
 * @param[in] s Non-null complete scene.
 * @param[in] camera Non-null fitted camera.
 * @param[in] workers Positive worker count; each worker owns its layer scratch.
 * @param[in] retained Bytes already charged to the aggregate allocation budget.
 * @param[in] depth Surface depths, borrowed for width*height*aa_scale^2 samples.
 * @param[in,out] rgba RGBA samples of the same count, composited in place.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on budget, allocation or cancellation failure.
 */
bool faces_render_samples(const Config *c, const Scene *s, const Camera *camera, int workers,
                          size_t retained, double *depth, uint8_t *rgba, Error *e);
/** @brief Test whether a polygon hides a label sample in camera space.
 * @param[in] s Non-null complete scene.
 * @param[in] camera Non-null fitted camera.
 * @param[in] factor Supersampling scale used for x/y coordinates.
 * @param[in] x Sample x in supersampled pixels.
 * @param[in] y Sample y in supersampled pixels.
 * @param[in] z Sample camera depth in angstroms.
 * @return true when an opaque polygon layer is in front of the sample.
 */
bool faces_occlude_label(const Scene *s, const Camera *camera, size_t factor, double x, double y, double z);
#ifdef VR_HAVE_CAIRO
/** @brief Render with the experimental Cairo painter and return straight RGBA.
 * @param[in] c Non-null validated configuration supported by this backend.
 * @param[in] s Non-null complete scene.
 * @param[in] camera Non-null fitted camera.
 * @param[in] source_bytes Retained input/configuration allocation bytes.
 * @param[out] image Non-null zero image; owns pixels on success, empty on failure.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on budget, allocation or Cairo failure.
 */
bool render_cairo(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *image, Error *e);
#endif
#endif
