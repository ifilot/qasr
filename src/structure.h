/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_STRUCTURE_H
#define VR_STRUCTURE_H
#include "common.h"

typedef struct { double x[3]; } Vec3;
typedef struct { unsigned element; Vec3 position; } Atom;
typedef enum { FORMAT_AUTO, FORMAT_POSCAR, FORMAT_OUTCAR, FORMAT_XML, FORMAT_XYZ, FORMAT_EXTXYZ,
               FORMAT_GAUSSIAN_INPUT, FORMAT_GAUSSIAN_OUTPUT, FORMAT_AMS } Format;
const char *format_name(Format format);
bool format_parse(const char *name, Format *format);
typedef struct {
    size_t frame, frame_count;
    Format format;
    bool recovered;
} SourceMetadata;
typedef struct {
    Vec3 lattice[3]; /* Rows are basis vectors in angstroms: Cartesian = sum(f[i]*row[i]). */
    Atom *atoms; /* Owned contiguous array; source index is its zero-based offset. */
    size_t count;
    bool cell_present[3], pbc[3]; /* Cell vectors and periodicity are independent. */
    SourceMetadata source;
} Structure;

/** @brief Look up an exact IUPAC symbol.
 * @param[in] symbol NUL-terminated, case-sensitive symbol.
 * @return Atomic number 1..118, or 0 if unknown.
 */
unsigned element_id(const char *symbol);
/** @brief Get a stable built-in element symbol.
 * @param[in] id Atomic number, 1..118.
 * @return Borrowed static string, or "?" for invalid IDs.
 */
const char *element_symbol(unsigned id);
/** @brief Validate finite, well-conditioned row-basis lattice.
 * @param[in] lattice Three basis vectors, angstroms.
 * @return true for supported nondegenerate geometry.
 */
bool lattice_valid(const Vec3 lattice[3]);
/** @brief Calculate signed row-basis cell volume.
 * @param[in] lattice Three basis vectors, angstroms.
 * @return Determinant in cubic angstroms.
 */
double cell_volume(const Vec3 lattice[3]);
/** @brief Release owned atoms and zero the structure; safe repeatedly.
 * @param[in,out] s Non-null initialized structure.
 */
void structure_free(Structure *s);
/* Complete missing directions for computation only; never modify the source cell. */
bool structure_basis(const Structure *s, Vec3 basis[3], Vec3 reciprocal[3], Error *e);
bool structure_validate(const Structure *s, Error *e);
#endif
