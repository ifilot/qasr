/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef VR_HAVE_JPEG
#include <jpeglib.h>
#endif
#ifdef VR_HAVE_WEBP
#include <webp/decode.h>
#endif
#define REQUIRE(x) do {if (!(x)) {fprintf(stderr,"line %d: %s (%s)\n",__LINE__,#x,e.message);return 1;}} while (0)

int main(void)
{
    Error e={0};PngOptions png={ENCODER_LIBPNG,FILTER_ADAPTIVE,1};
    uint8_t pixels[33*19*4];Image image={.width=33,.height=19,.pixels=pixels};
    for (size_t y=0;y<19;++y) for (size_t x=0;x<33;++x) {
        uint8_t *p=pixels+4*(y*33+x);
        p[0]=(uint8_t)(x*5);p[1]=(uint8_t)(y*10);p[2]=83;p[3]=(uint8_t)((x+y)*5);
    }
    char directory[]="image-codecs.XXXXXX";REQUIRE(mkdtemp(directory));
    char path[128];
    REQUIRE(!strcmp(image_format_name("dir.jpg/legacy"),"png"));
    REQUIRE(!strcmp(image_format_name("image.JPEG"),"jpeg"));
    REQUIRE(!strcmp(image_format_name("image.WeBp"),"webp"));
    for (int codec=0;codec<2;++codec) {
        snprintf(path,sizeof path,"%s/image.%s",directory,codec ? "webp" : "jpg");
        bool available=false;
#ifdef VR_HAVE_JPEG
        if (!codec) available=true;
#endif
#ifdef VR_HAVE_WEBP
        if (codec) available=true;
#endif
        e=(Error){0};
        REQUIRE(image_output_validate(path,&e)==available);
        if (!available) {REQUIRE(e.code==3);continue;}
        /* Budget and dimension checks precede pixel reads or file creation. */
        e=(Error){0};REQUIRE(!image_write_atomic(path,&image,&png,VR_MEMORY_BUDGET,&e) && e.code==6);
        Image invalid=image;invalid.width=16384;invalid.height=0;e=(Error){0};
        REQUIRE(!image_write_atomic(path,&invalid,&png,0,&e) && e.code==6);
        if (codec) {
            invalid.height=1;e=(Error){0};
            REQUIRE(!image_write_atomic(path,&invalid,&png,0,&e) && e.code==6);
        }
        if (!codec) {
            FILE *f=fopen(path,"wb");REQUIRE(f);REQUIRE(fputs("keep",f)>=0);REQUIRE(!fclose(f));
            e=(Error){0};REQUIRE(!image_write_atomic(path,&image,&png,0,&e) && e.code==2);
            char saved[5]={0};f=fopen(path,"rb");REQUIRE(f);REQUIRE(fread(saved,1,4,f)==4);fclose(f);
            REQUIRE(!strcmp(saved,"keep"));
            for (size_t i=0;i<33*19;++i) pixels[4*i+3]=255;
        } else for (size_t i=0;i<33*19;++i) pixels[4*i+3]=(uint8_t)(i%256);
        e=(Error){0};REQUIRE(image_write_atomic(path,&image,&png,0,&e));
#ifdef VR_HAVE_JPEG
        if (!codec) {
            FILE *f=fopen(path,"rb");REQUIRE(f);
            struct jpeg_decompress_struct info;struct jpeg_error_mgr error;
            info.err=jpeg_std_error(&error);jpeg_create_decompress(&info);jpeg_stdio_src(&info,f);
            REQUIRE(jpeg_read_header(&info,TRUE)==JPEG_HEADER_OK);
            REQUIRE(info.comp_info[0].h_samp_factor==1 && info.comp_info[0].v_samp_factor==1);
            info.out_color_space=JCS_RGB;jpeg_start_decompress(&info);
            REQUIRE(info.output_width==33 && info.output_height==19 && info.output_components==3);
            uint8_t row[33*3];unsigned long difference=0;
            while (info.output_scanline<info.output_height) {
                size_t y=info.output_scanline;JSAMPROW rows[]={row};REQUIRE(jpeg_read_scanlines(&info,rows,1)==1);
                for (size_t x=0;x<33;++x) for (size_t k=0;k<3;++k)
                    difference+=(unsigned long)abs((int)row[x*3+k]-(int)pixels[(y*33+x)*4+k]);
            }
            REQUIRE(difference<33*19*3*4);jpeg_finish_decompress(&info);jpeg_destroy_decompress(&info);fclose(f);
        }
#endif
#ifdef VR_HAVE_WEBP
        if (codec) {
            FILE *f=fopen(path,"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long size=ftell(f);REQUIRE(size>0);rewind(f);
            uint8_t *data=malloc((size_t)size);REQUIRE(data);REQUIRE(fread(data,1,(size_t)size,f)==(size_t)size);fclose(f);
            int width,height;uint8_t *decoded=WebPDecodeRGBA(data,(size_t)size,&width,&height);
            REQUIRE(decoded && width==33 && height==19);REQUIRE(!memcmp(decoded,pixels,sizeof pixels));
            WebPFree(decoded);free(data);
        }
#endif
        REQUIRE(!unlink(path));
    }
    REQUIRE(!rmdir(directory));puts("Image codec round trips and limits passed");return 0;
}
