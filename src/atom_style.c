/* SPDX-License-Identifier: GPL-3.0-only */
#include "atom_style.h"
#include "elements.h"
#include "cJSON.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool bad(Error *e, size_t rule, const char *field, const char *name, const char *message)
{
    char path[256];snprintf(path,sizeof path,"atom_overrides[%zu]%s%s",rule,*field ? "." : "",field);
    return fail_at(e,2,name,path,"%s: %s",path,message);
}
static bool bounded(const cJSON *j, size_t max, size_t *out)
{
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || j->valuedouble<0 || j->valuedouble>(double)max || floor(j->valuedouble)!=j->valuedouble) return false;
    *out=(size_t)j->valuedouble;return true;
}
static int ascending(const void *left, const void *right)
{
    size_t a=*(const size_t *)left,b=*(const size_t *)right;return (a>b)-(a<b);
}
static void clear_rules(Config *c)
{
    for (size_t i=0;i<c->rule_count;++i) free(c->rules[i].indices);
    free(c->rules);c->rules=NULL;c->rule_count=c->cell_rule_count=0;c->first_cell=SIZE_MAX;
    cJSON_Delete(c->rule_json);c->rule_json=NULL;
}

bool config_atom_rules(Config *c, const cJSON *json, Error *e)
{
    if (!cJSON_IsArray(json)) return fail_at(e,2,"invalid_atom_rules","atom_overrides","atom_overrides must be an array");
    size_t n=(size_t)cJSON_GetArraySize(json);
    if (n>VR_MAX_ATOM_RULES) return fail_at(e,6,"atom_rule_limit","atom_overrides","at most %u atom rules are supported",VR_MAX_ATOM_RULES);
    clear_rules(c);
    c->rule_json=cJSON_Duplicate(json,1);
    if (!c->rule_json) return fail(e,6,"cannot allocate atom rules");
    if (!n) return true;
    c->rules=calloc(n,sizeof *c->rules);
    if (!c->rules) return fail(e,6,"cannot allocate atom rules");
    c->rule_count=n;
    size_t i=0,last_cell=SIZE_MAX;
    for (const cJSON *entry=json->child;entry;entry=entry->next,++i) {
        AtomRule *r=&c->rules[i];r->next_cell=SIZE_MAX;
        if (!cJSON_IsObject(entry)) return bad(e,i,"","invalid_atom_rule","expected an object");
        const cJSON *select=NULL;
        for (const cJSON *field=entry->child;field;field=field->next) {
            if (!strcmp(field->string,"select")) select=field;
            else if (!strcmp(field->string,"radius_A")) {
                if (!cJSON_IsNumber(field) || !isfinite(field->valuedouble) || field->valuedouble<1e-9 || field->valuedouble>1e6)
                    return bad(e,i,"radius_A","invalid_atom_radius","radius_A must be within [1e-9,1e6] angstroms");
                r->radius=field->valuedouble;r->has_radius=true;
            } else if (!strcmp(field->string,"color")) {
                if (!cJSON_IsString(field)) return bad(e,i,"color","invalid_atom_color","expected #RRGGBB[AA]");
                const char *v=field->valuestring;size_t len=strlen(v);
                if ((len!=7 && len!=9) || *v!='#' || strspn(v+1,"0123456789abcdefABCDEF")!=len-1)
                    return bad(e,i,"color","invalid_atom_color","expected #RRGGBB[AA]");
                decode_color(v,r->color);r->has_color=true;
            } else return bad(e,i,field->string,"unknown_atom_rule_field","unknown rule field");
        }
        if (!r->has_radius && !r->has_color) return bad(e,i,"","empty_atom_style","supply radius_A and/or color");
        if (!cJSON_IsObject(select)) return bad(e,i,"select","invalid_atom_selector","expected a selector object");
        for (const cJSON *field=select->child;field;field=field->next) {
            const char *key=field->string;
            if (!strcmp(key,"element")) {
                r->element=cJSON_IsString(field) ? element_id(field->valuestring) : 0;
                if (!r->element) return bad(e,i,"select.element","invalid_atom_element","expected an exact element symbol");
            } else if (!strcmp(key,"source_indices")) {
                if (!cJSON_IsArray(field) || !field->child) return bad(e,i,"select.source_indices","invalid_atom_indices","expected a nonempty index array");
                r->index_count=(size_t)cJSON_GetArraySize(field);
                if (r->index_count>VR_MAX_ATOMS) return bad(e,i,"select.source_indices","invalid_atom_indices","too many source indices");
                r->indices=malloc(r->index_count*sizeof *r->indices);
                if (!r->indices) return fail(e,6,"cannot allocate atom indices");
                size_t at=0;
                for (const cJSON *v=field->child;v;v=v->next,++at) {
                    if (!bounded(v,VR_MAX_ATOMS-1,&r->indices[at])) {
                        char path[96];snprintf(path,sizeof path,"select.source_indices[%zu]",at);
                        return bad(e,i,path,"invalid_atom_index","expected a zero-based integer index below the source atom limit");
                    }
                }
                qsort(r->indices,r->index_count,sizeof *r->indices,ascending);
                for (size_t at2=1;at2<r->index_count;++at2) if (r->indices[at2-1]==r->indices[at2])
                    return bad(e,i,"select.source_indices","duplicate_atom_index","source indices must be unique");
            } else if (!strcmp(key,"source_range")) {
                if (!cJSON_IsArray(field) || cJSON_GetArraySize(field)!=2 ||
                    !bounded(field->child,VR_MAX_ATOMS-1,&r->first) || !bounded(field->child->next,VR_MAX_ATOMS-1,&r->last) || r->first>r->last)
                    return bad(e,i,"select.source_range","invalid_atom_range","expected inclusive [first,last] zero-based bounds");
                r->range=true;
            } else if (!strcmp(key,"cell")) {
                if (!cJSON_IsArray(field) || cJSON_GetArraySize(field)!=3) return bad(e,i,"select.cell","invalid_atom_cell","expected three integer lattice origins");
                size_t at=0;
                for (const cJSON *v=field->child;v;v=v->next,++at) {
                    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < -1024 || v->valuedouble>1024 || floor(v->valuedouble)!=v->valuedouble)
                        return bad(e,i,"select.cell","invalid_atom_cell","cell entries must be integers within [-1024,1024]");
                    r->cell[at]=(int)v->valuedouble;
                }
                r->has_cell=true;++c->cell_rule_count;
                if (last_cell==SIZE_MAX) c->first_cell=i;else c->rules[last_cell].next_cell=i;
                last_cell=i;
            } else {char path[96];snprintf(path,sizeof path,"select.%s",key);return bad(e,i,path,"unknown_atom_selector_field","unknown selector field");}
        }
        if (r->range && r->indices) return bad(e,i,"select","conflicting_atom_selector","use source_indices or source_range, not both");
        if (!r->range && !r->indices && !r->element) return bad(e,i,"select","empty_atom_selector","select an element and/or source indices/range");
    }
    return true;
}

static bool selected(const AtomRule *r, size_t index, unsigned element)
{
    if (r->element && r->element!=element) return false;
    if (r->range) return index>=r->first && index<=r->last;
    if (!r->indices) return true;
    return bsearch(&index,r->indices,r->index_count,sizeof index,ascending)!=NULL;
}
static void apply(const AtomRule *r, size_t id, double scale, AtomStyle *style)
{
    if (r->has_radius) {style->radius=r->radius*scale;style->rules.radius=id;}
    if (r->has_color) {memcpy(style->color,r->color,4);style->rules.color=id;}
}

bool atom_styles_resolve(const Config *c, const Structure *s, AtomStyle **styles, Error *e)
{
    *styles=NULL;if (!c->rule_count) return true;
    size_t instances=s->count;
    for (size_t d=0;d<3;++d) {
        if (instances>VR_MAX_INSTANCES/c->repeat[d]) return fail(e,6,"atom instance limit exceeded");
        instances*=c->repeat[d];
    }
    if (c->cell_rule_count && instances>200000000u/c->cell_rule_count)
        return fail_at(e,6,"atom_rule_work_limit","atom_overrides","cell-specific rule evaluation exceeds work limit");
    if (s->count*sizeof(AtomStyle)+config_memory_bytes(c)>VR_CONFIG_MEMORY_LIMIT)
        return fail_at(e,6,"atom_style_memory_limit","atom_overrides","source atom style table exceeds 64 MiB budget with configuration");
    AtomStyle *table=calloc(s->count,sizeof *table);
    if (!table) return fail(e,6,"cannot allocate source atom styles");
    for (size_t i=0;i<s->count;++i) {
        table[i].radius=element_radius(c,s->atoms[i].element);element_color(c,s->atoms[i].element,table[i].color);
        table[i].rules=(RuleStamp){SIZE_MAX,SIZE_MAX};
    }
    size_t work=0;
    for (size_t k=0;k<c->rule_count;++k) {
        const AtomRule *r=&c->rules[k];
        if (r->has_cell) for (size_t d=0;d<3;++d) {
            int lower=d<2 ? -(int)c->expand_xy[d] : 0;
            if (r->cell[d]<lower || r->cell[d]>=lower+(int)c->repeat[d]) {free(table);return bad(e,k,"select.cell","atom_cell_out_of_range","cell is not part of the requested expansion");}
        }
        if ((r->indices && r->indices[r->index_count-1]>=s->count) || (r->range && r->last>=s->count)) {
            char field[96];
            if (r->indices) {
                const cJSON *selector=cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(c->rule_json,(int)k),"select");
                const cJSON *indices=cJSON_GetObjectItemCaseSensitive(selector,"source_indices");
                size_t at=0;for (const cJSON *v=indices->child;v;v=v->next,++at) if (v->valuedouble>=(double)s->count) break;
                snprintf(field,sizeof field,"select.source_indices[%zu]",at);
            } else strcpy(field,"select.source_range[1]");
            free(table);return bad(e,k,field,"invalid_atom_index","selector exceeds the selected input's atom count");
        }
        size_t count=r->indices ? r->index_count : r->range ? r->last-r->first+1 : s->count,matched=0;
        if (count>200000000u-work) {free(table);return fail_at(e,6,"atom_rule_work_limit","atom_overrides","source-rule evaluation exceeds work limit");}
        work+=count;
        for (size_t at=0;at<count;++at) {
            size_t index=r->indices ? r->indices[at] : r->range ? r->first+at : at;
            if (r->element && r->element!=s->atoms[index].element) continue;
            ++matched;if (!r->has_cell) apply(r,k,c->sphere_scale,&table[index]);
        }
        if (!matched) {free(table);return bad(e,k,"select","empty_atom_selection","selector matches no atoms in the selected input");}
    }
    for (size_t i=0;i<s->count;++i) if (!isfinite(table[i].radius) || table[i].radius<1e-9 || table[i].radius>1e6) {
        free(table);return fail_at(e,5,"invalid_atom_radius","atom_overrides","scaled atom radius must be within [1e-9,1e6] angstroms");
    }
    for (size_t k=0;k<c->rule_count;++k) if (c->rules[k].has_radius) {
        double r=c->rules[k].radius*c->sphere_scale;
        if (!isfinite(r) || r<1e-9 || r>1e6) {free(table);return bad(e,k,"radius_A","invalid_atom_radius","scaled atom radius is outside supported limits");}
    }
    *styles=table;return true;
}
void atom_style_instance(const Config *c, size_t source, unsigned element, const int origin[3], AtomStyle *style)
{
    for (size_t k=c->first_cell;k<c->rule_count;k=c->rules[k].next_cell) {
        const AtomRule *r=&c->rules[k];
        if (r->has_cell && !memcmp(r->cell,origin,sizeof r->cell) && selected(r,source,element)) apply(r,k,c->sphere_scale,style);
    }
}
