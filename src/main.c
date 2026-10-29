/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "parse.h"
#include "render.h"
#include "version.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static void print_vector(Vec3 v)
{
    printf("[%.17g,%.17g,%.17g]", v.x[0], v.x[1], v.x[2]);
}

static bool inspect(const Config *c, const Structure *s)
{
    printf("{\"format\":\"%s\",\"frame\":%zu,\"frame_count\":%zu,\"recovered\":%s,\"atom_count\":%zu,\"lattice_A\":[",
           s->format == FORMAT_POSCAR ? "poscar" : s->format == FORMAT_XML ? "xml" : "outcar", s->frame, s->frame_count, s->recovered ? "true" : "false", s->count);
    for (size_t i = 0; i < 3; ++i) { if (i) putchar(','); print_vector(s->lattice[i]); }
    printf("],\"atoms\":[");
    for (size_t i = 0; i < s->count; ++i) {
        if (i) putchar(',');
        printf("{\"index\":%zu,\"element\":\"%s\",\"position_A\":", i, element_symbol(s->atoms[i].element));
        print_vector(s->atoms[i].position); putchar('}');
    }
    printf("],\"settings\":{\"image\":{\"width\":%zu,\"height\":%zu,\"background\":\"%s\",\"style\":\"%s\",\"aa\":\"%s\",\"backend\":\"%s\",\"depth_fade\":%.17g,\"outline_width\":%.17g",
           c->width, c->height, c->background,c->style==STYLE_ILLUSTRATED ? "illustrated" : "shaded",
           c->adaptive_aa ? "adaptive" : c->aa_scale==1 ? "native" : c->aa_scale==4 ? "4x" : "2x",
           c->backend==BACKEND_CAIRO ? "cairo" : "zbuffer",c->depth_fade,c->outline_width);
    printf(",\"png_encoder\":\"%s\",\"png_filter\":\"%s\",\"png_compression\":",
           c->png.encoder==ENCODER_CAIRO ? "cairo" : c->png.encoder==ENCODER_LIBDEFLATE ? "libdeflate" : "libpng",
           c->png.encoder==ENCODER_CAIRO ? "internal" : c->png.filter==FILTER_NONE ? "none" :
           c->png.filter==FILTER_SUB ? "sub" : c->png.filter==FILTER_UP ? "up" : "adaptive");
    if (c->png.encoder==ENCODER_CAIRO) printf("null");
    else if (c->png.compression==-1) printf("\"auto\"");
    else printf("%d",c->png.compression);
    printf("},");
    printf("\"camera\":{\"projection\":\"orthographic\",\"view\":");
    if (c->view_top) printf("\"top\""); else print_vector(c->view);
    printf(",\"up\":"); print_vector(c->up); printf(",\"margin\":%.17g},", c->margin);
    printf("\"geometry\":{\"repeat\":[%zu,%zu,%zu],\"expand_xy\":[%zu,%zu],\"image_colors\":\"%s\",\"sphere_scale\":%.17g,\"bond_radius_A\":%.17g},",
           c->repeat[0], c->repeat[1], c->repeat[2], c->expand_xy[0], c->expand_xy[1],
           c->image_colors == IMAGES_MUTED ? "muted" : c->image_colors == IMAGES_GREYSCALE ? "greyscale" : "normal",
           c->sphere_scale, c->bond_radius);
    printf("\"overlays\":{\"cell\":%s,\"cell_width\":%.17g,\"labels\":\"%s\",\"axes\":%s,\"legend\":%s,\"scale_bar\":%s},",
           c->cell ? "true" : "false",c->cell_width, c->labels == 0 ? "none" : c->labels == 1 ? "element" : "index",
           c->axes ? "true" : "false", c->legend ? "true" : "false", c->scale_bar ? "true" : "false");
    printf("\"threads\":%zu,\"elements\":{", c->threads);
    bool comma = false;
    for (unsigned z = 1; z <= 118; ++z) {
        if (!c->element_radius[z] && !c->element_color[z][0]) continue;
        if (comma) putchar(',');
        printf("\"%s\":{", element_symbol(z));
        if (c->element_radius[z]) printf("\"radius_A\":%.17g", c->element_radius[z]);
        if (c->element_color[z][0]) printf("%s\"color\":\"%s\"", c->element_radius[z] ? "," : "", c->element_color[z]);
        putchar('}'); comma = true;
    }
    printf("},\"bond_cutoffs_A\":{"); comma = false;
    for (unsigned a = 1; a <= 118; ++a) for (unsigned b = a; b <= 118; ++b) {
        if (!c->cutoffs[a][b]) continue;
        if (comma) putchar(',');
        printf("\"%s-%s\":%.17g", element_symbol(a), element_symbol(b), c->cutoffs[a][b]); comma = true;
    }
    printf("}}}\n");
    return fflush(stdout) == 0 && !ferror(stdout);
}

static void help(void)
{
    puts("vasp-render " VR_VERSION "\n"
         "Usage: vasp-render INPUT -o OUTPUT.png [options]\n"
         "  --inspect                   Normalized input JSON, without scene building\n"
         "  --inspect-scene             Wrapped instances, clipped bonds and camera JSON\n"
         "  --format auto|poscar|contcar|outcar|xml\n"
         "  --frame final|N             Zero-based ionic geometry index\n"
         "  --recover                   Accept unfinished OUTCAR with complete geometry\n"
         "  --species Si,O              Explicit ordered species for unlabeled input\n"
         "  --config FILE               Strict JSON config; CLI overrides it\n"
         "  --width N --height N --repeat NX,NY,NZ\n"
         "  --expand-xy NX,NY           Neighbor cells on each side along a/b (0..511)\n"
         "  --image-colors normal|muted|greyscale  Color copies outside primary cell\n"
         "  --view top|X,Y,Z --up X,Y,Z --background '#RRGGBB[AA]'\n"
         "  --style shaded|illustrated  Smooth lighting or layered flat-color atoms\n"
         "  --aa native|2x|4x|adaptive Sample grid; default 2x, 4x quality reference\n"
         "  --backend zbuffer|cairo    Cairo requires an experimental build\n"
         "  --png-encoder libpng|libdeflate|cairo\n"
         "  --png-compression auto|0..9 --png-filter adaptive|none|sub|up\n"
         "  --outline-width PIXELS      Illustrated atom/bond contours; 0 disables\n"
         "  --cell-width PIXELS         Unit-cell line width (default 2, maximum 32)\n"
         "  --sphere-scale F --bond-radius ANGSTROM\n"
         "  --cell on|off --labels none|element|index\n"
         "  --axes on|off --legend on|off --scale-bar on|off\n"
         "  --threads auto|N --benchmark-stages\n"
         "  --help --version\n"
         "Annotations use the accurate Z-buffer. Use --build-info for compiled capabilities.");
}

static bool inspect_scene(const Config *c, const Scene *s, const Camera *camera)
{
    printf("{\"preset\":\"atom-architect-9e73e510\",\"instance_count\":%zu,\"bond_count\":%zu,\"base_bond_count\":%zu,\"camera\":{\"scale\":%.17g,\"right\":",
           s->atom_count,s->bond_count,s->base_bond_count,camera->scale);
    print_vector(camera->right); printf(",\"up\":");print_vector(camera->up);
    printf(",\"view\":");print_vector(camera->view);
    printf(",\"center\":[%.17g,%.17g],\"width\":%zu,\"height\":%zu",
           camera->center_x,camera->center_y,camera->width,camera->height);
    if (c->labels || c->axes || c->legend || c->scale_bar) {
        printf(",\"viewport_px\":{\"origin\":\"lower-left\",\"x\":%zu,\"y\":%zu,\"width\":%zu,\"height\":%zu}",
               camera->viewport_x,camera->viewport_y,camera->viewport_width,camera->viewport_height);
        if (c->legend) printf(",\"legend\":{\"columns\":%zu,\"rows\":%zu,\"width\":%zu,\"height\":%zu}",
            camera->legend_columns,camera->legend_rows,camera->legend_width,camera->legend_height);
        if (c->scale_bar) printf(",\"scale_bar\":{\"angstrom\":%.17g,\"pixels\":%.17g,\"label\":\"%s\",\"x\":12,\"y\":%zu}",
            camera->scale_bar_A,camera->scale_bar_pixels,camera->scale_bar_label,camera->height-20);
    }
    printf("},\"primitives\":[");
    for (size_t i=0;i<s->count;++i) {
        const Primitive *p=&s->items[i];
        if (i) putchar(',');
        printf("{\"kind\":\"%s\",\"a\":",p->kind==SPHERE ? "sphere" : p->kind==CYLINDER ? "bond" : "cell");
        print_vector(p->a);printf(",\"b\":");print_vector(p->b);
        printf(",\"radius\":%.17g,\"rgba\":[%u,%u,%u,%u],\"source\":[%zu,%zu],\"image\":[%d,%d,%d],\"origin\":[%d,%d,%d],\"distance\":%.17g",
               p->kind==CELL_EDGE ? 0.5*c->cell_width/camera->scale : p->radius,(unsigned)p->color[0],(unsigned)p->color[1],(unsigned)p->color[2],(unsigned)p->color[3],
               p->source_a,p->source_b,p->image[0],p->image[1],p->image[2],p->origin[0],p->origin[1],p->origin[2],p->distance);
        if (p->kind==SPHERE) printf(",\"element\":\"%s\",\"instance_index\":%zu,\"primary_cell\":%s",element_symbol(p->element),i,
                                  !p->origin[0] && !p->origin[1] && !p->origin[2] ? "true" : "false");
        printf("}");
    }
    puts("]}"); return fflush(stdout)==0 && !ferror(stdout);
}

static double elapsed_ms(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec-a.tv_sec)*1000+(double)(b.tv_nsec-a.tv_nsec)/1e6;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) { help(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("vasp-render " VR_VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1],"--build-info")) {
        printf("{\"version\":\"%s\",\"xml\":%s,\"openmp\":%s,\"cairo\":%s,\"libdeflate\":%s}\n",VR_VERSION,
#ifdef VR_HAVE_XML
               "true",
#else
               "false",
#endif
#ifdef _OPENMP
               "true",
#else
               "false",
#endif
#ifdef VR_HAVE_CAIRO
               "true",
#else
               "false",
#endif
#ifdef VR_HAVE_LIBDEFLATE
               "true"
#else
               "false"
#endif
        );return 0;
    }
    Error e = {0}; Config c; Structure s = {0}; Scene scene={0}; Image image={0}; Camera camera={0};
    struct timespec begin, configured, parsed, built, rendered, written;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    if (!config_parse(argc, argv, &c, &e)) goto failure;
    clock_gettime(CLOCK_MONOTONIC, &configured);
    if (!parse_structure(&c, &s, &e)) goto failure;
    clock_gettime(CLOCK_MONOTONIC, &parsed);
    if (s.recovered) fprintf(stderr, "%s: warning: recovered complete ionic frame %zu from unfinished OUTCAR\n", c.input, s.frame);
    built=rendered=written=parsed;
    if (!c.inspect || c.inspect_scene) {
        if (!scene_build(&c,&s,&scene,&e) || !camera_fit(&c,&scene,&camera,&e)) goto failure;
        clock_gettime(CLOCK_MONOTONIC,&built); rendered=written=built;
    }
    if (c.inspect) {
        if (!(c.inspect_scene ? inspect_scene(&c,&scene,&camera) : inspect(&c,&s))) {
            fail(&e,7,"failed to write inspection JSON to stdout");goto failure;
        }
    } else {
        if (!render_image(&c,&scene,&camera,s.count*sizeof(Atom),&image,&e)) goto failure;
        clock_gettime(CLOCK_MONOTONIC,&rendered);
        PngOptions png=png_resolve_options(c.output,c.png);
        if (c.benchmark) {
            if (png.encoder==ENCODER_CAIRO) fprintf(stderr,"png_encoder_internal=1\n");
            else fprintf(stderr,"png_compression_resolved=%d\n",png.compression);
        }
        if (!png_write_options_atomic(c.output,&image,&png,
            scene.capacity*sizeof(Primitive)+s.count*sizeof(Atom),&e)) goto failure;
        clock_gettime(CLOCK_MONOTONIC,&written);
    }
    if (c.benchmark) {
        fprintf(stderr,"stages_ms: config=%.3f parse=%.3f scene=%.3f render=%.3f png=%.3f (excludes process startup and JSON output)\n",
                elapsed_ms(begin,configured),elapsed_ms(configured,parsed),elapsed_ms(parsed,built),elapsed_ms(built,rendered),elapsed_ms(rendered,written));
    }
    image_free(&image);scene_free(&scene);structure_free(&s); return 0;
failure:
    image_free(&image);scene_free(&scene);structure_free(&s);
    fprintf(stderr, "vasp-render: %s\n", e.message);
    return e.code ? e.code : 8;
}
