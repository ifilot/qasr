/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include "automation.h"
#include "render.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool json_write(const char *path, const cJSON *json, bool exclusive, Error *e)
{
    char *text=json ? cJSON_PrintUnformatted(json) : NULL;
    if (!text) return fail(e,6,"cannot encode JSON output");
    if (!strcmp(path,"-")) {
        bool ok=fputs(text,stdout)!=EOF && putchar('\n')!=EOF && fflush(stdout)==0;
        free(text);return ok ? true : fail(e,7,"failed writing JSON to stdout");
    }
    size_t n=strlen(path);
    if (n>4095) {free(text);return fail(e,7,"JSON output path exceeds 4095 bytes");}
    char *temporary=malloc(n+16);
    if (!temporary) {free(text);return fail(e,6,"cannot allocate JSON output path");}
    snprintf(temporary,n+16,"%s.tmp.XXXXXX",path);
    int fd=mkstemp(temporary);
    if (fd<0) {free(temporary);free(text);return fail_at(e,7,"json_write_error",path,"cannot create temporary output for %s",path);}
    FILE *f=fdopen(fd,"wb");bool ok=false;
    if (!f) {close(fd);fail(e,7,"cannot open temporary JSON stream");}
    else {
        ok=fputs(text,f)!=EOF && fputc('\n',f)!=EOF;
        if (fclose(f)) ok=false;
        if (!ok) fail(e,7,"failed writing/closing JSON output");
    }
    if (ok) {
        int rc=exclusive ? link(temporary,path) : rename(temporary,path);
        if (rc) ok=fail_at(e,7,exclusive && errno==EEXIST ? "config_already_exists" : "json_write_error",path,
                          "cannot commit JSON output %s%s",path,exclusive ? "; existing files are never overwritten" : "");
    }
    unlink(temporary);free(temporary);free(text);return ok;
}

bool config_initialize_file(const Config *c, Error *e)
{
    if (!c->user_path) return fail_at(e,2,"no_user_config_path","user_config","set an absolute XDG_CONFIG_HOME or HOME before --init-config");
    char path[4096];snprintf(path,sizeof path,"%s",c->user_path);
    for (char *p=path+1;*p;++p) if (*p=='/') {
        *p='\0';
        if (mkdir(path,0700) && errno!=EEXIST) return fail_at(e,7,"config_directory_error",path,"cannot create user configuration directory");
        *p='/';
    }
    cJSON *template=cJSON_Parse("{\"schema_version\":1,\"image\":{\"width\":512,\"height\":512,\"style\":\"shaded\"},\"geometry\":{\"sphere_scale\":1,\"bond_radius_A\":0.12},\"overlays\":{\"cell\":true,\"labels\":\"none\",\"axes\":false,\"legend\":false,\"scale_bar\":false}}");
    bool ok=json_write(c->user_path,template,true,e);cJSON_Delete(template);if (!ok) return false;
    cJSON *result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"ok",1);cJSON_AddStringToObject(result,"path",c->user_path);
    ok=json_write("-",result,false,e);cJSON_Delete(result);return ok;
}

static bool same_target(const char *a, const char *b)
{
    if (!a || !b) return false;
    if (!strcmp(a,b)) return true;
    struct stat x,y;
    if (!stat(a,&x) && !stat(b,&y) && x.st_dev==y.st_dev && x.st_ino==y.st_ino) return true;
    char ar[4096],br[4096];
    const char *values[]={a,b};char *destinations[]={ar,br};
    for (size_t i=0;i<2;++i) {
        char copy[4096];if (strlen(values[i])>=sizeof copy) return false;
        strcpy(copy,values[i]);char *slash=strrchr(copy,'/');const char *leaf=slash ? slash+1 : copy;
        char name[4096];strcpy(name,leaf);
        if (slash) *slash='\0';
        char parent[4096];if (!realpath(slash ? (*copy ? copy : "/") : ".",parent)) return false;
        size_t n=strlen(parent),m=strlen(name);if (n+m+2>4096) return false;
        memcpy(destinations[i],parent,n);destinations[i][n]='/';memcpy(destinations[i]+n+1,name,m+1);
    }
    return !strcmp(ar,br);
}
bool report_preflight(const Config *c, Error *e)
{
    if (!c->report || !strcmp(c->report,"-")) return true;
    if (same_target(c->report,c->input) || same_target(c->report,c->output))
        return fail_at(e,2,"report_path_conflict","runtime.report","report path must differ from input and image output");
    struct stat target;
    if (!stat(c->report,&target) && S_ISDIR(target.st_mode))
        return fail_at(e,7,"report_write_error","runtime.report","report target is a directory");
    if (strlen(c->report)>4095) return fail(e,7,"report path too long");
    char temporary[4112];snprintf(temporary,sizeof temporary,"%s.tmp.XXXXXX",c->report);
    int fd=mkstemp(temporary);if (fd<0) return fail_at(e,7,"report_write_error","runtime.report","cannot write report directory");
    close(fd);unlink(temporary);return true;
}

void print_json_error(const Error *e)
{
    cJSON *root=cJSON_CreateObject(),*error=cJSON_CreateObject();
    cJSON_AddBoolToObject(root,"ok",0);cJSON_AddStringToObject(error,"code",*e->name ? e->name : "internal_error");
    cJSON_AddNumberToObject(error,"exit_code",e->code ? e->code : 8);
    if (*e->path) cJSON_AddStringToObject(error,"path",e->path);else cJSON_AddNullToObject(error,"path");
    cJSON_AddStringToObject(error,"message",e->message);cJSON_AddItemToObject(root,"error",error);
    char *text=cJSON_PrintUnformatted(root);
    if (text) {fprintf(stderr,"%s\n",text);free(text);}else fputs("{\"ok\":false,\"error\":{\"code\":\"allocation_failure\",\"exit_code\":6}}\n",stderr);
    cJSON_Delete(root);
}

cJSON *render_report(const Config *c, const Structure *s, const Scene *scene, const Camera *camera,
                     const PngOptions *png, const char *version, const char input_hash[65], const double stages[5], Error *e)
{
    char output_hash[65],config_hash[65];
    if (!sha256_file(c->output,output_hash,e)) return NULL;
    cJSON *settings=config_settings_json(c);char *normalized=settings ? cJSON_PrintUnformatted(settings) : NULL;
    if (!normalized) {cJSON_Delete(settings);fail(e,6,"cannot encode report configuration");return NULL;}
    sha256_data(normalized,strlen(normalized),config_hash);free(normalized);
    cJSON *root=cJSON_CreateObject(),*input=cJSON_CreateObject(),*output=cJSON_CreateObject(),*counts=cJSON_CreateObject(),*times=cJSON_CreateObject(),*options=cJSON_CreateObject();
    if (!root || !input || !output || !counts || !times || !options) {
        cJSON_Delete(root);cJSON_Delete(input);cJSON_Delete(output);cJSON_Delete(counts);cJSON_Delete(times);cJSON_Delete(options);cJSON_Delete(settings);
        fail(e,6,"cannot allocate render report");return NULL;
    }
    cJSON_AddBoolToObject(root,"ok",1);cJSON_AddNumberToObject(root,"schema_version",1);cJSON_AddStringToObject(root,"version",version);
    cJSON_AddStringToObject(input,"path",c->input);cJSON_AddStringToObject(input,"sha256",input_hash);
    cJSON_AddStringToObject(input,"format",s->format==FORMAT_XML ? "xml" : s->format==FORMAT_OUTCAR ? "outcar" : "poscar");
    cJSON_AddNumberToObject(input,"frame",(double)s->frame);cJSON_AddNumberToObject(input,"frame_count",(double)s->frame_count);
    cJSON_AddBoolToObject(input,"recovered",s->recovered);cJSON_AddNumberToObject(input,"atoms",(double)s->count);
    cJSON_AddStringToObject(output,"format",image_format_name(c->output));
    cJSON_AddStringToObject(output,"path",c->output);cJSON_AddStringToObject(output,"sha256",output_hash);
    cJSON_AddNumberToObject(output,"width",(double)camera->width);cJSON_AddNumberToObject(output,"height",(double)camera->height);
    struct stat info;if (!stat(c->output,&info)) cJSON_AddNumberToObject(output,"bytes",(double)info.st_size);
    cJSON_AddNumberToObject(counts,"atom_instances",(double)scene->atom_count);cJSON_AddNumberToObject(counts,"bonds",(double)scene->bond_count);
    cJSON_AddNumberToObject(counts,"atom_rules",(double)c->rule_count);
    cJSON_AddStringToObject(root,"config_sha256",config_hash);
    const char *groups[]={"image","camera","geometry","overlays"};
    for (size_t i=0;i<4;++i) cJSON_AddItemToObject(options,groups[i],cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(settings,groups[i]),1));
    cJSON_AddNumberToObject(options,"threads_requested",(double)c->threads);
    cJSON *resolved=cJSON_GetObjectItemCaseSensitive(options,"image");
    cJSON_ReplaceItemInObjectCaseSensitive(resolved,"png_compression",cJSON_CreateNumber(png->compression));
    if (strcmp(image_format_name(c->output),"png")) {
        cJSON_DeleteItemFromObjectCaseSensitive(resolved,"png_encoder");
        cJSON_DeleteItemFromObjectCaseSensitive(resolved,"png_filter");
        cJSON_DeleteItemFromObjectCaseSensitive(resolved,"png_compression");
    }
    const char *names[]={"config","parse","scene","render",!strcmp(image_format_name(c->output),"png") ? "png" : "encode"};
    for (size_t i=0;i<5;++i) cJSON_AddNumberToObject(times,names[i],stages[i]);
    cJSON_AddItemToObject(root,"input",input);cJSON_AddItemToObject(root,"output",output);cJSON_AddItemToObject(root,"counts",counts);
    cJSON_AddItemToObject(root,"settings",options);cJSON_AddItemToObject(root,"stages_ms",times);
    cJSON_AddItemToObject(root,"sources",cJSON_Duplicate(c->sources,1));cJSON_Delete(settings);
    if (!root || !input || !output || !counts || !times || !options) {cJSON_Delete(root);fail(e,6,"cannot allocate render report");return NULL;}
    return root;
}
