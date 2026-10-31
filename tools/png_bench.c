/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "image_io.h"
#include <stdlib.h>
#include <time.h>

static double milliseconds(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec-a.tv_sec)*1000+(double)(b.tv_nsec-a.tv_nsec)/1e6;
}

int main(int argc, char **argv)
{
    if (argc!=9) {
        fprintf(stderr,"usage: png-encode-bench WIDTH HEIGHT INPUT.rgba OUTPUT.png ENCODER LEVEL FILTER SAMPLES\n");
        return 2;
    }
    char *config_argv[]={argv[0],argv[3],"-o",argv[4],"--width",argv[1],"--height",argv[2],
                        "--png-encoder",argv[5],"--png-compression",argv[6],"--png-filter",argv[7]};
    Config c;Error e={0};size_t samples;
    if (!config_parse((int)(sizeof config_argv/sizeof *config_argv),config_argv,&c,&e) ||
        !integer(argv[8],10000,&samples) || samples<2) {
        fprintf(stderr,"invalid encoder benchmark configuration: %s\n",e.message);config_free(&c);return 2;
    }
    size_t bytes=c.width*c.height*4;
    Image image={.width=c.width,.height=c.height,.pixels=malloc(bytes)};
    double *elapsed=malloc(samples*sizeof *elapsed), *cpu=malloc(samples*sizeof *cpu);
    if (!image.pixels || !elapsed || !cpu) { fail(&e,6,"benchmark allocation failed");goto error; }
    FILE *input=fopen(argv[3],"rb");
    if (!input) { fail(&e,7,"cannot open raw RGBA input");goto error; }
    bool valid=fread(image.pixels,1,bytes,input)==bytes && fgetc(input)==EOF && !ferror(input);
    if (fclose(input)) valid=false;
    if (!valid) { fail(&e,7,"raw RGBA size/read error");goto error; }
    for (size_t i=0;i<3;++i)
        if (!png_write_options_atomic(argv[4],&image,&c.png,0,&e)) goto error;
    for (size_t i=0;i<samples;++i) {
        struct timespec a,b,ca,cb;
        clock_gettime(CLOCK_MONOTONIC,&a);clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ca);
        if (!png_write_options_atomic(argv[4],&image,&c.png,0,&e)) goto error;
        clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&cb);clock_gettime(CLOCK_MONOTONIC,&b);
        elapsed[i]=milliseconds(a,b);cpu[i]=milliseconds(ca,cb);
    }
    printf("{\"warmups\":3,\"samples\":%zu,\"encode_ms\":[",samples);
    for (size_t i=0;i<samples;++i) printf("%s%.6f",i ? "," : "",elapsed[i]);
    printf("],\"cpu_ms\":[");
    for (size_t i=0;i<samples;++i) printf("%s%.6f",i ? "," : "",cpu[i]);
    puts("]}");image_free(&image);free(elapsed);free(cpu);config_free(&c);return 0;
error:
    fprintf(stderr,"%s\n",e.message);image_free(&image);free(elapsed);free(cpu);
    config_free(&c);return e.code ? e.code : 6;
}
