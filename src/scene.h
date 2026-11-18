/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_SCENE_H
#define VR_SCENE_H
#include "elements.h"
#include "atom_style.h"
#include "surface.h"

#define VR_MAX_BONDS 10000000u
#define VR_MAX_CANDIDATES 200000000u

/* SYMMETRY_AXIS is a flat-colored world-space rod, not a bond. */
typedef enum { SPHERE, CYLINDER, CELL_EDGE, SYMMETRY_AXIS } PrimitiveKind;
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
#define VR_SYMMETRY_ANCHORS 32u
/* Resolved symmetry element in world angstroms. Anchors are candidate label
 * positions: both axis tips, or points on the mirror-plane boundary. */
typedef struct {
    SymmetryKind kind;
    unsigned order;
    Vec3 origin, axis;
    Vec3 anchors[VR_SYMMETRY_ANCHORS];
    size_t anchor_count;
    double deviation; /* Largest atom-to-image distance; negative when unverified. */
    size_t deviation_atom;
    char label[32];
} SymmetryMark;
typedef struct {
    Primitive *items;
    size_t count, capacity, atom_count, bond_count, base_bond_count;
    Vec3 supercell[3];
    bool cell_present[3], pbc[3];
    RuleStamp *style_rules; /* Optional winning per-property IDs, one per sphere. */
    FaceTriangle *faces;
    size_t triangle_count, face_count;
    SymmetryMark symmetry;
} Scene;
/** @brief Resolve, verify and add the configured symmetry element to a scene.
 * @param[in] c Validated configuration with at most one operation.
 * @param[in] s Parsed input, borrowed until return.
 * @param[in] fractional Wrapped fractional positions, one per source atom.
 * @param[in] basis Completed lattice rows used for Cartesian conversion.
 * @param[in,out] scene Scene with atoms and faces; gains rods and plane triangles.
 * @param[in] scratch Bytes already charged to the aggregate allocation budget.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success or without an operation; false on unresolved geometry,
 * a structure that the operation does not map onto itself, or allocation limits.
 */
bool scene_symmetry_build(const Config *c, const Structure *s, const Vec3 *fractional,
                          const Vec3 basis[3], Scene *scene, size_t scratch, Error *e);
/** @brief Carry a point part of the way through a symmetry operation.
 * @param[in] mark Resolved element with origin, unit axis, kind and order.
 * @param[in] rotation Fraction 0..1 of the 2*pi/n rotation (Cn and Sn only).
 * @param[in] reflection Fraction 0..1 of the reflection or inversion: each
 * atom moves on a straight line through the plane (sigma, Sn) or centre (i).
 * @param[in] point World position in angstroms.
 * @return The intermediate position; both fractions at 1 give the full image.
 */
Vec3 symmetry_pose(const SymmetryMark *mark, double rotation, double reflection, Vec3 point);
/** Append one primitive under the scene allocation budget. */
bool scene_append(Scene *s, Primitive p, size_t scratch, Error *e);
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
/** @brief Finish a fit from projected geometry bounds on an oriented camera.
 * @param[in] c Configuration with margin, fixed framing and annotations.
 * @param[in] scene Scene supplying cell flags and any symmetry label anchors.
 * @param[in,out] camera Camera already oriented and laid out by camera_fit.
 * @param[in] lo Lower bounds along camera right/up in angstroms.
 * @param[in] hi Upper bounds along camera right/up in angstroms.
 * @param[out] e Error for invalid or unrepresentable extents.
 * @return true on success; sets scale, centre and the scale bar.
 */
bool camera_fit_bounds(const Config *c, const Scene *scene, Camera *camera, const double lo[2], const double hi[2], Error *e);
#endif
