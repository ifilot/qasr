/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "image_io.h"
#include <errno.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/vfs.h>
#include <linux/magic.h>
#endif
#ifdef VR_HAVE_LIBDEFLATE
#include <libdeflate.h>
#endif
#ifdef VR_HAVE_CAIRO
#include <cairo.h>
#endif

static void png_error_message(png_structp png, const char *message)
{
    Error *e=png_get_error_ptr(png);
    fail(e,7,"PNG encode failed: %s",message);png_longjmp(png,1);
}

static void png_warning_message(png_structp png, const char *message)
{
    (void)png;(void)message;
}

static bool write_libpng(FILE *file, const Image *image, const PngOptions *options, Error *e)
{
    png_structp png=png_create_write_struct(PNG_LIBPNG_VER_STRING,e,png_error_message,png_warning_message);
    png_infop info=png ? png_create_info_struct(png) : NULL;
    volatile bool ok=false;
    if (!png || !info) fail(e,6,"cannot allocate PNG encoder");
    else if (setjmp(png_jmpbuf(png))==0) {
        static const int filters[]={PNG_ALL_FILTERS,PNG_FILTER_NONE,PNG_FILTER_SUB,PNG_FILTER_UP};
        png_init_io(png,file);
        png_set_IHDR(png,info,(png_uint_32)image->width,(png_uint_32)image->height,8,PNG_COLOR_TYPE_RGBA,
                     PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_DEFAULT,PNG_FILTER_TYPE_DEFAULT);
        png_set_compression_level(png,options->compression);
        png_set_filter(png,PNG_FILTER_TYPE_BASE,filters[options->filter]);
        png_write_info(png,info);
        for (size_t y=0;y<image->height;++y) png_write_row(png,image->pixels+4*y*image->width);
        png_write_end(png,info);ok=true;
    }
    if (png) png_destroy_write_struct(&png,info ? &info : NULL);
    return ok;
}

#ifdef VR_HAVE_LIBDEFLATE
static void big_endian(uint8_t *p, uint32_t value)
{
    p[0]=(uint8_t)(value>>24);p[1]=(uint8_t)(value>>16);p[2]=(uint8_t)(value>>8);p[3]=(uint8_t)value;
}

static bool chunk(FILE *file, const char type[4], const void *data, size_t size, Error *e)
{
    if (size>2147483647u) return fail(e,6,"PNG chunk exceeds format limit");
    uint8_t header[8], tail[4];big_endian(header,(uint32_t)size);memcpy(header+4,type,4);
    uint32_t crc=libdeflate_crc32(0,type,4);
    if (size) crc=libdeflate_crc32(crc,data,size);
    big_endian(tail,crc);
    if (fwrite(header,1,8,file)!=8 || (size && fwrite(data,1,size,file)!=size) || fwrite(tail,1,4,file)!=4)
        return fail(e,7,"failed writing PNG chunk");
    return true;
}

static bool write_deflate(FILE *file, const Image *image, const PngOptions *options, Error *e)
{
    size_t stride=image->width*4, raw_size=(stride+1)*image->height;
    size_t bound=libdeflate_zlib_compress_bound(NULL,raw_size);
    uint8_t *raw=malloc(raw_size), *compressed=malloc(bound);
    struct libdeflate_compressor *compressor=libdeflate_alloc_compressor(options->compression);
    if (!raw || !compressed || !compressor) {
        free(raw);free(compressed);libdeflate_free_compressor(compressor);
        return fail(e,6,"fast PNG allocation failed");
    }
    for (size_t y=0;y<image->height;++y) {
        const uint8_t *row=image->pixels+y*stride, *previous=y ? row-stride : NULL;
        unsigned mode=options->filter==FILTER_SUB ? 1u : options->filter==FILTER_UP ? 2u : 0u;
        if (options->filter==FILTER_ADAPTIVE) {
            size_t scores[3]={0,0,0};
            for (size_t x=0;x<stride;++x) {
                uint8_t v[3]={row[x],(uint8_t)(row[x]-(x>=4 ? row[x-4] : 0)),
                             (uint8_t)(row[x]-(previous ? previous[x] : 0))};
                for (size_t k=0;k<3;++k) scores[k]+=v[k]<128 ? v[k] : 256u-v[k];
            }
            if (scores[1]<scores[mode]) mode=1;
            if (scores[2]<scores[mode]) mode=2;
        }
        uint8_t *destination=raw+y*(stride+1);destination[0]=(uint8_t)mode;
        for (size_t x=0;x<stride;++x) destination[x+1]=(uint8_t)(row[x]-
            (mode==1 ? (x>=4 ? row[x-4] : 0) : mode==2 && previous ? previous[x] : 0));
    }
    size_t size=libdeflate_zlib_compress(compressor,raw,raw_size,compressed,bound);
    bool ok=false;
    if (!size) fail(e,6,"fast PNG compression failed");
    else {
        static const uint8_t signature[8]={137,80,78,71,13,10,26,10};
        uint8_t header[13]={0};big_endian(header,(uint32_t)image->width);big_endian(header+4,(uint32_t)image->height);
        header[8]=8;header[9]=6;
        if (fwrite(signature,1,8,file)!=8) fail(e,7,"failed writing PNG signature");
        else ok=chunk(file,"IHDR",header,13,e) && chunk(file,"IDAT",compressed,size,e) && chunk(file,"IEND",NULL,0,e);
    }
    libdeflate_free_compressor(compressor);free(raw);free(compressed);return ok;
}
#endif

#ifdef VR_HAVE_CAIRO
static cairo_status_t cairo_png_write(void *closure, const unsigned char *data, unsigned int size)
{
    return fwrite(data,1,size,closure)==size ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
}

static bool write_cairo(FILE *file, const Image *image, Error *e)
{
    int stride=cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32,(int)image->width);
    if (stride<0) return fail(e,6,"invalid Cairo PNG stride");
    uint8_t *data=malloc((size_t)stride*image->height);
    if (!data) return fail(e,6,"Cairo PNG allocation failed");
    for (size_t y=0;y<image->height;++y) for (size_t x=0;x<image->width;++x) {
        const uint8_t *p=image->pixels+4*(y*image->width+x);
        uint32_t value=((uint32_t)p[3]<<24) | (((uint32_t)p[0]*p[3]+127)/255<<16) |
            (((uint32_t)p[1]*p[3]+127)/255<<8) | ((uint32_t)p[2]*p[3]+127)/255;
        memcpy(data+y*(size_t)stride+4*x,&value,4);
    }
    cairo_surface_t *surface=cairo_image_surface_create_for_data(data,CAIRO_FORMAT_ARGB32,
        (int)image->width,(int)image->height,stride);
    cairo_status_t status=cairo_surface_status(surface);
    if (status==CAIRO_STATUS_SUCCESS) status=cairo_surface_write_to_png_stream(surface,cairo_png_write,file);
    cairo_surface_destroy(surface);free(data);
    if (status!=CAIRO_STATUS_SUCCESS)
        return fail(e,status==CAIRO_STATUS_NO_MEMORY ? 6 : 7,"Cairo PNG failed: %s",cairo_status_to_string(status));
    return true;
}
#endif

PngOptions png_resolve_options(const char *path, PngOptions options)
{
    if (options.filter==FILTER_DEFAULT)
        options.filter=options.encoder==ENCODER_LIBDEFLATE ? FILTER_NONE : FILTER_ADAPTIVE;
    if (options.compression!=-1) return options;
    options.compression=options.encoder==ENCODER_LIBDEFLATE ? 0 : 1;
#ifdef __linux__
    /* The measured 9p/Windows output path favors smaller level-1 streams;
     * native Linux storage favors level 0. Explicit levels bypass this policy. */
    if (options.encoder==ENCODER_LIBDEFLATE) {
        size_t n=strlen(path);
        if (n<=65536) {
            char *parent=malloc(n+2);
            if (parent) {
                memcpy(parent,path,n+1);
                char *slash=strrchr(parent,'/');
                if (!slash) strcpy(parent,".");
                else if (slash==parent) parent[1]='\0';
                else *slash='\0';
                struct statfs filesystem;
                if (!statfs(parent,&filesystem) && (unsigned long)filesystem.f_type==V9FS_MAGIC)
                    options.compression=1;
                free(parent);
            }
        }
    }
#else
    (void)path;
#endif
    return options;
}

bool png_write_options_atomic(const char *path, const Image *image, const PngOptions *options,
                              size_t retained_bytes, Error *e)
{
    PngOptions resolved=png_resolve_options(path,*options);
    options=&resolved;
    if (!image->pixels || !image->width || !image->height || image->width>16384 || image->height>16384 ||
        image->width>67108864u/image->height) return fail(e,6,"invalid PNG image dimensions");
    if ((unsigned)options->encoder>ENCODER_CAIRO || (unsigned)options->filter>FILTER_UP || options->compression<0 || options->compression>9)
        return fail(e,2,"invalid PNG options");
    size_t count=image->width*image->height, extra=0, reserve=8u*1024u*1024u;
    if (options->encoder==ENCODER_LIBDEFLATE) {
#ifdef VR_HAVE_LIBDEFLATE
        size_t raw=(image->width*4+1)*image->height;
        extra=raw+libdeflate_zlib_compress_bound(NULL,raw);reserve=32u*1024u*1024u;
#else
        return fail(e,3,"libdeflate encoder requires ENABLE_LIBDEFLATE");
#endif
    } else if (options->encoder==ENCODER_CAIRO) {
#ifdef VR_HAVE_CAIRO
        if (options->compression!=1 || options->filter!=FILTER_ADAPTIVE)
            return fail(e,2,"Cairo PNG uses internal compression/filter settings");
        extra=count*4;
#else
        return fail(e,3,"Cairo PNG requires ENABLE_CAIRO_EXPERIMENTS");
#endif
    }
    if (retained_bytes>VR_MEMORY_BUDGET-reserve || extra>VR_MEMORY_BUDGET-reserve-retained_bytes ||
        count>(VR_MEMORY_BUDGET-reserve-retained_bytes-extra)/4)
        return fail(e,6,"PNG encoder and retained scene exceed 1 GiB allocation budget");
    size_t n=strlen(path);
    if (n>65536) return fail(e,7,"output path exceeds limit");
    char *temp=malloc(n+12);
    if (!temp) return fail(e,6,"output path allocation failed");
    memcpy(temp,path,n);memcpy(temp+n,".tmp.XXXXXX",12);
    int fd=mkstemp(temp);
    if (fd<0) { free(temp);return fail(e,7,"%s: cannot create temporary output: %s",path,strerror(errno)); }
    FILE *file=fdopen(fd,"wb");
    if (!file) { close(fd);unlink(temp);free(temp);return fail(e,7,"%s: cannot open temporary stream",path); }
    bool ok=false;
    switch (options->encoder) {
    case ENCODER_LIBPNG:ok=write_libpng(file,image,options,e);break;
#ifdef VR_HAVE_LIBDEFLATE
    case ENCODER_LIBDEFLATE:ok=write_deflate(file,image,options,e);break;
#endif
#ifdef VR_HAVE_CAIRO
    case ENCODER_CAIRO:ok=write_cairo(file,image,e);break;
#endif
    default:break;
    }
    if (fclose(file)) ok=fail(e,7,"%s: failed closing temporary PNG",path);
    if (ok) ok=check_cancel(e);
    if (ok && rename(temp,path)) ok=fail(e,7,"%s: failed replacing output: %s",path,strerror(errno));
    if (!ok) unlink(temp);
    free(temp);return ok;
}

bool png_write_atomic(const char *path, const Image *image, Error *e)
{
    const PngOptions options={ENCODER_LIBPNG,FILTER_ADAPTIVE,1};
    return png_write_options_atomic(path,image,&options,0,e);
}
