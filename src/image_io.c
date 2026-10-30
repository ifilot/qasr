/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#ifdef VR_HAVE_JPEG
#include <jpeglib.h>
#include <setjmp.h>
#endif
#ifdef VR_HAVE_WEBP
#include <webp/encode.h>
#endif

const char *image_format_name(const char *path)
{
    const char *leaf=strrchr(path,'/');
    const char *ext=strrchr(leaf ? leaf+1 : path,'.');
    if (ext && (!strcasecmp(ext,".jpg") || !strcasecmp(ext,".jpeg"))) return "jpeg";
    if (ext && !strcasecmp(ext,".webp")) return "webp";
    return "png"; /* Preserve PNG for extensionless and other legacy paths. */
}

bool image_output_validate(const char *path, Error *e)
{
    const char *format=image_format_name(path);
#ifndef VR_HAVE_JPEG
    if (!strcmp(format,"jpeg")) return fail(e,3,"JPEG output requires ENABLE_JPEG=ON (system libjpeg)");
#endif
#ifndef VR_HAVE_WEBP
    if (!strcmp(format,"webp")) return fail(e,3,"WebP output requires ENABLE_WEBP=ON (system libwebp)");
#endif
    (void)format;(void)e;return true;
}

#ifdef VR_HAVE_JPEG
struct JpegError { struct jpeg_error_mgr manager; jmp_buf jump; Error *error; };
static void jpeg_failure(j_common_ptr info)
{
    struct JpegError *error=(struct JpegError *)info->err;
    char message[JMSG_LENGTH_MAX];
    (*info->err->format_message)(info,message);
    fail(error->error,7,"JPEG encode failed: %s",message);
    longjmp(error->jump,1);
}
static bool write_jpeg(FILE *file, const Image *image, Error *e)
{
    struct jpeg_compress_struct *info=calloc(1,sizeof *info);
    uint8_t *row=malloc(image->width*3);
    if (!info || !row) {free(info);free(row);return fail(e,6,"JPEG encoder allocation failed");}
    struct JpegError error;
    info->err=jpeg_std_error(&error.manager);error.manager.error_exit=jpeg_failure;error.error=e;
    volatile bool ok=false;
    if (!setjmp(error.jump)) {
        jpeg_create_compress(info);jpeg_stdio_dest(info,file);
        info->image_width=(JDIMENSION)image->width;info->image_height=(JDIMENSION)image->height;
        info->input_components=3;info->in_color_space=JCS_RGB;
        jpeg_set_defaults(info);jpeg_set_quality(info,90,TRUE);
        /* 4:4:4 avoids color bleeding around narrow bonds and text. */
        for (int i=0;i<3;++i) info->comp_info[i].h_samp_factor=info->comp_info[i].v_samp_factor=1;
        jpeg_start_compress(info,TRUE);
        while (info->next_scanline<info->image_height) {
            if (!check_cancel(e)) goto done;
            const uint8_t *source=image->pixels+4*image->width*info->next_scanline;
            for (size_t x=0;x<image->width;++x) memcpy(row+3*x,source+4*x,3);
            JSAMPROW rows[]={row};jpeg_write_scanlines(info,rows,1);
        }
        jpeg_finish_compress(info);ok=true;
    }
done:
    jpeg_destroy_compress(info);free(info);free(row);return ok;
}
#endif

#ifdef VR_HAVE_WEBP
struct WebpOutput { FILE *file; Error *error; };
static int webp_write(const uint8_t *data, size_t size, const WebPPicture *picture)
{
    struct WebpOutput *output=picture->custom_ptr;
    return check_cancel(output->error) && fwrite(data,1,size,output->file)==size;
}
static bool write_webp(FILE *file, const Image *image, Error *e)
{
    WebPConfig config;WebPPicture picture;
    if (!WebPConfigInit(&config) || !WebPPictureInit(&picture)) return fail(e,7,"incompatible WebP library ABI");
    config.lossless=1;config.quality=75;config.method=4;config.exact=1;
    picture.width=(int)image->width;picture.height=(int)image->height;picture.use_argb=1;
    struct WebpOutput output={file,e};picture.writer=webp_write;picture.custom_ptr=&output;
    bool ok=WebPPictureImportRGBA(&picture,image->pixels,(int)(image->width*4))!=0;
    if (ok) ok=WebPEncode(&config,&picture)!=0;
    if (!ok && !e->code) fail(e,picture.error_code==VP8_ENC_ERROR_OUT_OF_MEMORY ? 6 : 7,
                             "WebP encode failed (error %d)",(int)picture.error_code);
    WebPPictureFree(&picture);return ok;
}
#endif

bool image_write_atomic(const char *path, const Image *image, const PngOptions *png,
                        size_t retained_bytes, Error *e)
{
    const char *format=image_format_name(path);
    if (!strcmp(format,"png")) return png_write_options_atomic(path,image,png,retained_bytes,e);
    if (!image_output_validate(path,e)) return false;
    if (!image->pixels || !image->width || !image->height || image->width>16384 || image->height>16384 ||
        image->width>67108864u/image->height) return fail(e,6,"invalid image dimensions");
    bool webp=!strcmp(format,"webp");
    if (webp && (image->width>16383 || image->height>16383)) return fail(e,6,"WebP dimensions must not exceed 16383");
    /* Conservative codec workspace allowance, including libwebp's ARGB copy,
     * hash chains and lossless transforms; reject before reading image data. */
    size_t reserve=webp ? 64u*1024u*1024u : 8u*1024u*1024u;
    size_t per_pixel=webp ? 128 : 4;
    if (retained_bytes>VR_MEMORY_BUDGET-reserve ||
        image->width*image->height>(VR_MEMORY_BUDGET-reserve-retained_bytes)/per_pixel)
        return fail(e,6,"image encoder and retained scene exceed 1 GiB allocation budget");
    if (!webp) for (size_t i=0;i<image->width*image->height;++i)
        if (image->pixels[4*i+3]!=255) return fail(e,2,"JPEG cannot store transparency; use an opaque --background or PNG/WebP");
    size_t n=strlen(path);
    if (n>65536) return fail(e,7,"output path exceeds limit");
    char *temp=malloc(n+12);
    if (!temp) return fail(e,6,"output path allocation failed");
    memcpy(temp,path,n);memcpy(temp+n,".tmp.XXXXXX",12);
    int fd=mkstemp(temp);
    if (fd<0) {free(temp);return fail(e,7,"%s: cannot create temporary output: %s",path,strerror(errno));}
    FILE *file=fdopen(fd,"wb");
    if (!file) {close(fd);unlink(temp);free(temp);return fail(e,7,"%s: cannot open temporary stream",path);}
    bool ok=false;
#ifdef VR_HAVE_JPEG
    if (!webp) ok=write_jpeg(file,image,e);
#endif
#ifdef VR_HAVE_WEBP
    if (webp) ok=write_webp(file,image,e);
#endif
    if (fclose(file)) ok=fail(e,7,"%s: failed closing temporary image",path);
    if (ok) ok=check_cancel(e);
    if (ok && rename(temp,path)) ok=fail(e,7,"%s: failed replacing output: %s",path,strerror(errno));
    if (!ok) unlink(temp);
    free(temp);return ok;
}
