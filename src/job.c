/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "job.h"
#include "automation.h"
#include "sha256.h"
#include "parse.h"
#include "render.h"
#include "image_io.h"
#include "movie.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double milliseconds(struct timespec a, struct timespec b)
{
    return (double)(b.tv_sec-a.tv_sec)*1000+(double)(b.tv_nsec-a.tv_nsec)/1e6;
}
void job_cache_clear(JobCache *c)
{
    structure_free(&c->source);scene_free(&c->scene);c->source_key[0]=c->scene_key[0]='\0';
}
static size_t retained(const JobCache *c) {return c->source.count*sizeof(Atom)+scene_memory_bytes(&c->scene);}
cJSON *job_cache_info(const JobCache *c)
{
    cJSON *j=cJSON_CreateObject();
    if (!j) return NULL;
    if (!cJSON_AddNumberToObject(j,"limit_bytes",(double)c->limit) ||
        !cJSON_AddNumberToObject(j,"retained_bytes",(double)retained(c)) ||
        !cJSON_AddNumberToObject(j,"source_hits",(double)c->source_hits) ||
        !cJSON_AddNumberToObject(j,"scene_hits",(double)c->scene_hits) ||
        !cJSON_AddNumberToObject(j,"source_misses",(double)c->misses)) {cJSON_Delete(j);return NULL;}
    return j;
}
static bool hash_json(cJSON *j, char out[65], Error *e)
{
    char *text=j ? cJSON_PrintUnformatted(j) : NULL;cJSON_Delete(j);
    if (!text) return fail(e,6,"cannot allocate cache key");
    sha256_data(text,strlen(text),out);free(text);return true;
}
static bool source_key(const Config *c, const char *input_hash, char key[65], Error *e)
{
    cJSON *j=cJSON_CreateObject(),*species=cJSON_CreateArray();
    if (!j || !species) {cJSON_Delete(j);cJSON_Delete(species);return fail(e,6,"cannot allocate input cache key");}
    bool ok=cJSON_AddStringToObject(j,"content",input_hash) && cJSON_AddNumberToObject(j,"format",c->format) &&
        cJSON_AddBoolToObject(j,"final",c->final_frame) && cJSON_AddNumberToObject(j,"frame",(double)c->frame) &&
        cJSON_AddBoolToObject(j,"recover",c->recover);
    for (size_t i=0;ok && i<c->species_count;++i) {
        cJSON *value=cJSON_CreateNumber(c->species[i]);
        if (!value || !cJSON_AddItemToArray(species,value)) {cJSON_Delete(value);ok=false;}
    }
    if (!ok || !cJSON_AddItemToObject(j,"species",species)) {
        cJSON_Delete(species);cJSON_Delete(j);return fail(e,6,"cannot allocate complete input key");
    }
    return hash_json(j,key,e);
}
static bool scene_key(const Config *c, const char *source, char key[65], Error *e)
{
    cJSON *settings=config_settings_json(c),*j=cJSON_CreateObject();
    if (!settings || !j) {cJSON_Delete(settings);cJSON_Delete(j);return fail(e,6,"cannot allocate scene cache key");}
    bool ok=cJSON_AddStringToObject(j,"source",source)!=NULL;
    const char *groups[]={"geometry","elements","bond_cutoffs_A","atom_overrides","faces"};
    for (size_t i=0;i<5;++i) {
        cJSON *v=cJSON_DetachItemFromObjectCaseSensitive(settings,groups[i]);
        if (v && !cJSON_AddItemToObject(j,groups[i],v)) {cJSON_Delete(v);ok=false;}
    }
    ok=ok && cJSON_AddBoolToObject(j,"cell",c->cell) && cJSON_AddStringToObject(j,"cell_color",c->cell_color);cJSON_Delete(settings);
    if (!ok) {cJSON_Delete(j);return fail(e,6,"cannot allocate complete scene key");}
    return hash_json(j,key,e);
}

cJSON *job_render(const Config *c, JobCache *cache, bool report, const char *version, double config_ms, Error *e)
{
    if (c->movie.mode) {
        if (cache) job_cache_clear(cache);
        return movie_render(c,version,e);
    }
    Structure local_source={0};Scene local_scene={0};Image image={0};Camera camera={0};
    Structure *s=&local_source;Scene *scene=&local_scene;cJSON *result=NULL;
    struct timespec begin,parsed,built,rendered,written;clock_gettime(CLOCK_MONOTONIC,&begin);
    char input_hash[65]={0},sk[65]={0},gk[65]={0};bool source_hit=false,scene_hit=false;
    bool caching=cache && cache->limit;
    if (!image_output_validate(c->output,e) || !report_preflight(c,e) || !check_cancel(e)) goto done;
    if ((report || caching) && !sha256_file(c->input,input_hash,e)) goto done;
    if (caching && !source_key(c,input_hash,sk,e)) goto done;
    if (caching && !strcmp(cache->source_key,sk)) {s=&cache->source;source_hit=true;++cache->source_hits;}
    else {
        if (cache) {job_cache_clear(cache);++cache->misses;}
        if (!parse_structure(c,&local_source,e)) goto done;
        if (caching) {
            char after[65];if (!sha256_file(c->input,after,e)) goto done;
            if (strcmp(after,input_hash)) {fail_at(e,7,"input_changed","input","input changed during parsing");goto done;}
            if (local_source.count*sizeof(Atom)<=cache->limit) {
                cache->source=local_source;memset(&local_source,0,sizeof local_source);s=&cache->source;strcpy(cache->source_key,sk);
            }
        }
    }
    clock_gettime(CLOCK_MONOTONIC,&parsed);
    if (s->source.recovered) fprintf(stderr,"%s: warning: recovered complete ionic frame %zu from unfinished OUTCAR\n",c->input,s->source.frame);
    if (caching && !scene_key(c,sk,gk,e)) goto done;
    if (caching && source_hit && !strcmp(cache->scene_key,gk)) {scene=&cache->scene;scene_hit=true;++cache->scene_hits;}
    else {
        if (cache) {scene_free(&cache->scene);cache->scene_key[0]='\0';}
        if (!scene_build(c,s,&local_scene,e)) goto done;
        if (caching && s==&cache->source && scene_memory_bytes(&local_scene)<=cache->limit-retained(cache)) {
            cache->scene=local_scene;memset(&local_scene,0,sizeof local_scene);scene=&cache->scene;strcpy(cache->scene_key,gk);
        }
    }
    if (!check_cancel(e) || !camera_fit(c,scene,&camera,e)) goto done;
    clock_gettime(CLOCK_MONOTONIC,&built);
    if (!render_image(c,scene,&camera,s->count*sizeof(Atom)+config_memory_bytes(c),&image,e) || !check_cancel(e)) goto done;
    clock_gettime(CLOCK_MONOTONIC,&rendered);
    PngOptions png=png_resolve_options(c->output,c->png);
    if (c->benchmark && !strcmp(image_format_name(c->output),"png")) {
        if (png.encoder==ENCODER_CAIRO) fprintf(stderr,"png_encoder_internal=1\n");
        else fprintf(stderr,"png_compression_resolved=%d\n",png.compression);
    }
    if (!image_write_atomic(c->output,&image,&png,scene_memory_bytes(scene)+s->count*sizeof(Atom)+config_memory_bytes(c),e)) goto done;
    clock_gettime(CLOCK_MONOTONIC,&written);
    double stages[5]={config_ms,milliseconds(begin,parsed),milliseconds(parsed,built),milliseconds(built,rendered),milliseconds(rendered,written)};
    if (c->benchmark) fprintf(stderr,"stages_ms: config=%.3f parse=%.3f scene=%.3f render=%.3f %s=%.3f (excludes process startup and JSON output)\n",stages[0],stages[1],stages[2],stages[3],!strcmp(image_format_name(c->output),"png") ? "png" : "encode",stages[4]);
    if (report) result=render_report(c,s,scene,&camera,&png,version,input_hash,stages,e);
    else result=cJSON_CreateObject();
    if (!result) {if (!e->code) fail(e,6,"cannot allocate job result");goto done;}
    if (report && !cJSON_AddNumberToObject(result,"render_threads",(double)(image.threads_used ? image.threads_used : 1))) {cJSON_Delete(result);result=NULL;fail(e,6,"cannot allocate thread report");goto done;}
    if (cache) {
        cJSON *info=job_cache_info(cache);
        if (!info) {cJSON_Delete(result);result=NULL;fail(e,6,"cannot allocate cache report");goto done;}
        if (!cJSON_AddBoolToObject(info,"source_hit",source_hit) || !cJSON_AddBoolToObject(info,"scene_hit",scene_hit) || !cJSON_AddItemToObject(result,"cache",info)) {
            cJSON_Delete(info);cJSON_Delete(result);result=NULL;fail(e,6,"cannot allocate cache result");goto done;
        }
    }
done:
    image_free(&image);scene_free(&local_scene);structure_free(&local_source);return result;
}
