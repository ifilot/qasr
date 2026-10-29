/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_CONFIG_H
#define VR_CONFIG_H
#include "structure.h"
#include <stdint.h>
struct cJSON;

#define VR_MAX_ATOM_RULES 4096u
#define VR_CONFIG_MEMORY_LIMIT ((size_t)67108864)
typedef struct {
    unsigned element;
    size_t *indices, index_count, first, last, next_cell;
    bool range, has_cell, has_radius, has_color;
    int cell[3];
    double radius;
    uint8_t color[4];
} AtomRule;

typedef enum { STYLE_SHADED, STYLE_ILLUSTRATED } RenderStyle;
typedef enum { BACKEND_ZBUFFER, BACKEND_CAIRO } RenderBackend;
typedef enum { ENCODER_LIBPNG, ENCODER_LIBDEFLATE, ENCODER_CAIRO } PngEncoder;
typedef enum { FILTER_ADAPTIVE, FILTER_NONE, FILTER_SUB, FILTER_UP, FILTER_DEFAULT } PngFilter;
typedef enum { IMAGES_AUTO, IMAGES_NORMAL, IMAGES_MUTED, IMAGES_GREYSCALE } PeriodicImageStyle;
/* Compression -1 and FILTER_DEFAULT request automatic settings. */
typedef struct { PngEncoder encoder; PngFilter filter; int compression; } PngOptions;

typedef struct {
    size_t width, height, repeat[3], threads, aa_scale;
    size_t expand_xy[2]; /* Additional cells on each side of the primary a/b cell. */
    PeriodicImageStyle image_colors;
    Vec3 view, up;
    double margin, sphere_scale, bond_radius, depth_fade, outline_width, cell_width;
    char background[10];
    RenderStyle style;
    RenderBackend backend;
    PngOptions png;
    bool adaptive_aa;
    bool view_top, up_explicit;
    bool cell, axes, legend, scale_bar, recover, inspect, inspect_scene, benchmark;
    int labels; /* 0 none, 1 element, 2 index */
    Format format;
    bool final_frame;
    size_t frame;
    unsigned species[VR_MAX_SPECIES];
    size_t species_count;
    double element_radius[119]; /* Zero means use the versioned display defaults from M2. */
    char element_color[119][10];
    double cutoffs[119][119]; /* Symmetric; zero means no override. */
    const char *input, *output; /* Borrowed argv or owned job_json strings. */
    size_t requested_repeat[3];
    bool no_user_config, init_config, dump_config, validate_config, json_errors, user_loaded;
    const char *report;
    char *user_path;
    struct cJSON *provenance, *sources, *rule_json, *job_json;
    AtomRule *rules;
    size_t rule_count, cell_rule_count, first_cell;
} Config;

/** @brief Initialize built-ins, apply user and invocation JSON, then CLI overrides.
 * @param[in] argc Argument count.
 * @param[in] argv Borrowed arguments, valid for the config lifetime.
 * @param[out] c Non-null destination; release owned metadata with config_free,
 * including after a failed parse.
 * @param[out] e Error diagnostic on failure.
 * @return true on valid input; false on CLI/config failure.
 */
bool config_parse(int argc, char **argv, Config *c, Error *e);
/** Release metadata, request strings and rule indices; safe for a zero config. */
void config_free(Config *c);
/** Valid render settings JSON, with requested (not derived) repeat counts. */
struct cJSON *config_settings_json(const Config *c);
/** Resolved config/runtime/provenance envelope; caller owns returned object. */
struct cJSON *config_dump_json(const Config *c);
/** Approximate owned allocation charge, included in scene/render limits. */
size_t config_memory_bytes(const Config *c);
/** Internal rule decoder, bounded by the config and selector budgets. */
bool config_atom_rules(Config *c, const struct cJSON *rules, Error *e);
#endif
