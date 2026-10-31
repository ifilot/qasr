/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QASR_INPUT_H
#define QASR_INPUT_H
#include "structure.h"

/** Input-only settings; readers never consume render configuration.
 * input, species and visit_context are borrowed until input_read returns. */
typedef struct {
    const char *input;
    Format format;
    bool final_frame, recover;
    size_t frame, species_count;
    const unsigned *species;
    /* Optional synchronous observer on the parsing thread. frame and its atoms
     * are borrowed only for the callback; copy anything needed after return.
     * index is zero-based within the reader's frame sequence; source metadata
     * may not yet be final. Return false and set e to abort the read. Callbacks
     * can precede a later parse failure, so external effects are not rolled back.
     * XYZ/EXTXYZ, OUTCAR and XML expose trajectory frames. Gaussian output can
     * expose both orientation families with separate indices; POSCAR has no
     * callbacks. This observer is not a uniform iterator across all formats. */
    bool (*visit)(const Structure *frame, size_t index, void *context, Error *e);
    void *visit_context;
} InputOptions;
/** @brief Read and validate a path under the selected recovery/frame policy.
 * @param[in] options Non-null input settings and borrowed callback context.
 * @param[out] s Non-null zero structure; owns atoms on success, empty on failure.
 * Release with structure_free before reuse.
 * @param[out] e Non-null diagnostic destination, set on failure.
 * @return true on success; false on parse, I/O, callback or allocation failure.
 * Numeric frame selection still validates later recognized frames. A callback
 * may have observed complete frames even when the overall read fails.
 */
bool input_read(const InputOptions *options, Structure *s, Error *e);
bool read_xyz(Reader *r, const InputOptions *o, Structure *s, Error *e);
bool read_gaussian_input(Reader *r, const InputOptions *o, Structure *s, Error *e);
bool read_gaussian_output(Reader *r, const InputOptions *o, Structure *s, Error *e);
bool read_ams(Reader *r, const InputOptions *o, Structure *s, Error *e);

#ifdef VR_HAVE_XML
/* Borrowed stream; retains selected/current snapshots and validates to EOF. */
bool parse_xml(FILE *file, const InputOptions *c, Structure *s, Error *e);
#endif
#endif
