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

typedef enum { STYLE_SHADED, STYLE_ILLUSTRATED, STYLE_ORTEP, STYLE_BALSAC } RenderStyle;
typedef enum { TONE_COLOR, TONE_MONOCOLOR, TONE_DUALTONE } RenderTone;
typedef enum { BACKEND_ZBUFFER, BACKEND_CAIRO } RenderBackend;
typedef enum { ENCODER_LIBPNG, ENCODER_LIBDEFLATE, ENCODER_CAIRO } PngEncoder;
typedef enum { FILTER_ADAPTIVE, FILTER_NONE, FILTER_SUB, FILTER_UP, FILTER_DEFAULT } PngFilter;
typedef enum { IMAGES_AUTO, IMAGES_NORMAL, IMAGES_MUTED, IMAGES_GREYSCALE } PeriodicImageStyle;
typedef enum { BONDS_UNIFORM, BONDS_ATOMS } BondColors;
typedef enum { CELL_SOLID, CELL_DASHED } CellLineStyle;
typedef enum { MOVIE_NONE, MOVIE_ORBIT, MOVIE_TRAJECTORY } MovieMode;
typedef enum { MOVIE_CODEC_AUTO, MOVIE_CODEC_H264, MOVIE_CODEC_MPEG4 } MovieCodec;
typedef enum {
    MOVIE_FIELD_FPS=1u, MOVIE_FIELD_FRAMES=2u, MOVIE_FIELD_AXIS=4u,
    MOVIE_FIELD_ANGLE=8u, MOVIE_FIELD_INTERPOLATE=16u, MOVIE_FIELD_CODEC=32u
} MovieField;
typedef struct {
    MovieMode mode;
    size_t frames, fps, interpolate;
    Vec3 axis;
    double angle;
    unsigned explicit_fields;
    MovieCodec codec;
} MovieOptions;
#define VR_MAX_FACES 4096u
#define VR_MAX_FACE_VERTICES 64u
typedef struct {
    Vec3 *vertices;
    size_t *indices, count;
    int (*offsets)[3];
    bool fractional, repeat;
    uint8_t color[4], outline_color[4];
    double outline_width;
} FaceDefinition;
/* Compression -1 and FILTER_DEFAULT request automatic settings. */
typedef struct { PngEncoder encoder; PngFilter filter; int compression; } PngOptions;

typedef struct {
    size_t width, height, repeat[3], threads, aa_scale;
    size_t expand_xy[2]; /* Additional cells on each side of the primary a/b cell. */
    PeriodicImageStyle image_colors;
    BondColors bond_colors;
    CellLineStyle cell_style;
    Vec3 view, up;
    double margin, sphere_scale, bond_radius, depth_fade, outline_width, cell_width;
    double cell_outline_width, cell_dash_length, cell_dash_gap;
    char cell_color[10];
    char background[10];
    RenderStyle style;
    RenderTone tone;
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
    double element_radius[119]; /* Zero selects the built-in display defaults. */
    char element_color[119][10];
    double cutoffs[119][119]; /* Symmetric; zero means no override. */
    const char *input, *output; /* Borrowed argv or owned job_json strings. */
    size_t requested_repeat[3];
    bool no_user_config, init_config, dump_config, validate_config, json_errors, user_loaded;
    const char *report;
    MovieOptions movie;
    bool frame_explicit;
    char *user_path;
    struct cJSON *provenance, *sources, *rule_json, *job_json;
    AtomRule *rules;
    size_t rule_count, cell_rule_count, first_cell;
    FaceDefinition *faces;
    size_t face_count;
    struct cJSON *faces_json;
} Config;

/** @brief Parse configuration JSON with bounded input and strict key handling.
 * @param[in] text Non-null NUL-terminated JSON, borrowed until return.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return Caller-owned JSON, to release with cJSON_Delete; NULL on invalid input
 * or allocation failure. This parses syntax; config_request validates job fields.
 */
struct cJSON *config_json_text(const char *text, Error *e);
/** @brief Resolve a worker job into an independently owned configuration.
 * @param[in] job Borrowed job object; strings are copied into owned metadata.
 * @param[in] validation Permit validation-only requests without render paths.
 * @param[out] c Non-null destination without owned allocations; release with
 * config_free even after failure.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on valid input; false on configuration or allocation failure.
 */
bool config_request(const struct cJSON *job, bool validation, Config *c, Error *e);
/** @brief Initialize built-ins, apply user and invocation JSON, then CLI overrides.
 * @param[in] argc Argument count.
 * @param[in] argv Non-null arguments, borrowed for the config lifetime.
 * @param[out] c Non-null destination; release owned metadata with config_free,
 * including after a failed parse. Must not already own allocations.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on valid input; false on CLI/configuration or allocation failure.
 */
bool config_parse(int argc, char **argv, Config *c, Error *e);
/** @brief Release owned metadata, request strings, rule indices and faces, then reset.
 * @param[in,out] c Non-null initialized config; safe for zero or failed parse state.
 */
void config_free(Config *c);
/** @brief Serialize render settings with requested, rather than derived, repeats.
 * @param[in] c Non-null resolved configuration, borrowed until return.
 * @return Caller-owned JSON to release with cJSON_Delete; NULL on allocation failure.
 */
struct cJSON *config_settings_json(const Config *c);
/** @brief Serialize resolved configuration, runtime settings and provenance.
 * @param[in] c Non-null resolved configuration, borrowed until return.
 * @return Caller-owned JSON to release with cJSON_Delete; NULL on allocation failure.
 */
struct cJSON *config_dump_json(const Config *c);
/** @brief Estimate owned allocation bytes charged to scene and render budgets.
 * @param[in] c Non-null initialized config; excludes sizeof(Config) and borrowed argv.
 * @return Allocation charge, including owned JSON, rules and face definitions.
 */
size_t config_memory_bytes(const Config *c);
/** @brief Validate movie settings and optional build support without rendering.
 * @param[in] c Non-null resolved configuration, including explicit-field flags.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true for valid movie settings or no movie request; false on invalid or
 * unsupported settings. Does not open input or output files.
 */
bool config_validate_movie(const Config *c, Error *e);
/** @brief Serialize mode-specific movie options for configuration and reports.
 * @param[in] c Non-null configuration with an enabled, validated movie mode.
 * @return Caller-owned JSON to release with cJSON_Delete; NULL on allocation failure.
 */
struct cJSON *config_movie_json(const Config *c);
/** @brief Replace decoded atom rules under configuration and selector budgets.
 * @param[in,out] c Non-null config; remains safe to free on failure.
 * @param[in] rules Borrowed JSON array; retained rule JSON is copied.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on invalid rules or allocation failure.
 */
bool config_atom_rules(Config *c, const struct cJSON *rules, Error *e);
/** @brief Replace decoded polygon definitions under face and vertex limits.
 * @param[in,out] c Non-null config; remains safe to free on failure.
 * @param[in] faces Borrowed face JSON; retained metadata is copied.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on invalid faces or allocation failure.
 */
bool config_faces(Config *c, const struct cJSON *faces, Error *e);
/** @brief Release face definitions and their JSON metadata, resetting face fields.
 * @param[in,out] c Non-null initialized config; safe repeatedly.
 */
void config_faces_free(Config *c);
#endif
