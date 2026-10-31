/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "job.h"
#include "automation.h"
#include "parse.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static _Atomic int terminate_requested=0;
static void interrupted(int signal_number)
{
    atomic_store_explicit(&vr_cancel_requested,1,memory_order_relaxed);
    if (signal_number==SIGTERM) atomic_store_explicit(&terminate_requested,1,memory_order_relaxed);
}
static cJSON *error_json(const Error *e)
{
    cJSON *j=cJSON_CreateObject();if (!j) return NULL;
    bool ok=cJSON_AddStringToObject(j,"code",*e->name ? e->name : "internal_error") &&
        cJSON_AddNumberToObject(j,"exit_code",e->code ? e->code : 8) &&
        (*e->path ? cJSON_AddStringToObject(j,"path",e->path) : cJSON_AddNullToObject(j,"path")) &&
        cJSON_AddStringToObject(j,"message",e->message);
    if (!ok) {cJSON_Delete(j);return NULL;}
    return j;
}
static cJSON *dispatch(const cJSON *request, JobCache *cache, const char *version, Error *e)
{
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(request,"id"),*op=cJSON_GetObjectItemCaseSensitive(request,"op"),*job=cJSON_GetObjectItemCaseSensitive(request,"job");
    if (!cJSON_IsString(id) || !*id->valuestring || strlen(id->valuestring)>128) {fail_at(e,2,"invalid_request","id","id must be a nonempty string of at most 128 bytes");return NULL;}
    for (const unsigned char *p=(const unsigned char *)id->valuestring;*p;++p) if (*p<32 || *p>126) {fail_at(e,2,"invalid_request","id","id must contain printable ASCII characters");return NULL;}
    for (const cJSON *j=request->child;j;j=j->next) if (strcmp(j->string,"id") && strcmp(j->string,"op") && strcmp(j->string,"job")) {fail_at(e,2,"invalid_request",j->string,"unknown worker request field");return NULL;}
    if (!cJSON_IsString(op)) {fail_at(e,2,"invalid_request","op","op is required: render, validate, stats or clear_cache");return NULL;}
    if (!strcmp(op->valuestring,"stats") || !strcmp(op->valuestring,"clear_cache")) {
        if (job) {fail_at(e,2,"invalid_request","job","control requests do not accept a job");return NULL;}
        if (!strcmp(op->valuestring,"clear_cache")) job_cache_clear(cache);
        return job_cache_info(cache);
    }
    bool validation=!strcmp(op->valuestring,"validate");
    if (strcmp(op->valuestring,"render") && !validation) {fail_at(e,2,"invalid_request","op","unknown worker operation");return NULL;}
    if (!cJSON_IsObject(job)) {fail_at(e,2,"invalid_request","job","job must be an invocation object");return NULL;}
    Config c={0};cJSON *result=NULL;struct timespec start,configured;clock_gettime(CLOCK_MONOTONIC,&start);
    if (!config_request(job,validation,&c,e)) goto done;
    clock_gettime(CLOCK_MONOTONIC,&configured);
    if (validation) {
        job_cache_clear(cache);
        Structure s={0};AtomStyle *styles=NULL;
        /* Validation evicts retained data so parser allocations share the same budget. */
        if (c.input && (!parse_structure(&c,&s,e) || !atom_styles_resolve(&c,&s,&styles,e))) {free(styles);structure_free(&s);goto done;}
        if (c.input && c.face_count) {
            Scene scene={0};bool valid=scene_build(&c,&s,&scene,e);scene_free(&scene);
            if (!valid) {free(styles);structure_free(&s);goto done;}
        }
        free(styles);structure_free(&s);
        result=cJSON_CreateObject();cJSON_AddBoolToObject(result,"input_validated",c.input!=NULL);cJSON_AddBoolToObject(result,"selectors_validated",c.input!=NULL);
    } else {
        double ms=(double)(configured.tv_sec-start.tv_sec)*1000+(double)(configured.tv_nsec-start.tv_nsec)/1e6;
        result=job_render(&c,cache,true,version,ms,e);
        if (result && c.report && strcmp(c.report,"-") && !json_write(c.report,result,false,e)) {cJSON_Delete(result);result=NULL;}
    }
done:
    config_free(&c);return result;
}
int worker_run(int argc, char **argv, const char *version)
{
    Error e={0};JobCache cache={.limit=64u*1024u*1024u};size_t max_jobs=0;
    bool seen_cache=false,seen_max=false;
    for (int i=2;i<argc;++i) {
        const char *option=argv[i];size_t value;
        if (i+1==argc || !integer(argv[++i],1000000,&value)) {fputs("invalid worker option/value\n",stderr);return 2;}
        if (!strcmp(option,"--cache-mib") && !seen_cache && value<=512) {cache.limit=value*1024u*1024u;seen_cache=true;}
        else if (!strcmp(option,"--max-jobs") && !seen_max && value) {max_jobs=value;seen_max=true;}
        else {fputs("use --batch [--cache-mib 0..512] [--max-jobs 1..1000000]\n",stderr);return 2;}
    }
    if (!atomic_is_lock_free(&vr_cancel_requested) || !atomic_is_lock_free(&terminate_requested)) {fputs("worker cancellation requires lock-free atomics\n",stderr);return 3;}
    struct sigaction action={0},old_int,old_term,old_pipe;action.sa_handler=interrupted;sigemptyset(&action.sa_mask);
    sigaction(SIGINT,&action,&old_int);sigaction(SIGTERM,&action,&old_term);
    action.sa_handler=SIG_IGN;sigaction(SIGPIPE,&action,&old_pipe);
    char *line=malloc(VR_MAX_CONFIG_BYTES+1);int status=0;size_t jobs=0;
    if (!line) {status=6;goto done;}
    while (!max_jobs || jobs<max_jobs) {
        size_t n=0;bool too_long=false,nul=false;int ch;
        errno=0;
        while ((ch=fgetc(stdin))!=EOF && ch!='\n') {
            if (!ch) nul=true;
            if (n<VR_MAX_CONFIG_BYTES) line[n++]=(char)ch;else too_long=true;
            if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) break;
        }
        if (atomic_load_explicit(&vr_cancel_requested,memory_order_relaxed)) {status=130;break;}
        if (ch==EOF && ferror(stdin)) {status=7;break;}
        if (!n && ch==EOF) break;
        line[n]='\0';
        if (!too_long && !nul && strspn(line," \t\r")==n) continue;
        ++jobs;memset(&e,0,sizeof e);atomic_store_explicit(&vr_cancel_requested,0,memory_order_relaxed);
        cJSON *request=NULL,*result=NULL,*reply=cJSON_CreateObject();
        if (!reply) {status=6;break;}
        if (too_long || nul) fail_at(&e,2,"invalid_request",NULL,"request exceeds 1 MiB or contains NUL");
        else request=config_json_text(line,&e);
        const cJSON *id=request ? cJSON_GetObjectItemCaseSensitive(request,"id") : NULL;
        if (cJSON_IsString(id) && strlen(id->valuestring)<=128) cJSON_AddStringToObject(reply,"id",id->valuestring);else cJSON_AddNullToObject(reply,"id");
        if (request) result=dispatch(request,&cache,version,&e);
        if (!result && !e.code) fail(&e,6,"cannot allocate worker result");
        cJSON_AddBoolToObject(reply,"ok",result!=NULL);
        cJSON *body=result ? result : error_json(&e);
        if (!body || !cJSON_GetObjectItemCaseSensitive(reply,"id") || !cJSON_GetObjectItemCaseSensitive(reply,"ok") || !cJSON_AddItemToObject(reply,result ? "result" : "error",body)) {
            cJSON_Delete(body);cJSON_Delete(reply);cJSON_Delete(request);status=6;break;
        }
        /* Cancellation belongs to this request; allow its response to be flushed. */
        atomic_store_explicit(&vr_cancel_requested,0,memory_order_relaxed);
        if (!json_write("-",reply,false,&e)) status=7;
        cJSON_Delete(reply);cJSON_Delete(request);
        if (status || atomic_load_explicit(&terminate_requested,memory_order_relaxed)) break;
    }
done:
    free(line);job_cache_clear(&cache);
    sigaction(SIGINT,&old_int,NULL);sigaction(SIGTERM,&old_term,NULL);sigaction(SIGPIPE,&old_pipe,NULL);
    return status;
}
