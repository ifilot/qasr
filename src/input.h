/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef QASR_INPUT_H
#define QASR_INPUT_H
#include "structure.h"

/* Input-only contract: readers never consume render settings. */
typedef struct {
    const char *input;
    Format format;
    bool final_frame, recover;
    size_t frame, species_count;
    const unsigned *species;
} InputOptions;
/* Read a path into a zero-initialized destination. Owns atoms on success;
 * empty on failure. Selecting frame N still validates later recognized frames. */
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
