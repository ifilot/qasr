/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SCENE_H
#define VR_SCENE_H
#include "elements.h"
#include "atom_style.h"

#define VR_MEMORY_BUDGET ((size_t)1073741824)
#define VR_MAX_BONDS 10000000u
#define VR_MAX_CANDIDATES 200000000u

typedef enum { SPHERE, CYLINDER, CELL_EDGE } PrimitiveKind;
typedef struct {
    Vec3 a, b;
    double radius, distance;
    uint8_t color[4];
    PrimitiveKind kind;
    unsigned element; /* Sphere element identity, zero for bond/cell primitives. */
    size_t source_a, source_b;
    int image[3], origin[3]; /* Bond displacement and start translation in base-cell units. */
} Primitive;
typedef struct {
    Primitive *items;
    size_t count, capacity, atom_count, bond_count, base_bond_count;
    Vec3 supercell[3];
    RuleStamp *style_rules; /* Optional winning per-property IDs, one per sphere. */
} Scene;
size_t scene_memory_bytes(const Scene *s);
typedef struct {
    Vec3 right, up, view;
    double center_x, center_y, scale;
    size_t width, height;
    size_t viewport_x, viewport_y, viewport_width, viewport_height;
    size_t legend_columns, legend_rows, legend_column_width, legend_width, legend_height;
    size_t footer_height;
    double scale_bar_A, scale_bar_pixels;
    char scale_bar_label[32];
} Camera;

/** @brief Build wrapped instances, complete periodic bonds and the repeated cell.
 * @param[in] c Validated configuration.
 * @param[in] s Parsed input, borrowed throughout.
 * @param[out] scene Zero-initialized destination; owns primitives on success.
 * @param[out] e Error on failure; scene is freed and reset.
 * @return true on success. Enumeration and allocation limits fail explicitly.
 */
bool scene_build(const Config *c, const Structure *s, Scene *scene, Error *e);
/** @brief Release a scene and reset all fields; safe repeatedly.
 * @param[in,out] scene Non-null initialized scene.
 */
void scene_free(Scene *scene);
/** @brief Fit all primitives uniformly within the requested image aspect ratio.
 * @param[in] c Configuration, including camera orientation and margin.
 * @param[in] scene Complete scene with atom radii and bond widths.
 * @param[out] camera Camera with pixels-per-angstrom scale.
 * @param[out] e Error for invalid or unrepresentable extents.
 * @return true on success.
 */
bool camera_fit(const Config *c, const Scene *scene, Camera *camera, Error *e);
/** @brief Transform a world point to output pixels and camera depth (angstroms).
 * @param[in] camera Fitted camera.
 * @param[in] point World Cartesian position.
 * @return x/y centered pixel coordinates with y increasing upward; z toward camera.
 */
Vec3 camera_project(const Camera *camera, Vec3 point);
#endif
