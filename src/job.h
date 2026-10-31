/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_JOB_H
#define VR_JOB_H
#include "scene.h"
struct cJSON;
/** Single-entry source/scene cache with exclusive caller ownership.
 * Zero-initialize and set limit before use. The cache and global cancellation
 * state support sequential jobs; do not share a cache across concurrent calls. */
typedef struct {
    Structure source;
    Scene scene;
    char source_key[65], scene_key[65];
    size_t limit, source_hits, scene_hits, misses;
} JobCache;
/** @brief Evict retained source/scene data and keys, preserving limit and counters.
 * @param[in,out] cache Non-null initialized cache; safe repeatedly.
 */
void job_cache_clear(JobCache *cache);
/** @brief Snapshot cache limits, retained bytes and cumulative hit/miss counters.
 * @param[in] cache Non-null initialized cache, borrowed until return.
 * @return Caller-owned JSON to release with cJSON_Delete; NULL on allocation failure.
 */
struct cJSON *job_cache_info(const JobCache *cache);
/** @brief Render a normal CLI or worker job and return its result metadata.
 * @param[in] c Non-null validated configuration with input and output paths.
 * @param[in,out] cache Initialized exclusive cache, or NULL for no cache. Movie
 * jobs evict retained entries. Counters and retained data may change on failure.
 * @param[in] report Request a complete still-image report; movie jobs always
 * return a movie report. The caller writes any requested report file.
 * @param[in] version Non-null application version for provenance.
 * @param[in] config_ms Nonnegative configuration duration in milliseconds.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return Caller-owned JSON to release with cJSON_Delete, or NULL on failure.
 * Still jobs without reports return an empty object, plus cache info if supplied.
 * Output can already be committed when subsequent hashing/report creation fails.
 */
struct cJSON *job_render(const Config *c, JobCache *cache, bool report, const char *version,
                        double config_ms, Error *e);
/** @brief Process a sequential JSONL worker session using invocation arguments.
 * @param[in] argc Argument count.
 * @param[in] argv Non-null arguments, borrowed until return.
 * @param[in] version Non-null application version for provenance.
 * @return Process exit category; zero after a successful session.
 */
int worker_run(int argc, char **argv, const char *version);
#endif
