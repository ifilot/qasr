/* SPDX-License-Identifier: GPL-3.0-only */
#include "config.h"
#include "cJSON.h"
#include <stdlib.h>
#include <string.h>

static bool put(cJSON *object, const char *key, cJSON *item)
{
    if (!item) return false;
    bool ok=cJSON_GetObjectItemCaseSensitive(object,key) ?
        cJSON_ReplaceItemInObjectCaseSensitive(object,key,item) : cJSON_AddItemToObject(object,key,item);
    if (!ok) cJSON_Delete(item);
    return ok;
}
static cJSON *vector(const double *values, size_t n)
{
    cJSON *a=cJSON_CreateArray();
    for (size_t i=0;i<n;++i) if (!cJSON_AddItemToArray(a,cJSON_CreateNumber(values[i]))) { cJSON_Delete(a);return NULL; }
    return a;
}
static cJSON *sizes(const size_t *values, size_t n)
{
    double v[3];for (size_t i=0;i<n;++i) v[i]=(double)values[i];return vector(v,n);
}

cJSON *config_settings_json(const Config *c)
{
    cJSON *root=cJSON_CreateObject(),*image=cJSON_CreateObject(),*camera=cJSON_CreateObject(),
          *geometry=cJSON_CreateObject(),*overlays=cJSON_CreateObject(),*elements=cJSON_CreateObject(),*cutoffs=cJSON_CreateObject();
    bool ok=root && image && camera && geometry && overlays && elements && cutoffs;
    ok &= put(image,"width",cJSON_CreateNumber((double)c->width));
    ok &= put(image,"height",cJSON_CreateNumber((double)c->height));
    ok &= put(image,"background",cJSON_CreateString(c->background));
    ok &= put(image,"style",cJSON_CreateString(c->style==STYLE_ORTEP ? "ortep" : c->style==STYLE_BALSAC ? "balsac" : c->style==STYLE_ILLUSTRATED ? "illustrated" : "shaded"));
    ok &= put(image,"tone",cJSON_CreateString(c->tone==TONE_MONOCOLOR ? "monocolor" : c->tone==TONE_DUALTONE ? "dualtone" : "color"));
    ok &= put(image,"aa",cJSON_CreateString(c->adaptive_aa ? "adaptive" : c->aa_scale==1 ? "native" : c->aa_scale==4 ? "4x" : "2x"));
    ok &= put(image,"backend",cJSON_CreateString(c->backend==BACKEND_CAIRO ? "cairo" : "zbuffer"));
    ok &= put(image,"depth_fade",cJSON_CreateNumber(c->depth_fade));
    ok &= put(image,"outline_width",cJSON_CreateNumber(c->outline_width));
    ok &= put(image,"png_encoder",cJSON_CreateString(c->png.encoder==ENCODER_CAIRO ? "cairo" : c->png.encoder==ENCODER_LIBDEFLATE ? "libdeflate" : "libpng"));
    ok &= put(image,"png_filter",cJSON_CreateString(c->png.filter==FILTER_NONE ? "none" : c->png.filter==FILTER_SUB ? "sub" : c->png.filter==FILTER_UP ? "up" : "adaptive"));
    ok &= put(image,"png_compression",c->png.compression<0 ? cJSON_CreateString("auto") : cJSON_CreateNumber(c->png.compression));
    ok &= put(camera,"projection",cJSON_CreateString("orthographic"));
    ok &= put(camera,"view",c->view_top ? cJSON_CreateString("top") : vector(c->view.x,3));
    /* An implicit up must stay implicit when a dumped recipe is reloaded. */
    if (c->up_explicit) ok &= put(camera,"up",vector(c->up.x,3));
    ok &= put(camera,"margin",cJSON_CreateNumber(c->margin));
    ok &= put(geometry,"repeat",sizes(c->requested_repeat[0] ? c->requested_repeat : c->repeat,3));
    ok &= put(geometry,"expand_xy",sizes(c->expand_xy,2));
    ok &= put(geometry,"image_colors",cJSON_CreateString(c->image_colors==IMAGES_MUTED ? "muted" : c->image_colors==IMAGES_GREYSCALE ? "greyscale" : "normal"));
    ok &= put(geometry,"sphere_scale",cJSON_CreateNumber(c->sphere_scale));
    ok &= put(geometry,"bond_radius_A",cJSON_CreateNumber(c->bond_radius));
    ok &= put(overlays,"cell",cJSON_CreateBool(c->cell));
    ok &= put(overlays,"cell_width",cJSON_CreateNumber(c->cell_width));
    ok &= put(overlays,"labels",cJSON_CreateString(c->labels==1 ? "element" : c->labels==2 ? "index" : "none"));
    ok &= put(overlays,"axes",cJSON_CreateBool(c->axes));
    ok &= put(overlays,"legend",cJSON_CreateBool(c->legend));
    ok &= put(overlays,"scale_bar",cJSON_CreateBool(c->scale_bar));
    for (unsigned z=1;z<=118;++z) if (c->element_radius[z] || c->element_color[z][0]) {
        cJSON *entry=cJSON_CreateObject();
        if (c->element_radius[z]) ok &= put(entry,"radius_A",cJSON_CreateNumber(c->element_radius[z]));
        if (c->element_color[z][0]) ok &= put(entry,"color",cJSON_CreateString(c->element_color[z]));
        ok &= put(elements,element_symbol(z),entry);
    }
    for (unsigned a=1;a<=118;++a) for (unsigned b=a;b<=118;++b) if (c->cutoffs[a][b]) {
        char key[8];snprintf(key,sizeof key,"%s-%s",element_symbol(a),element_symbol(b));
        ok &= put(cutoffs,key,cJSON_CreateNumber(c->cutoffs[a][b]));
    }
    if (c->species_count) {
        cJSON *species=cJSON_CreateArray();
        for (size_t i=0;i<c->species_count;++i) ok &= cJSON_AddItemToArray(species,cJSON_CreateString(element_symbol(c->species[i])));
        ok &= put(root,"species",species);
    }
    ok &= put(root,"schema_version",cJSON_CreateNumber(1));
    ok &= put(root,"image",image);ok &= put(root,"camera",camera);ok &= put(root,"geometry",geometry);
    ok &= put(root,"overlays",overlays);ok &= put(root,"elements",elements);ok &= put(root,"bond_cutoffs_A",cutoffs);
    if (c->rule_json) ok &= put(root,"atom_overrides",cJSON_Duplicate(c->rule_json,1));
    if (!ok) {cJSON_Delete(root);return NULL;}return root;
}

static bool origins(const cJSON *node, const char *prefix, cJSON *out, const cJSON *explicit)
{
    for (const cJSON *j=node->child;j;j=j->next) {
        char path[256];int n=snprintf(path,sizeof path,"%s%s%s",prefix,*prefix ? "." : "",j->string);
        if (n<0 || (size_t)n>=sizeof path) return false;
        if (cJSON_IsObject(j)) {if (!origins(j,path,out,explicit)) return false;}
        else {
            const cJSON *source=cJSON_GetObjectItemCaseSensitive(explicit,path);
            cJSON *entry=source ? cJSON_Duplicate(source,1) : cJSON_CreateObject();
            if (!source && (!put(entry,"layer",cJSON_CreateString("built-in")) || !put(entry,"file",cJSON_CreateNull()))) {cJSON_Delete(entry);return false;}
            if (!put(out,path,entry)) return false;
        }
    }
    return true;
}

cJSON *config_dump_json(const Config *c)
{
    cJSON *root=cJSON_CreateObject(),*settings=config_settings_json(c),*runtime=cJSON_CreateObject(),
          *provenance=cJSON_CreateObject(),*user=cJSON_CreateObject(),*derived=cJSON_CreateObject();
    bool ok=root && settings && runtime && provenance && user && derived;
    if (settings) ok &= origins(settings,"",provenance,c->provenance);
    for (const cJSON *j=c->provenance ? c->provenance->child : NULL;j;j=j->next)
        if (!strncmp(j->string,"runtime.",8)) ok &= put(provenance,j->string,cJSON_Duplicate(j,1));
    ok &= put(runtime,"input",c->input ? cJSON_CreateString(c->input) : cJSON_CreateNull());
    ok &= put(runtime,"output",c->output ? cJSON_CreateString(c->output) : cJSON_CreateNull());
    ok &= put(runtime,"report",c->report ? cJSON_CreateString(c->report) : cJSON_CreateNull());
    ok &= put(runtime,"frame",c->final_frame ? cJSON_CreateString("final") : cJSON_CreateNumber((double)c->frame));
    ok &= put(runtime,"format",cJSON_CreateString(format_name(c->format)));
    ok &= put(runtime,"threads",c->threads ? cJSON_CreateNumber((double)c->threads) : cJSON_CreateString("auto"));
    ok &= put(runtime,"recover",cJSON_CreateBool(c->recover));
    ok &= put(user,"path",c->user_path ? cJSON_CreateString(c->user_path) : cJSON_CreateNull());
    ok &= put(user,"loaded",cJSON_CreateBool(c->user_loaded));
    ok &= put(user,"disabled",cJSON_CreateBool(c->no_user_config));
    ok &= put(derived,"repeat",sizes(c->repeat,3));
    ok &= put(derived,"up",vector(c->up.x,3));
    ok &= put(root,"schema_version",cJSON_CreateNumber(1));
    ok &= put(root,"config",settings);ok &= put(root,"runtime",runtime);ok &= put(root,"provenance",provenance);
    ok &= put(root,"user_config",user);ok &= put(root,"derived",derived);
    ok &= put(root,"sources",c->sources ? cJSON_Duplicate(c->sources,1) : cJSON_CreateArray());
    if (!ok) {cJSON_Delete(root);return NULL;}return root;
}

static size_t json_bytes(const cJSON *j)
{
    size_t n=0;
    for (;j;j=j->next) n+=sizeof *j+(j->string ? strlen(j->string)+1 : 0)+(j->valuestring ? strlen(j->valuestring)+1 : 0)+json_bytes(j->child);
    return n;
}
size_t config_memory_bytes(const Config *c)
{
    size_t n=json_bytes(c->provenance)+json_bytes(c->sources)+json_bytes(c->rule_json)+json_bytes(c->job_json)+
             (c->user_path ? strlen(c->user_path)+1 : 0)+c->rule_count*sizeof(AtomRule);
    for (size_t i=0;i<c->rule_count;++i) n+=c->rules[i].index_count*sizeof(size_t);
    return n;
}
void config_free(Config *c)
{
    for (size_t i=0;i<c->rule_count;++i) free(c->rules[i].indices);
    free(c->rules);free(c->user_path);cJSON_Delete(c->provenance);cJSON_Delete(c->sources);cJSON_Delete(c->rule_json);cJSON_Delete(c->job_json);
    memset(c,0,sizeof *c);
}
