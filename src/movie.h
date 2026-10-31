/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QASR_MOVIE_H
#define QASR_MOVIE_H
#include "config.h"

/** @brief Stream rendered frames into an MP4 using optional linked FFmpeg libraries.
 * @param[in] c Non-null validated movie config with input and output paths;
 * borrowed until return. Calls must be serialized: cancellation and FFmpeg log
 * level are process-wide state.
 * @param[in] version Non-null application version for provenance.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return Caller-owned report to release with cJSON_Delete; NULL on failure,
 * including when movie support is disabled. The destination is replaced only
 * after flush, trailer and close succeed. Later hashing/report failures can
 * return NULL after replacement; no filesystem durability guarantee is made.
 */
struct cJSON *movie_render(const Config *c, const char *version, Error *e);
#endif
