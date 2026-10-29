/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include <errno.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void png_error_message(png_structp png, const char *message)
{
    Error *e=png_get_error_ptr(png);
    fail(e,7,"PNG encode failed: %s",message);
    png_longjmp(png,1);
}

static void png_warning_message(png_structp png, const char *message)
{
    (void)png; (void)message;
}

bool png_write_atomic(const char *path, const Image *image, Error *e)
{
    size_t n=strlen(path);
    if (n > 65536) return fail(e,7,"output path exceeds limit");
    char *temp=malloc(n+12);
    if (!temp) return fail(e,6,"output path allocation failed");
    memcpy(temp,path,n); memcpy(temp+n,".tmp.XXXXXX",12);
    int fd=mkstemp(temp);
    if (fd<0) { free(temp);return fail(e,7,"%s: cannot create temporary output: %s",path,strerror(errno)); }
    FILE *file=fdopen(fd,"wb");
    if (!file) { close(fd);unlink(temp);free(temp);return fail(e,7,"%s: cannot open temporary stream",path); }
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,e,png_error_message,png_warning_message);
    png_infop info=png ? png_create_info_struct(png) : NULL;
    bool ok=false;
    if (!png || !info) fail(e,6,"cannot allocate PNG encoder");
    else if (setjmp(png_jmpbuf(png))==0) {
        png_init_io(png,file);
        png_set_IHDR(png,info,(png_uint_32)image->width,(png_uint_32)image->height,8,PNG_COLOR_TYPE_RGBA,
                     PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
        png_set_compression_level(png,1);
        png_write_info(png,info);
        for (size_t y=0;y<image->height;++y) png_write_row(png,image->pixels+4*y*image->width);
        png_write_end(png,info);
        ok=true;
    }
    if (png) png_destroy_write_struct(&png,info ? &info : NULL);
    if (fclose(file)) ok=fail(e,7,"%s: failed closing temporary PNG",path);
    if (ok && rename(temp,path)) ok=fail(e,7,"%s: failed replacing output: %s",path,strerror(errno));
    if (!ok) unlink(temp);
    free(temp); return ok;
}
