/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_AUTOMATION_H
#define VR_AUTOMATION_H
#include "scene.h"
#include "cJSON.h"
bool json_write(const char *path, const cJSON *json, bool exclusive, Error *e);
bool config_initialize_file(const Config *c, Error *e);
bool report_preflight(const Config *c, Error *e);
void print_json_error(const Error *e);
cJSON *render_report(const Config *c, const Structure *s, const Scene *scene,
                     const Camera *camera, const PngOptions *png, const char *version,
                     const char input_hash[65], const double stages[5], Error *e);
#endif
