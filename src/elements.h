/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_ELEMENTS_H
#define VR_ELEMENTS_H
#include "config.h"
#include <stdint.h>
/** @brief Get effective display radius in angstroms, including sphere scale.
 * @param[in] c Validated configuration.
 * @param[in] z Atomic number 1..118.
 * @return Effective radius; scene construction validates its numeric range.
 */
double element_radius(const Config *c, unsigned z);
/** @brief Resolve an element color from overrides or the pinned preset.
 * @param[in] c Configuration.
 * @param[in] z Atomic number.
 * @param[out] rgba Four 8-bit straight-alpha channels.
 */
void element_color(const Config *c, unsigned z, uint8_t rgba[4]);
/** @brief Resolve symmetric pair cutoff in angstroms.
 * @param[in] c Configuration; explicit pair rules override the preset.
 * @param[in] a First atomic number.
 * @param[in] b Second atomic number.
 * @return Positive cutoff, including Atom Architect's Al-C exception.
 */
double bond_cutoff(const Config *c, unsigned a, unsigned b);
/** @brief Decode a validated #RRGGBB or #RRGGBBAA color.
 * @param[in] text Validated color string.
 * @param[out] rgba Four straight-alpha channels.
 */
void decode_color(const char *text, uint8_t rgba[4]);
#endif
