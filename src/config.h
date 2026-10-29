/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_CONFIG_H
#define VR_CONFIG_H
#include "structure.h"

typedef enum { STYLE_SHADED, STYLE_ILLUSTRATED } RenderStyle;
typedef enum { BACKEND_ZBUFFER, BACKEND_CAIRO } RenderBackend;
typedef enum { ENCODER_LIBPNG, ENCODER_LIBDEFLATE, ENCODER_CAIRO } PngEncoder;
typedef enum { FILTER_ADAPTIVE, FILTER_NONE, FILTER_SUB, FILTER_UP, FILTER_DEFAULT } PngFilter;
/* Compression -1 and FILTER_DEFAULT request automatic settings. */
typedef struct { PngEncoder encoder; PngFilter filter; int compression; } PngOptions;

typedef struct {
    size_t width, height, repeat[3], threads, aa_scale;
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
    const char *input, *output; /* Borrowed argv strings. */
} Config;

/** @brief Initialize defaults, apply one JSON file, then explicit CLI overrides.
 * @param[in] argc Argument count.
 * @param[in] argv Borrowed arguments, valid for the config lifetime.
 * @param[out] c Non-null destination, safe to discard on failure; no owned resources.
 * @param[out] e Error diagnostic on failure.
 * @return true on valid input; false on CLI/config failure.
 */
bool config_parse(int argc, char **argv, Config *c, Error *e);
#endif
