/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SCENE_H
#define VR_SCENE_H
#include "elements.h"
#include "atom_style.h"

#define VR_MAX_BONDS 10000000u
#define VR_MAX_CANDIDATES 200000000u

typedef enum { SPHERE, CYLINDER, CELL_EDGE } PrimitiveKind;
typedef struct {
    Vec3 a, b;
    double radius, distance;
    uint8_t color[4];
    uint8_t color_b[4]; /* Second endpoint color in atom-colored bond mode. */
    double color_split; /* 0.5 for atom-colored bonds; unused in uniform mode. */
    PrimitiveKind kind;
    unsigned element; /* Sphere element identity, zero for bond/cell primitives. */
    size_t source_a, source_b;
    int image[3], origin[3]; /* Bond displacement and start translation in base-cell units. */
} Primitive;
typedef struct {
    Vec3 vertices[3];
    uint8_t color[4], outline_color[4], edges;
    double outline_width;
    size_t face, instance;
    int origin[3];
} FaceTriangle;
typedef struct {
    Primitive *items;
    size_t count, capacity, atom_count, bond_count, base_bond_count;
    Vec3 supercell[3];
    bool cell_present[3], pbc[3];
    RuleStamp *style_rules; /* Optional winning per-property IDs, one per sphere. */
    FaceTriangle *faces;
    size_t triangle_count, face_count;
} Scene;
bool scene_faces_build(const Config *c, const Structure *s, const Vec3 *fractional,
                       const Vec3 basis[3], Scene *scene, size_t scratch, Error *e);
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
/** @brief Build a scene while charging additional live allocations to its budget.
 * @param[in] c Non-null validated configuration.
 * @param[in] s Non-null parsed input, borrowed until return.
 * @param[in] retained External allocation bytes, such as movie sampling/encoder
 * buffers. Excludes input atoms, configuration, fractional-coordinate scratch and
 * resolved atom styles, which are charged internally.
 * @param[out] scene Non-null zero scene; owns primitives/faces on success and
 * remains empty on failure. Release with scene_free before reuse.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on invalid geometry, limits or allocation failure.
 */
bool scene_build_retained(const Config *c, const Structure *s, size_t retained, Scene *scene, Error *e);
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
