/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_IMAGE_IO_H
#define VR_IMAGE_IO_H
#include "config.h"
#include "image.h"

/** @brief Encode PNG and replace the destination after successful encoding/close.
 * @param[in] path Non-null destination; temporary file is created beside it.
 * @param[in] image Non-null complete RGBA image, borrowed until return.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true after rename; false on failure, removing temporary files and
 * preserving the earlier destination. Does not guarantee filesystem durability.
 */
bool png_write_atomic(const char *path, const Image *image, Error *e);
/** @brief Encode PNG with selected options under the aggregate allocation budget.
 * @param[in] path Non-null destination; temporary file is created beside it.
 * @param[in] image Non-null complete RGBA image, borrowed until return.
 * @param[in] options Non-null encoder, filter and compression settings.
 * @param[in] retained_bytes Live allocations other than this image and encoder.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true after rename; false on invalid/unsupported options, budget or I/O
 * failure, removing temporary files and preserving the earlier destination.
 */
bool png_write_options_atomic(const char *path, const Image *image, const PngOptions *options,
                              size_t retained_bytes, Error *e);
/** @brief Resolve automatic PNG settings for a destination filesystem.
 * @param[in] path Non-null destination path, borrowed until return.
 * @param[in] options Requested settings, with automatic fields permitted.
 * @return Resolved encoder/filter/compression settings; does not create output.
 */
PngOptions png_resolve_options(const char *path, PngOptions options);
/** @brief Select image format using a case-insensitive destination suffix.
 * @param[in] path Non-null destination path, borrowed until return.
 * @return Static name: "jpeg" for .jpg/.jpeg, "webp" for .webp, otherwise "png".
 * This is not validation: .mp4 is rejected separately for still images.
 */
const char *image_format_name(const char *path);
/** @brief Check still-image suffix and optional codec availability.
 * @param[in] path Non-null destination path, borrowed until return.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true if supported; false for .mp4 or unavailable JPEG/WebP support.
 */
bool image_output_validate(const char *path, Error *e);
/** @brief Atomically encode the selected still-image format.
 * @param[in] path Non-null destination, checked with image_output_validate by the
 * caller; temporary file is created beside it.
 * @param[in] image Non-null complete RGBA image, borrowed until return; JPEG
 * requires fully opaque pixels and uses quality 90, 4:4:4. WebP is lossless RGBA.
 * @param[in] png Non-null PNG options, used only for PNG output.
 * @param[in] retained_bytes Live allocations other than this image and encoder.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true after rename; false on validation, budget, encoding or I/O failure,
 * preserving the earlier destination. Does not guarantee filesystem durability.
 */
bool image_write_atomic(const char *path, const Image *image, const PngOptions *png,
                        size_t retained_bytes, Error *e);
#endif
