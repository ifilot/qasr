/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SHA256_H
#define VR_SHA256_H
#include "common.h"

/** @brief Hash a file for provenance, subject to input-byte and cancellation limits.
 * @param[in] path Non-null input path, borrowed until return.
 * @param[out] hex Non-null 65-byte destination; written only on success.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on open/read/close, limit or cancellation failure.
 */
bool sha256_file(const char *path, char hex[65], Error *e);
/** @brief Hash bytes into a lowercase, NUL-terminated SHA-256 digest.
 * @param[in] data Borrowed readable storage of bytes length; may be NULL if empty.
 * @param[in] bytes Number of input bytes.
 * @param[out] hex Non-null 65-byte destination, disjoint from the input.
 */
void sha256_data(const void *data, size_t bytes, char hex[65]);
#endif
