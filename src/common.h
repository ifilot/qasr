/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef VR_COMMON_H
#define VR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define VR_MAX_ATOMS 1000000u
#define VR_MAX_INSTANCES 2000000u
#define VR_MAX_FRAMES 10000000u
#define VR_MAX_LINE 65536u
#define VR_MAX_INPUT_BYTES 68719476736ULL
#define VR_MAX_CONFIG_BYTES 1048576u
#define VR_MAX_SPECIES 118u

typedef struct { int code; char message[512]; } Error;
typedef struct {
    FILE *file;
    const char *path;
    size_t line;
    unsigned long long bytes;
    bool unterminated; /* Current line ended at EOF rather than newline. */
    char text[VR_MAX_LINE + 2];
} Reader;

/** @brief Set a bounded diagnostic.
 * @param[out] e Non-null diagnostic destination.
 * @param[in] code Process error category.
 * @param[in] format printf format followed by matching arguments.
 * @return false, for propagation from failing branches.
 */
bool fail(Error *e, int code, const char *format, ...);
/** @brief Read a bounded text line, rejecting embedded NUL and I/O errors.
 * @param[in,out] r Open reader; text is replaced and newline stripped.
 * @param[out] e Error destination, set only on failure.
 * @return 1 for a line, 0 for EOF, -1 for failure.
 */
int read_line(Reader *r, Error *e);
/** @brief Tokenize whitespace in place, ignoring a trailing ! or # comment.
 * @param[in,out] text Mutable NUL-terminated line.
 * @param[out] words Borrowed token pointers into text.
 * @param[in] capacity Number of available pointers.
 * @return Token count, or capacity + 1 on overflow.
 */
size_t split_words(char *text, char **words, size_t capacity);
/** @brief Parse a complete finite decimal number, including Fortran D exponents.
 * @param[in] text Non-null token.
 * @param[out] value Written only on success.
 * @return true on success.
 */
bool number(const char *text, double *value);
/** @brief Parse an unsigned decimal bounded integer; signs are rejected.
 * @param[in] text Non-null token.
 * @param[in] maximum Inclusive upper bound.
 * @param[out] value Written only on success.
 * @return true on success.
 */
bool integer(const char *text, size_t maximum, size_t *value);
#endif
