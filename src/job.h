/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_JOB_H
#define VR_JOB_H
#include "automation.h"
typedef struct {
    Structure source;
    Scene scene;
    char source_key[65], scene_key[65];
    size_t limit, source_hits, scene_hits, misses;
} JobCache;
void job_cache_clear(JobCache *cache);
cJSON *job_cache_info(const JobCache *cache);
/* Shared normal CLI/worker render path. Caller owns the optional report.
 * On successful no-report CLI calls, returns a small success object. */
cJSON *job_render(const Config *c, JobCache *cache, bool report, const char *version,
                  double config_ms, Error *e);
int worker_run(int argc, char **argv, const char *version);
#endif
