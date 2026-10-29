/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "parse.h"
#include "render.h"
#include "version.h"
#include "automation.h"
#include "job.h"
#include "schema.h"
#include <stdlib.h>
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
    cJSON *settings=config_settings_json(c);
    if (!settings) return false;
    cJSON_AddNumberToObject(settings,"threads",(double)c->threads);
    double repeats[3]={(double)c->repeat[0],(double)c->repeat[1],(double)c->repeat[2]};
    cJSON_ReplaceItemInObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(settings,"geometry"),"repeat",cJSON_CreateDoubleArray(repeats,3));
    if (!c->up_explicit) cJSON_AddItemToObject(cJSON_GetObjectItemCaseSensitive(settings,"camera"),"up",cJSON_CreateDoubleArray(c->up.x,3));
    if (c->png.encoder==ENCODER_CAIRO) {
        cJSON *image=cJSON_GetObjectItemCaseSensitive(settings,"image");
        cJSON_ReplaceItemInObjectCaseSensitive(image,"png_filter",cJSON_CreateString("internal"));
        cJSON_ReplaceItemInObjectCaseSensitive(image,"png_compression",cJSON_CreateNull());
    }
    char *text=cJSON_PrintUnformatted(settings);cJSON_Delete(settings);
    if (!text) return false;
    printf("],\"settings\":%s}\n",text);free(text);
    return fflush(stdout) == 0 && !ferror(stdout);
}

static void help(void)
{
    puts("QASR: Quick Atomic Structure Renderer\n"
         "qasr " VR_VERSION "\n"
         "Usage: qasr INPUT -o OUTPUT.png [options]\n"
         "  --inspect                   Normalized input JSON, without scene building\n"
         "  --inspect-scene             Wrapped instances, clipped bonds and camera JSON\n"
         "  --format auto|poscar|contcar|outcar|xml\n"
         "  --frame final|N             Zero-based ionic geometry index\n"
         "  --recover                   Accept unfinished OUTCAR with complete geometry\n"
         "  --species Si,O              Explicit ordered species for unlabeled input\n"
         "  --batch [--cache-mib N] [--max-jobs N]  Sequential JSONL worker\n"
         "  --config FILE|-             Strict job settings; CLI overrides them\n"
         "  --job FILE|-                Invocation JSON with embedded config\n"
         "  --no-user-config            Ignore automatic XDG personal defaults\n"
         "  --init-config               Create personal starter JSON, never overwrite\n"
         "  --dump-config               Resolved settings, runtime and provenance JSON\n"
         "  --validate-config           Validate settings/selectors without rendering\n"
         "  --json-errors               Structured errors on stderr\n"
         "  --report FILE|-             Compact successful render manifest\n"
         "  --schema [config|user|job|worker]   Print JSON Schema; run alone\n"
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
        if (p->kind==SPHERE && s->style_rules) {
            RuleStamp rules=s->style_rules[i];
            printf(",\"applied_rules\":{\"color\":");
            if (rules.color==SIZE_MAX) printf("null");else printf("%zu",rules.color);
            printf(",\"radius\":");
            if (rules.radius==SIZE_MAX) printf("null");else printf("%zu",rules.radius);
            putchar('}');
        }
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
    if (argc>=2 && !strcmp(argv[1],"--batch")) return worker_run(argc,argv,VR_VERSION);
    if ((argc==2 || argc==3) && !strcmp(argv[1],"--schema")) {
        const unsigned char *schema=config_schema;
        if (argc==3 && !strcmp(argv[2],"job")) schema=job_schema;
        else if (argc==3 && !strcmp(argv[2],"worker")) schema=worker_schema;
        else if (argc==3 && !strcmp(argv[2],"user")) schema=user_schema;
        else if (argc==3 && strcmp(argv[2],"config")) {fputs("unknown schema; use config, user, job or worker\n",stderr);return 2;}
        return fputs((const char *)schema,stdout)==EOF || fflush(stdout) ? 7 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--help")) { help(); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--version")) { puts("qasr " VR_VERSION); return 0; }
    if (argc == 2 && !strcmp(argv[1],"--build-info")) {
        printf("{\"version\":\"%s\",\"config_schema_version\":1,\"user_config\":true,\"atom_overrides\":true,\"job_requests\":true,\"batch_worker\":true,\"xml\":%s,\"openmp\":%s,\"cairo\":%s,\"libdeflate\":%s}\n",VR_VERSION,
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
    Error e = {0}; Config c={0}; Structure s = {0}; Scene scene={0}; Image image={0}; Camera camera={0};
    struct timespec begin, configured, parsed, built, rendered, written;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    if (!config_parse(argc, argv, &c, &e)) goto failure;
    clock_gettime(CLOCK_MONOTONIC, &configured);
    if (c.init_config) {
        if (!config_initialize_file(&c,&e)) goto failure;
        config_free(&c);return 0;
    }
    if (c.dump_config) {
        cJSON *dump=config_dump_json(&c);bool ok=json_write("-",dump,false,&e);cJSON_Delete(dump);
        if (!ok) goto failure;
        config_free(&c);return 0;
    }
    if (!c.inspect && !c.validate_config) {
        cJSON *result=job_render(&c,NULL,c.report!=NULL,VR_VERSION,elapsed_ms(begin,configured),&e);
        if (!result) goto failure;
        bool ok=!c.report || json_write(c.report,result,false,&e);cJSON_Delete(result);
        if (!ok) goto failure;
        config_free(&c);return 0;
    }

    if (c.input && !parse_structure(&c, &s, &e)) goto failure;
    if (c.validate_config) {
        AtomStyle *styles=NULL;
        if (c.input && !atom_styles_resolve(&c,&s,&styles,&e)) goto failure;
        free(styles);
        cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",1);cJSON_AddNumberToObject(result,"schema_version",1);
        cJSON_AddBoolToObject(result,"input_validated",c.input!=NULL);cJSON_AddBoolToObject(result,"selectors_validated",c.input!=NULL);
        cJSON_AddNumberToObject(result,"atom_rules",(double)c.rule_count);
        bool ok=json_write("-",result,false,&e);cJSON_Delete(result);if (!ok) goto failure;
        structure_free(&s);config_free(&c);return 0;
    }
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
    }
    if (c.benchmark) {
        fprintf(stderr,"stages_ms: config=%.3f parse=%.3f scene=%.3f render=%.3f png=%.3f (excludes process startup and JSON output)\n",
                elapsed_ms(begin,configured),elapsed_ms(configured,parsed),elapsed_ms(parsed,built),elapsed_ms(built,rendered),elapsed_ms(rendered,written));
    }
    image_free(&image);scene_free(&scene);structure_free(&s);config_free(&c); return 0;
failure:
    image_free(&image);scene_free(&scene);structure_free(&s);
    if (c.json_errors) print_json_error(&e);else fprintf(stderr, "qasr: %s\n", e.message);
    config_free(&c);return e.code ? e.code : 8;
}
