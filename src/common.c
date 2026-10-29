/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

bool fail(Error *e, int code, const char *format, ...)
{
    va_list args;
    e->code = code;
    static const char *names[]={"success","error","invalid_configuration","unsupported_capability","invalid_input","invalid_geometry","resource_limit","io_error","internal_error"};
    snprintf(e->name,sizeof e->name,"%s",code>=0 && code<=8 ? names[code] : "error");
    e->path[0]='\0';
    va_start(args, format);
    vsnprintf(e->message, sizeof e->message, format, args);
    va_end(args);
    return false;
}

bool fail_at(Error *e, int code, const char *name, const char *path, const char *format, ...)
{
    e->code=code;
    snprintf(e->name,sizeof e->name,"%s",name);
    snprintf(e->path,sizeof e->path,"%s",path ? path : "");
    va_list args;va_start(args,format);vsnprintf(e->message,sizeof e->message,format,args);va_end(args);
    return false;
}

int read_line(Reader *r, Error *e)
{
    size_t n = 0;
    int c;
    while ((c = fgetc(r->file)) != EOF) {
        if (++r->bytes > VR_MAX_INPUT_BYTES || c == 0) {
            fail(e, 4, "%s:%zu: input byte/line limit exceeded or embedded NUL", r->path, r->line + 1);
            return -1;
        }
        if (c == '\n') break;
        if (n >= VR_MAX_LINE) {
            fail(e, 4, "%s:%zu: input line limit exceeded", r->path, r->line + 1);
            return -1;
        }
        r->text[n++] = (char)c;
    }
    if (ferror(r->file)) {
        fail(e, 7, "%s:%zu: input read error", r->path, r->line + 1);
        return -1;
    }
    if (c == EOF && n == 0) return 0;
    r->unterminated = c == EOF;
    r->text[n] = '\0';
    ++r->line;
    return 1;
}

size_t split_words(char *text, char **words, size_t capacity)
{
    size_t n = 0;
    char *p = text;
    while (*p) {
        while (isspace((unsigned char)*p)) ++p;
        if (!*p || *p == '!' || *p == '#') break;
        if (n == capacity) return capacity + 1;
        words[n++] = p;
        while (*p && !isspace((unsigned char)*p) && *p != '!' && *p != '#') ++p;
        if (*p == '!' || *p == '#') { *p = '\0'; break; }
        if (*p) *p++ = '\0';
    }
    return n;
}

bool number(const char *text, double *value)
{
    char buf[128], *end;
    size_t n = strlen(text);
    if (!n || n >= sizeof buf) return false;
    bool digit = false;
    for (size_t i = 0; i <= n; ++i) {
        char c = text[i];
        if (i < n && !(isdigit((unsigned char)c) || c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E' || c == 'd' || c == 'D')) return false;
        if (isdigit((unsigned char)c)) digit = true;
        buf[i] = (c == 'd' || c == 'D') ? 'e' : c;
    }
    errno = 0;
    double x = strtod(buf, &end);
    if (!digit || *end || errno == ERANGE || !isfinite(x)) return false;
    *value = x;
    return true;
}

bool integer(const char *text, size_t maximum, size_t *value)
{
    size_t n = 0;
    if (!*text) return false;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        size_t d = (size_t)(*p - '0');
        if (d > maximum || n > (maximum - d) / 10) return false;
        n = n * 10 + d;
    }
    *value = n;
    return true;
}
