/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "movie.h"
#include "automation.h"
#include "sha256.h"
#include "input.h"
#include "parse.h"
#include "render.h"
#include "overlay.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef VR_HAVE_FFMPEG
cJSON *movie_render(const Config *c, const char *version, Error *e)
{
    (void)c;(void)version;fail_at(e,3,"movie_unavailable","movie","movie output requires ENABLE_FFMPEG=ON");return NULL;
}
#else
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>

static double dot(Vec3 a, Vec3 b) {return a.x[0]*b.x[0]+a.x[1]*b.x[1]+a.x[2]*b.x[2];}
static Vec3 mul(Vec3 a,double v) {for (int d=0;d<3;++d) a.x[d]*=v;return a;}
static Vec3 rotate(Vec3 v, Vec3 axis, double angle)
{
    double co=cos(angle),si=sin(angle),projection=dot(v,axis);
    Vec3 cross={{axis.x[1]*v.x[2]-axis.x[2]*v.x[1],axis.x[2]*v.x[0]-axis.x[0]*v.x[2],axis.x[0]*v.x[1]-axis.x[1]*v.x[0]}};
    for (int d=0;d<3;++d) v.x[d]=v.x[d]*co+cross.x[d]*si+axis.x[d]*projection*(1-co);
    return v;
}
static bool av_error(Error *e, int status, const char *step)
{
    char message[AV_ERROR_MAX_STRING_SIZE];av_strerror(status,message,sizeof message);
    return fail_at(e,7,"movie_encode_error","output","FFmpeg %s: %s",step,message);
}

typedef struct {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVStream *stream;
    AVFrame *frame;
    AVPacket *packet;
    AVIOContext *io;
    struct SwsContext *convert;
    FILE *file;
    char *temporary;
    size_t frames;
    bool committed;
} Encoder;

#if LIBAVFORMAT_VERSION_MAJOR >= 61
static int output_write(void *context, const uint8_t *data, int size)
#else
static int output_write(void *context, uint8_t *data, int size)
#endif
{
    FILE *file=context;
    return fwrite(data,1,(size_t)size,file)==(size_t)size ? size : AVERROR(EIO);
}
static int64_t output_seek(void *context,int64_t offset,int whence)
{
    FILE *file=context;
    if ((whence & ~AVSEEK_FORCE)==AVSEEK_SIZE) {struct stat st;return fstat(fileno(file),&st) ? AVERROR(errno) : st.st_size;}
    if (fseeko(file,(off_t)offset,whence & ~AVSEEK_FORCE)) return AVERROR(errno);
    off_t position=ftello(file);return position<0 ? AVERROR(errno) : position;
}
static void encoder_free(Encoder *v)
{
    sws_freeContext(v->convert);av_frame_free(&v->frame);av_packet_free(&v->packet);avcodec_free_context(&v->codec);
    if (v->format) {v->format->pb=NULL;avformat_free_context(v->format);}
    if (v->io) {av_freep(&v->io->buffer);avio_context_free(&v->io);}
    if (v->file) fclose(v->file);
    if (v->temporary) {if (!v->committed) unlink(v->temporary);free(v->temporary);}
    memset(v,0,sizeof *v);
}
static bool encoder_open(Encoder *v,const Config *c,Error *e)
{
    const AVCodec *codec=c->movie.codec==MOVIE_CODEC_MPEG4 ? NULL : avcodec_find_encoder_by_name("libx264");
    if (!codec && c->movie.codec!=MOVIE_CODEC_H264) codec=avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec) return fail_at(e,3,"movie_codec_unavailable","movie.codec","requested movie encoder is unavailable in the linked FFmpeg build");
    int rc=avformat_alloc_output_context2(&v->format,NULL,"mp4",NULL);
    if (rc<0) return av_error(e,rc,"allocate MP4 context");
    v->codec=avcodec_alloc_context3(codec);v->frame=av_frame_alloc();v->packet=av_packet_alloc();
    v->stream=avformat_new_stream(v->format,NULL);
    if (!v->codec || !v->frame || !v->packet || !v->stream) return fail(e,6,"cannot allocate movie encoder");
    AVCodecContext *cc=v->codec;
    cc->width=(int)c->width;cc->height=(int)c->height;cc->pix_fmt=AV_PIX_FMT_YUV420P;
    cc->time_base=(AVRational){1,(int)c->movie.fps};cc->framerate=(AVRational){(int)c->movie.fps,1};
    cc->thread_count=1;cc->gop_size=(int)c->movie.fps;cc->max_b_frames=0;
    cc->color_range=AVCOL_RANGE_MPEG;cc->colorspace=AVCOL_SPC_BT709;
    cc->color_primaries=AVCOL_PRI_BT709;cc->color_trc=AVCOL_TRC_BT709;
    cc->bit_rate=(int64_t)c->width*(int64_t)c->height*(int64_t)c->movie.fps*2;
    if (v->format->oformat->flags & AVFMT_GLOBALHEADER) cc->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary *options=NULL;
    if (codec->id==AV_CODEC_ID_H264) {av_dict_set(&options,"preset","fast",0);av_dict_set(&options,"crf","18",0);av_dict_set(&options,"tune","zerolatency",0);}
    rc=avcodec_open2(cc,codec,&options);av_dict_free(&options);
    if (rc<0) return av_error(e,rc,"open encoder");
    v->stream->time_base=cc->time_base;v->stream->avg_frame_rate=cc->framerate;
    if ((rc=avcodec_parameters_from_context(v->stream->codecpar,cc))<0) return av_error(e,rc,"copy stream parameters");
    v->frame->format=cc->pix_fmt;v->frame->width=cc->width;v->frame->height=cc->height;
    v->frame->color_range=cc->color_range;v->frame->colorspace=cc->colorspace;
    v->frame->color_primaries=cc->color_primaries;v->frame->color_trc=cc->color_trc;
    if ((rc=av_frame_get_buffer(v->frame,32))<0) return av_error(e,rc,"allocate video frame");
    v->convert=sws_getContext(cc->width,cc->height,AV_PIX_FMT_RGBA,cc->width,cc->height,cc->pix_fmt,SWS_BICUBIC,NULL,NULL,NULL);
    if (!v->convert) return fail(e,6,"cannot allocate movie color conversion");
    const int *coefficients=sws_getCoefficients(SWS_CS_ITU709);
    if ((rc=sws_setColorspaceDetails(v->convert,coefficients,1,coefficients,0,0,1<<16,1<<16))<0) return av_error(e,rc,"set video color space");
    size_t n=strlen(c->output);
    if (n>4095) return fail(e,7,"movie output path exceeds 4095 bytes");
    v->temporary=malloc(n+16);if (!v->temporary) return fail(e,6,"cannot allocate temporary movie path");
    snprintf(v->temporary,n+16,"%s.tmp.XXXXXX",c->output);
    int fd=mkstemp(v->temporary);
    if (fd<0) return fail(e,7,"cannot create temporary movie output for %s",c->output);
    v->file=fdopen(fd,"w+b");if (!v->file) {close(fd);return fail(e,7,"cannot open temporary movie stream");}
    uint8_t *buffer=av_malloc(65536);
    if (!buffer) return fail(e,6,"cannot allocate movie I/O buffer");
    v->io=avio_alloc_context(buffer,65536,1,v->file,NULL,output_write,output_seek);
    if (!v->io) {av_free(buffer);return fail(e,6,"cannot allocate movie I/O context");}
    v->format->pb=v->io;v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    if ((rc=avformat_write_header(v->format,NULL))<0) return av_error(e,rc,"write MP4 header");
    return true;
}
static bool encoder_drain(Encoder *v,Error *e)
{
    for (;;) {
        int rc=avcodec_receive_packet(v->codec,v->packet);
        if (rc==AVERROR(EAGAIN) || rc==AVERROR_EOF) return true;
        if (rc<0) return av_error(e,rc,"receive video packet");
        if (!v->packet->duration) v->packet->duration=1;
        av_packet_rescale_ts(v->packet,v->codec->time_base,v->stream->time_base);
        v->packet->stream_index=v->stream->index;
        rc=av_interleaved_write_frame(v->format,v->packet);av_packet_unref(v->packet);
        if (rc<0) return av_error(e,rc,"write video packet");
        if (!check_cancel(e)) return false;
    }
}
static bool encoder_frame(Encoder *v,const Image *image,Error *e)
{
    for (size_t i=0;i<image->width*image->height;++i) if (image->pixels[4*i+3]!=255)
        return fail(e,2,"MP4 does not support transparent pixels; choose an opaque background");
    int rc=av_frame_make_writable(v->frame);if (rc<0) return av_error(e,rc,"make video frame writable");
    const uint8_t *source[4]={image->pixels,NULL,NULL,NULL};int stride[4]={(int)image->width*4,0,0,0};
    rc=sws_scale(v->convert,source,stride,0,(int)image->height,v->frame->data,v->frame->linesize);
    if (rc!=(int)image->height) return fail(e,7,"FFmpeg movie color conversion failed");
    v->frame->pts=(int64_t)v->frames;
    rc=avcodec_send_frame(v->codec,v->frame);if (rc<0) return av_error(e,rc,"send video frame");
    if (!encoder_drain(v,e)) return false;
    ++v->frames;return true;
}
static bool encoder_finish(Encoder *v,const Config *c,Error *e)
{
    int rc=avcodec_send_frame(v->codec,NULL);
    if (rc<0) return av_error(e,rc,"flush encoder");
    if (!encoder_drain(v,e)) return false;
    if ((rc=av_write_trailer(v->format))<0) return av_error(e,rc,"write MP4 trailer");
    avio_flush(v->io);
    if (v->io->error<0) return av_error(e,v->io->error,"flush movie output");
    bool ok=!ferror(v->file);if (fclose(v->file)) ok=false;v->file=NULL;
    if (!ok) return fail(e,7,"cannot close movie output");
    if (!check_cancel(e)) return false;
    if (rename(v->temporary,c->output)) return fail(e,7,"cannot commit movie output %s",c->output);
    v->committed=true;return true;
}

typedef struct {
    const Config *config;
    const Structure *identity;
    Structure previous, interpolated;
    Camera camera;
    Encoder encoder;
    RenderWorkspace workspace;
    double lo[3],hi[3];
    bool fitting, initialized;
    size_t frames, snapshots, source_bytes, scene_retained;
    bool has_cell;
} Movie;

static void bounds_point(Movie *m,Vec3 point,double radius,bool projected)
{
    if (projected) point=(Vec3){{dot(point,m->camera.right),dot(point,m->camera.up),0}};
    for (int d=0;d<(projected ? 2 : 3);++d) {m->lo[d]=fmin(m->lo[d],point.x[d]-radius);m->hi[d]=fmax(m->hi[d],point.x[d]+radius);}
}
static bool emit(Movie *m,const Structure *s,Error *e)
{
    if (!check_cancel(e)) return false;
    if (m->frames>=100000) return fail(e,6,"movie exceeds 100000 output frames");
    Scene scene={0};Image image={0};bool ok=false;
    size_t extra=m->scene_retained+m->workspace.samples*12;
    if (!scene_build_retained(m->config,s,extra,&scene,e)) goto done;
    if (!m->initialized) {
        if (!camera_fit(m->config,&scene,&m->camera,e)) goto done;
        m->initialized=true;
    }
    if (m->fitting) {
        for (size_t i=0;i<scene.count;++i) {
            const Primitive *p=&scene.items[i];bounds_point(m,p->a,p->radius,true);bounds_point(m,p->b,p->radius,true);
        }
        for (size_t i=0;i<scene.triangle_count;++i) for (int k=0;k<3;++k) bounds_point(m,scene.faces[i].vertices[k],0,true);
        for (int d=0;d<3;++d) m->has_cell=m->has_cell || scene.cell_present[d];
    } else if (!render_image_workspace(m->config,&scene,&m->camera,m->source_bytes,&m->workspace,&image,e) || !encoder_frame(&m->encoder,&image,e)) goto done;
    ++m->frames;ok=true;
 done:
    image_free(&image);scene_free(&scene);return ok;
}
static bool visit(const Structure *s,size_t index,void *context,Error *e)
{
    Movie *m=context;const Structure *identity=m->identity;
    if (s->count!=identity->count) return fail(e,5,"trajectory atom count changes at frame %zu",index);
    for (size_t i=0;i<s->count;++i) if (s->atoms[i].element!=identity->atoms[i].element)
        return fail(e,5,"trajectory atom order/identity changes at frame %zu atom %zu",index,i);
    for (int d=0;d<3;++d) if (s->pbc[d]!=identity->pbc[d] || s->cell_present[d]!=identity->cell_present[d])
        return fail(e,5,"trajectory cell/periodic axes change at frame %zu",index);
    if (m->previous.atoms) {
        size_t steps=m->config->movie.interpolate;
        Atom *atoms=m->interpolated.atoms;
        if (!atoms) atoms=malloc(s->count*sizeof(Atom));
        if (!atoms) return fail(e,6,"cannot allocate interpolated geometry");
        m->interpolated=*s;m->interpolated.atoms=atoms;
        memcpy(m->interpolated.atoms,s->atoms,s->count*sizeof(Atom));
        for (size_t k=1;k<=steps;++k) {
            double t=(double)k/(double)(steps+1);
            for (int d=0;d<3;++d) for (int a=0;a<3;++a)
                m->interpolated.lattice[d].x[a]=m->previous.lattice[d].x[a]*(1-t)+s->lattice[d].x[a]*t;
            for (size_t i=0;i<s->count;++i) for (int d=0;d<3;++d)
                m->interpolated.atoms[i].position.x[d]=m->previous.atoms[i].position.x[d]*(1-t)+s->atoms[i].position.x[d]*t;
            if (!structure_validate(&m->interpolated,e) || !emit(m,&m->interpolated,e)) return false;
        }
    }
    if (!emit(m,s,e)) return false;
    ++m->snapshots;
    if (m->config->movie.interpolate) {
        Atom *atoms=m->previous.atoms;
        if (!atoms) atoms=malloc(s->count*sizeof(Atom));
        if (!atoms) return fail(e,6,"cannot retain previous trajectory geometry");
        m->previous=*s;m->previous.atoms=atoms;
        memcpy(m->previous.atoms,s->atoms,s->count*sizeof(Atom));
    }
    return true;
}
static bool pass(Movie *m,Error *e)
{
    const Config *c=m->config;
    /* Observers still visit and validate the complete stream. The returned
     * selected snapshot is discarded, so retain frame zero rather than copying
     * every later snapshot into an unused final-frame selection. */
    InputOptions input={.input=c->input,.format=m->identity->source.format,.final_frame=false,.frame=0,.recover=c->recover,
        .species=c->species,.species_count=c->species_count,.visit=visit,.visit_context=m};
    Structure selected={0};m->snapshots=0;m->frames=0;structure_free(&m->previous);
    bool ok=input_read(&input,&selected,e);
    structure_free(&selected);structure_free(&m->previous);structure_free(&m->interpolated);
    if (ok && m->snapshots!=m->identity->source.frame_count) return fail(e,5,"trajectory snapshot count changed or contains no ionic snapshots");
    return ok;
}
static bool fit_finish(Movie *m,bool sphere,Error *e)
{
    const Config *c=m->config;Camera *cam=&m->camera;
    double width=m->hi[0]-m->lo[0],height=m->hi[1]-m->lo[1];
    if (sphere) {double depth=m->hi[2]-m->lo[2];width=height=hypot(hypot(width,height),depth);}
    double guard=c->cell && m->has_cell ? c->cell_width+4*c->cell_outline_width+1 : 2;
    cam->scale=fmin(fmax((double)cam->viewport_width*(1-2*c->margin)-guard,.5)/width,
                    fmax((double)cam->viewport_height*(1-2*c->margin)-guard,.5)/height);
    if (!isfinite(cam->scale) || cam->scale<=0) return fail(e,5,"invalid global movie camera bounds");
    cam->center_x=(m->lo[0]+m->hi[0])/2;cam->center_y=(m->lo[1]+m->hi[1])/2;
    return overlay_scale_bar(c,cam,e);
}
static void center_camera(const Config *c,Camera *cam,double x,double y)
{
    cam->center_x=x+((double)c->width/2-((double)cam->viewport_x+(double)cam->viewport_width/2))/cam->scale;
    cam->center_y=y+((double)c->height/2-((double)cam->viewport_y+(double)cam->viewport_height/2))/cam->scale;
}
static bool unchanged(const Config *c,const char *before,Error *e)
{
    char after[65];if (!sha256_file(c->input,after,e)) return false;
    return !strcmp(before,after) || fail_at(e,7,"input_changed","input","movie input changed between validation and rendering");
}

cJSON *movie_render(const Config *c,const char *version,Error *e)
{
    /* The CLI/worker runs one encoder at a time. Keep diagnostics under QASR's
     * control, including when stderr must contain one structured JSON error. */
    int previous_log_level=av_log_get_level();av_log_set_level(AV_LOG_QUIET);
    struct timespec begin,fitted,finished;clock_gettime(CLOCK_MONOTONIC,&begin);
    Movie m={.config=c,.fitting=true};Structure source={0};Scene scene={0};Image image={0};cJSON *result=NULL;
    char input_hash[65],output_hash[65];bool ok=false;size_t fitted_frames=0;
    struct stat in,out;
    if (!config_validate_movie(c,e) || !report_preflight(c,e) || !check_cancel(e)) goto done;
    if (!stat(c->input,&in) && !stat(c->output,&out) && in.st_dev==out.st_dev && in.st_ino==out.st_ino) {
        fail(e,2,"movie output must differ from its input");goto done;
    }
    if (!sha256_file(c->input,input_hash,e) || !parse_structure(c,&source,e)) goto done;
    m.identity=&source;
    /* Reserve a conservative conversion/codec workspace and all streaming source
     * snapshots against the renderer's aggregate allocation budget. */
    size_t config_bytes=config_memory_bytes(c);
    m.source_bytes=6*source.count*sizeof(Atom)+config_bytes+c->width*c->height*64+16777216;
    m.scene_retained=m.source_bytes-source.count*sizeof(Atom)-config_bytes;
    if (m.source_bytes>=VR_MEMORY_BUDGET) {fail(e,6,"movie workspace exceeds memory budget");goto done;}
    for (int d=0;d<3;++d) {m.lo[d]=DBL_MAX;m.hi[d]=-DBL_MAX;}
    if (c->movie.mode==MOVIE_TRAJECTORY) {
        Format format=source.source.format;
        if (format!=FORMAT_XYZ && format!=FORMAT_EXTXYZ && format!=FORMAT_OUTCAR && format!=FORMAT_XML) {
            fail(e,3,"trajectory movies require multi-frame XYZ, Extended XYZ, OUTCAR or vasprun.xml; use orbit for a single structure");goto done;
        }
        if (source.source.frame_count<2 || source.source.frame_count>100000 ||
            source.source.frame_count-1>99999/(c->movie.interpolate+1)) {fail(e,2,"trajectory needs at least two snapshots and at most 100000 output frames");goto done;}
        if (!pass(&m,e) || !fit_finish(&m,false,e)) goto done;
        fitted_frames=m.frames;
        center_camera(c,&m.camera,m.camera.center_x,m.camera.center_y);
    } else {
        if (!scene_build(c,&source,&scene,e) || !camera_fit(c,&scene,&m.camera,e)) goto done;
        for (size_t i=0;i<scene.count;++i) {
            bounds_point(&m,scene.items[i].a,scene.items[i].radius,false);bounds_point(&m,scene.items[i].b,scene.items[i].radius,false);
        }
        for (size_t i=0;i<scene.triangle_count;++i) for (int k=0;k<3;++k) bounds_point(&m,scene.faces[i].vertices[k],0,false);
        for (int d=0;d<3;++d) m.has_cell=m.has_cell || scene.cell_present[d];
        if (!fit_finish(&m,true,e)) goto done;
    }
    clock_gettime(CLOCK_MONOTONIC,&fitted);
    if (!unchanged(c,input_hash,e) || !encoder_open(&m.encoder,c,e)) goto done;
    if (c->movie.mode==MOVIE_TRAJECTORY) {
        m.fitting=false;
        if (!pass(&m,e)) goto done;
        if (m.frames!=fitted_frames) {fail(e,5,"movie frame count changed during rendering");goto done;}
    } else {
        Camera initial=m.camera;Vec3 target={{(m.lo[0]+m.hi[0])/2,(m.lo[1]+m.hi[1])/2,(m.lo[2]+m.hi[2])/2}};
        Vec3 axis=mul(c->movie.axis,1/hypot(hypot(c->movie.axis.x[0],c->movie.axis.x[1]),c->movie.axis.x[2]));
        double denominator=(double)(fabs(remainder(c->movie.angle,360))<1e-9 ? c->movie.frames : c->movie.frames-1);
        for (size_t i=0;i<c->movie.frames;++i) {
            if (!check_cancel(e)) goto done;
            double angle=c->movie.angle*0.017453292519943295*(double)i/denominator;
            m.camera=initial;m.camera.view=rotate(initial.view,axis,angle);m.camera.up=rotate(initial.up,axis,angle);m.camera.right=rotate(initial.right,axis,angle);
            center_camera(c,&m.camera,dot(target,m.camera.right),dot(target,m.camera.up));
            if (!render_image_workspace(c,&scene,&m.camera,m.source_bytes,&m.workspace,&image,e) || !encoder_frame(&m.encoder,&image,e)) goto done;
            image_free(&image);
        }
    }
    if (!unchanged(c,input_hash,e) || !encoder_finish(&m.encoder,c,e) || !sha256_file(c->output,output_hash,e)) goto done;
    clock_gettime(CLOCK_MONOTONIC,&finished);
    result=cJSON_CreateObject();
    if (!result) {fail(e,6,"cannot allocate movie report");goto done;}
    cJSON *movie=config_movie_json(c),*camera=cJSON_CreateObject(),*settings=config_settings_json(c);
    if (!movie || !camera || !settings) {cJSON_Delete(movie);cJSON_Delete(camera);cJSON_Delete(settings);fail(e,6,"cannot allocate movie report settings");goto done;}
    cJSON_AddBoolToObject(result,"ok",1);cJSON_AddStringToObject(result,"version",version);
    cJSON_AddStringToObject(result,"input",c->input);cJSON_AddStringToObject(result,"input_sha256",input_hash);
    cJSON_AddStringToObject(result,"output",c->output);cJSON_AddStringToObject(result,"output_sha256",output_hash);
    cJSON_AddNumberToObject(movie,"encoded_frames",(double)m.encoder.frames);cJSON_AddNumberToObject(movie,"duration_s",(double)m.encoder.frames/(double)c->movie.fps);
    cJSON_AddStringToObject(movie,"encoder",m.encoder.codec->codec->name);
    cJSON_AddBoolToObject(camera,"fixed",c->movie.mode==MOVIE_TRAJECTORY);cJSON_AddNumberToObject(camera,"scale",m.camera.scale);
    cJSON_AddNumberToObject(camera,"center_x",m.camera.center_x);cJSON_AddNumberToObject(camera,"center_y",m.camera.center_y);
    cJSON_AddItemToObject(camera,"view",cJSON_CreateDoubleArray(m.camera.view.x,3));
    cJSON_AddNumberToObject(result,"width",(double)c->width);cJSON_AddNumberToObject(result,"height",(double)c->height);
    cJSON_AddItemToObject(result,"movie",movie);cJSON_AddItemToObject(result,"camera",camera);cJSON_AddItemToObject(result,"settings",settings);ok=true;
    double fit_ms=(double)(fitted.tv_sec-begin.tv_sec)*1000+(double)(fitted.tv_nsec-begin.tv_nsec)/1e6;
    double render_ms=(double)(finished.tv_sec-fitted.tv_sec)*1000+(double)(finished.tv_nsec-fitted.tv_nsec)/1e6;
    cJSON *timings=cJSON_CreateObject();
    if (!timings || !cJSON_AddNumberToObject(timings,"fit",fit_ms) || !cJSON_AddNumberToObject(timings,"render_encode",render_ms) ||
        !cJSON_AddNumberToObject(timings,"total",fit_ms+render_ms) || !cJSON_AddItemToObject(result,"timings_ms",timings)) {
        cJSON_Delete(timings);ok=false;fail(e,6,"cannot allocate movie timing report");
    }
    if (c->benchmark) fprintf(stderr,"movie_stages_ms: fit=%.3f render_encode=%.3f total=%.3f frames=%zu\n",fit_ms,render_ms,fit_ms+render_ms,m.encoder.frames);
 done:
    if (!ok) {cJSON_Delete(result);result=NULL;}
    encoder_free(&m.encoder);structure_free(&m.previous);structure_free(&m.interpolated);
    render_workspace_free(&m.workspace);
    image_free(&image);scene_free(&scene);structure_free(&source);
    av_log_set_level(previous_log_level);return result;
}
#endif
