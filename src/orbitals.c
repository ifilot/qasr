/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "elements.h"
#include "surface.h"
#include "cJSON.h"
#include "vec.h"
#include "sha256.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MESH_LIMIT 200000u
_Static_assert(sizeof(float)==4 && sizeof(double)==8,"PLY requires 32-bit float and 64-bit double storage");
/** @brief Set the stable diagnostic for invalid orbital metadata or mesh data.
 * @param[out] e Non-null error destination.
 * @param[in] message Borrowed diagnostic text.
 * @return false, for direct failure propagation.
 */
static bool bad(Error *e, const char *message)
{
    return fail_at(e,2,"invalid_orbital","orbitals","%s",message);
}
/** @brief Append one PyTessel-layout PLY mesh under the configuration budget.
 * @param[in,out] c Initialized configuration; owns appended triangles, including
 * partial results on failure. Scratch vertices/normals are released before return.
 * @param[in] path Borrowed file path, opened in binary mode and closed here.
 * @param[in] scale Positive mesh-unit to angstrom conversion factor.
 * @param[in] origin Translation in mesh units, applied before scaling positions.
 * @param[in] color Borrowed four-channel opaque phase color.
 * @param[in] layer Zero-based orbital layer identity used by contour detection.
 * @param[in,out] remaining Remaining input triangle allowance across all layers;
 * charged from the header, including subsequently skipped degenerate triangles.
 * @param[out] e Non-null diagnostic destination.
 * @return true on success; false on invalid/truncated data, I/O, allocation,
 * budget or cancellation failure. PLY has no units; normals are never scaled.
 */
static bool load_ply(Config *c, const char *path, double scale, Vec3 origin,
                     const uint8_t color[4], size_t layer, size_t *remaining, Error *e)
{
    FILE *f=fopen(path,"rb");
    if (!f) return fail_at(e,4,"orbital_open","orbitals","cannot open PLY mesh: %s",path);
    Reader r={.file=f,.path=path};bool ok=false,binary=false,ended=false;
    size_t nv=0,nf=0,properties=0;int section=0;bool wide[6]={false};
    Vec3 *vertices=NULL,*normals=NULL;
    if (read_line(&r,e)!=1 || strcmp(r.text,"ply") || read_line(&r,e)!=1) goto invalid;
    if (!strcmp(r.text,"format binary_little_endian 1.0")) binary=true;
    else if (strcmp(r.text,"format ascii 1.0")) goto invalid;
    for (size_t lines=0;lines<128 && read_line(&r,e)==1;++lines) {
        char *w[5];size_t n=split_words(r.text,w,5);
        if (!n || !strcmp(w[0],"comment") || !strcmp(w[0],"obj_info")) continue;
        if (n==1 && !strcmp(w[0],"end_header")) {ended=true;break;}
        if (n==3 && !strcmp(w[0],"element")) {
            if (!section && !strcmp(w[1],"vertex") && integer(w[2],MESH_LIMIT,&nv) && nv) section=1;
            else if (section==1 && !strcmp(w[1],"face") && integer(w[2],MESH_LIMIT,&nf) && nf) section=2;
            else goto invalid;
        } else if (section==1 && n==3 && !strcmp(w[0],"property")) {
            const char *names[]={"x","y","z","nx","ny","nz"};
            if (properties==6 || strcmp(w[2],names[properties]) || (strcmp(w[1],"float") && strcmp(w[1],"double"))) goto invalid;
            wide[properties++]=!strcmp(w[1],"double");
        } else if (section==2 && n==5 && !strcmp(w[0],"property") &&
                   !strcmp(w[1],"list") && !strcmp(w[2],"uchar") &&
                   (!strcmp(w[3],"uint") || !strcmp(w[3],"int")) &&
                   (!strcmp(w[4],"vertex_indices") || !strcmp(w[4],"vertex_index"))) section=3;
        else goto invalid;
    }
    if (!ended || section!=3 || (properties!=3 && properties!=6)) goto invalid;
    size_t charge=config_memory_bytes(c),extra=nf*sizeof(FaceTriangle)+nv*2*sizeof(Vec3);
    if (nf>*remaining || charge>VR_CONFIG_MEMORY_LIMIT || extra>VR_CONFIG_MEMORY_LIMIT-charge) {
        fail(e,6,"orbital meshes exceed 200000 triangles or 64 MiB configuration budget");goto done;
    }
    *remaining-=nf;
    vertices=calloc(nv,sizeof *vertices);normals=calloc(nv,sizeof *normals);
    FaceTriangle *triangles=realloc(c->orbital_triangles,(c->orbital_count+nf)*sizeof *triangles);
    if (!vertices || !normals || !triangles) {if (triangles) c->orbital_triangles=triangles;fail(e,6,"cannot allocate orbital mesh");goto done;}
    c->orbital_triangles=triangles;c->orbital_capacity=c->orbital_count+nf;
    size_t stride=0;
    for (size_t d=0;d<properties;++d) stride+=wide[d] ? 8u : 4u;
    for (size_t i=0;i<nv;++i) {
        if (!check_cancel(e)) goto done;
        unsigned char packed[48];size_t offset=0;
        if (binary && fread(packed,1,stride,f)!=stride) goto invalid;
        char *words[7];
        if (!binary && (read_line(&r,e)!=1 || split_words(r.text,words,7)!=properties)) goto invalid;
        for (size_t d=0;d<properties;++d) {
            double value;
            if (binary) {
                size_t n=wide[d] ? 8 : 4;uint64_t bits=0;
                for (size_t b=0;b<n;++b) bits|=(uint64_t)packed[offset+b]<<(8*b);
                offset+=n;
                if (wide[d]) memcpy(&value,&bits,8);
                else {uint32_t small=(uint32_t)bits;float v;memcpy(&v,&small,4);value=v;}
            } else if (!number(words[d],&value)) goto invalid;
            if (!isfinite(value) || fabs(value)>1e10) goto invalid;
            if (d<3) vertices[i].x[d]=scale*(value+origin.x[d]);
            else normals[i].x[d-3]=value;
        }
        if (properties==6) {
            double norm=length(normals[i]);if (norm<1e-12 || !isfinite(norm)) goto invalid;
            normals[i]=vmul(normals[i],1/norm);
        }
    }
    for (size_t i=0;i<nf;++i) {
        if (!check_cancel(e)) goto done;
        size_t index[3];
        if (binary) {
            unsigned char packed[13];
            if (fread(packed,1,sizeof packed,f)!=sizeof packed || packed[0]!=3) goto invalid;
            for (size_t d=0;d<3;++d) {
                const unsigned char *b=packed+1+d*4;
                index[d]=(size_t)b[0]|((size_t)b[1]<<8)|((size_t)b[2]<<16)|((size_t)b[3]<<24);
            }
        } else {
            char *w[5];if (read_line(&r,e)!=1 || split_words(r.text,w,5)!=4 || strcmp(w[0],"3")) goto invalid;
            for (size_t d=0;d<3;++d) if (!integer(w[d+1],nv-1,&index[d])) goto invalid;
        }
        FaceTriangle t;memset(&t,0,sizeof t);t.smooth=true;memcpy(t.color,color,4);
        t.instance=c->orbital_count;t.face=layer;
        for (size_t d=0;d<3;++d) {
            if (index[d]>=nv) goto invalid;
            t.vertices[d]=vertices[index[d]];t.normals[d]=normals[index[d]];
        }
        Vec3 normal=cross(vsub(t.vertices[1],t.vertices[0]),vsub(t.vertices[2],t.vertices[0]));
        if (length(normal)<1e-15) continue; /* Marching cubes may emit zero-area triangles. */
        if (properties==3) for (size_t d=0;d<3;++d) t.normals[d]=unit(normal);
        c->orbital_triangles[c->orbital_count++]=t;
    }
    if (binary) {if (fgetc(f)!=EOF) goto invalid;}
    else {int ch;while ((ch=fgetc(f))!=EOF) if (!isspace((unsigned char)ch)) goto invalid;}
    if (ferror(f)) goto invalid;
    ok=true;goto done;
invalid:
    if (!e->code) bad(e,"invalid PLY: expected ASCII or binary little-endian vertices x/y/z [nx/ny/nz] and triangular uchar/uint faces");
done:
    free(vertices);free(normals);fclose(f);return ok;
}
void config_orbitals_free(Config *c)
{
    free(c->orbital_triangles);c->orbital_triangles=NULL;
    c->orbital_count=0;c->orbital_capacity=0;
    memset(c->orbital_hashes,0,sizeof c->orbital_hashes);
    cJSON_Delete(c->orbitals_json);c->orbitals_json=NULL;
}
bool config_orbitals(Config *c, const cJSON *array, const char *file, Error *e)
{
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array)>32) return bad(e,"orbitals requires at most 32 PLY layers");
    config_orbitals_free(c);
    c->orbitals_json=cJSON_Duplicate(array,1);
    if (!c->orbitals_json) return fail(e,6,"cannot allocate orbital metadata");
    size_t layer=0,remaining=MESH_LIMIT;
    for (cJSON *j=c->orbitals_json->child;j;j=j->next,++layer) {
        if (!cJSON_IsObject(j)) return bad(e,"expected orbital layer object");
        const cJSON *path=cJSON_GetObjectItemCaseSensitive(j,"file"),*units=cJSON_GetObjectItemCaseSensitive(j,"units");
        if (!cJSON_IsString(path) || !*path->valuestring || !strcmp(path->valuestring,"-") ||
            !cJSON_IsString(units) || (strcmp(units->valuestring,"bohr") && strcmp(units->valuestring,"angstrom")))
            return bad(e,"each orbital requires file and explicit units: bohr or angstrom");
        double scale=!strcmp(units->valuestring,"bohr") ? 0.529177210903 : 1;
        Vec3 origin={{0,0,0}};uint8_t color[4];decode_color("#4c9cdb",color);
        for (const cJSON *v=j->child;v;v=v->next) {
            if (!strcmp(v->string,"file") || !strcmp(v->string,"units")) continue;
            if (!strcmp(v->string,"color")) {
                if (!cJSON_IsString(v) || strlen(v->valuestring)!=7 || v->valuestring[0]!='#') return bad(e,"orbital color must be opaque #RRGGBB");
                for (size_t d=1;d<7;++d) if (!isxdigit((unsigned char)v->valuestring[d])) return bad(e,"invalid orbital color");
                decode_color(v->valuestring,color);
            } else if (!strcmp(v->string,"origin")) {
                if (!cJSON_IsArray(v) || cJSON_GetArraySize(v)!=3) return bad(e,"origin requires three coordinates in mesh units");
                size_t d=0;for (const cJSON *x=v->child;x;x=x->next,++d) {
                    if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble) || fabs(x->valuedouble)>1e10) return bad(e,"invalid orbital origin");
                    origin.x[d]=x->valuedouble;
                }
            } else return bad(e,"unknown orbital layer field");
        }
        char resolved[4096],relative[4096],cwd[4096];
        const char *slash=file ? strrchr(file,'/') : NULL;
        int n=slash && *path->valuestring!='/' ? snprintf(relative,sizeof relative,"%.*s/%s",(int)(slash-file),file,path->valuestring) : snprintf(relative,sizeof relative,"%s",path->valuestring);
        if (n<0 || (size_t)n>=sizeof relative || !getcwd(cwd,sizeof cwd)) return bad(e,"orbital path too long");
        n=*relative=='/' ? snprintf(resolved,sizeof resolved,"%s",relative) : snprintf(resolved,sizeof resolved,"%s/%s",cwd,relative);
        if (n<0 || (size_t)n>=sizeof resolved) return bad(e,"orbital path too long");
        char after[65];
        if (!sha256_file(resolved,c->orbital_hashes[layer],e) || !load_ply(c,resolved,scale,origin,color,layer,&remaining,e) || !sha256_file(resolved,after,e)) return false;
        if (strcmp(after,c->orbital_hashes[layer])) return bad(e,"orbital file changed while loading");
        cJSON *value=cJSON_CreateString(resolved);
        if (!value || !cJSON_ReplaceItemInObjectCaseSensitive(j,"file",value)) {cJSON_Delete(value);return fail(e,6,"cannot retain orbital path");}
    }
    return true;
}
