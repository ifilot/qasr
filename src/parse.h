/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_PARSE_H
#define VR_PARSE_H
#include "config.h"

/** @brief Read and validate a complete input under the selected recovery policy.
 * @param[in] c Validated config; input path and species mapping are borrowed.
 * @param[out] s Non-null zero-initialized destination. Owns atoms on success;
 * remains empty on failure. Release with structure_free.
 * @param[out] e Error diagnostic on failure.
 * @return true on success. Numeric trajectory selection still validates later frames.
 */
bool parse_structure(const Config *c, Structure *s, Error *e);
#ifdef VR_HAVE_XML
/** Stream a complete vasprun.xml document from an open, borrowed file.
 * Retains only metadata, the current structure and the selected snapshot.
 * The caller owns/cleans s, including on failure. No truncated XML recovery.
 */
bool parse_xml(FILE *file, const Config *c, Structure *s, Error *e);
#endif
#endif
