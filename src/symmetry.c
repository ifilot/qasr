/* SPDX-License-Identifier: GPL-3.0-only */
#include "scene.h"
#include "cJSON.h"
#include "vec.h"
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#define SYMMETRY_DISC_SEGMENTS 64u
#define SYMMETRY_VERIFY_WORK 200000000u
static const double pi=3.14159265358979323846;

static bool bad(Error *e, const char *field, const char *message)
{
    char path[64];snprintf(path,sizeof path,"symmetry%s%s",*field ? "." : "",field);
    return fail_at(e,2,"invalid_symmetry",path,"%s: %s",path,message);
}
static bool color_text(const cJSON *j, bool alpha, char out[10])
{
    if (!cJSON_IsString(j)) return false;
    size_t n=strlen(j->valuestring);
    if ((n!=7 && !(alpha && n==9)) || j->valuestring[0]!='#') return false;
    for (size_t k=1;k<n;++k) if (!isxdigit((unsigned char)j->valuestring[k])) return false;
    memcpy(out,j->valuestring,n+1);return true;
}
static bool real(const cJSON *j, double low, double high, bool open_low, double *out)
{
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || j->valuedouble>high ||
        (open_low ? j->valuedouble<=low : j->valuedouble<low)) return false;
    *out=j->valuedouble;return true;
}
static bool vector(const cJSON *j, double limit, Vec3 *out)
{
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j)!=3) return false;
    size_t d=0;
    for (const cJSON *v=j->child;v;v=v->next,++d) {
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || fabs(v->valuedouble)>limit) return false;
        out->x[d]=v->valuedouble;
    }
    return true;
}
static bool indices(const cJSON *j, size_t minimum, size_t maximum, size_t *out, size_t *count)
{
    if (!cJSON_IsArray(j) || (size_t)cJSON_GetArraySize(j)<minimum || (size_t)cJSON_GetArraySize(j)>maximum) return false;
    size_t n=0;
    for (const cJSON *v=j->child;v;v=v->next) {
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>=VR_MAX_ATOMS ||
            floor(v->valuedouble)!=v->valuedouble) return false;
        out[n++]=(size_t)v->valuedouble;
    }
    for (size_t a=0;a<n;++a) for (size_t b=a+1;b<n;++b) if (out[a]==out[b]) return false;
    *count=n;return true;
}

void config_symmetry_defaults(Config *c)
{
    SymmetryOptions *o=&c->symmetry;memset(o,0,sizeof *o);
    o->show_label=o->verify=true;o->tolerance=0.1;o->axis_radius=0.045;o->padding=0.7;
    o->plane_outline_width=1.5;o->label_scale=1;
    strcpy(o->axis_color,"#7a2f8f");strcpy(o->plane_color,"#f0a63a59");
    strcpy(o->plane_outline_color,"#b8741a");strcpy(o->label_color,"#181c20");
}

/* Accepted names: E, i, Cn, Sn, sigma, sigma_h, sigma_v, sigma_d and the same
 * with a UTF-8 Greek sigma. S1 and S2 keep their label but draw as a mirror
 * plane and an inversion centre; C1 draws as the identity. */
bool config_symmetry_operation(Config *c, const char *text)
{
    SymmetryOptions *o=&c->symmetry;size_t n=strlen(text);
    if (!n || n>=sizeof o->operation) return false;
    SymmetryKind kind=SYMMETRY_NONE;unsigned order=1;
    const char *suffix=NULL;
    if (!strcmp(text,"E") || !strcmp(text,"e")) kind=SYMMETRY_IDENTITY;
    else if (!strcmp(text,"i") || !strcmp(text,"I")) kind=SYMMETRY_INVERSION;
    else if (!strncmp(text,"sigma",5)) {kind=SYMMETRY_MIRROR;suffix=text+5;}
    else if (!strncmp(text,"\xcf\x83",2)) {kind=SYMMETRY_MIRROR;suffix=text+2;}
    else if ((text[0]=='C' || text[0]=='c' || text[0]=='S' || text[0]=='s') && isdigit((unsigned char)text[1])) {
        bool improper=text[0]=='S' || text[0]=='s';size_t value=0;
        if (text[1]=='0' || !integer(text+1,VR_MAX_SYMMETRY_ORDER,&value) || !value) return false;
        order=(unsigned)value;
        kind=improper ? (order==1 ? SYMMETRY_MIRROR : order==2 ? SYMMETRY_INVERSION : SYMMETRY_IMPROPER)
                      : (order==1 ? SYMMETRY_IDENTITY : SYMMETRY_ROTATION);
    } else return false;
    if (suffix) {
        if (*suffix=='_') ++suffix;
        if (*suffix && strcmp(suffix,"h") && strcmp(suffix,"v") && strcmp(suffix,"d")) return false;
    }
    o->kind=kind;o->order=order;memcpy(o->operation,text,n+1);return true;
}

bool config_symmetry(Config *c, const cJSON *object, Error *e)
{
    config_symmetry_defaults(c);
    if (cJSON_IsNull(object)) return true;
    SymmetryOptions *o=&c->symmetry;
    const cJSON *operation=cJSON_IsObject(object) ? cJSON_GetObjectItemCaseSensitive(object,"operation") : NULL;
    if (!cJSON_IsString(operation) || !config_symmetry_operation(c,operation->valuestring))
        return bad(e,"operation","expected E, i, Cn, Sn (n up to 99), sigma, sigma_h, sigma_v or sigma_d");
    bool axis=false,normal=false;
    for (const cJSON *j=object->child;j;j=j->next) {
        const char *k=j->string;
        if (!strcmp(k,"operation")) continue;
        if (!strcmp(k,"axis") || !strcmp(k,"normal")) {
            if (!vector(j,1e6,&o->axis) || length(o->axis)<1e-9) return bad(e,k,"expected three finite numbers forming a nonzero vector");
            o->axis_set=true;if (!strcmp(k,"axis")) axis=true;else normal=true;
        } else if (!strcmp(k,"origin_A")) {
            if (!vector(j,1e6,&o->origin)) return bad(e,k,"expected three coordinates within 1e6 angstroms");
            o->origin_set=true;
        } else if (!strcmp(k,"axis_atoms")) {
            if (!indices(j,2,2,o->axis_atoms,&o->axis_atom_count)) return bad(e,k,"expected two distinct zero-based atom indices");
        } else if (!strcmp(k,"plane_atoms")) {
            if (!indices(j,3,3,o->plane_atoms,&o->plane_atom_count)) return bad(e,k,"expected three distinct zero-based atom indices");
        } else if (!strcmp(k,"origin_atoms")) {
            if (!indices(j,1,VR_MAX_SYMMETRY_ORIGIN_ATOMS,o->origin_atoms,&o->origin_atom_count)) return bad(e,k,"expected 1..64 distinct zero-based atom indices");
        } else if (!strcmp(k,"label")) {
            if (!cJSON_IsString(j) || strlen(j->valuestring)>=sizeof o->label) return bad(e,k,"expected a string of at most 31 bytes");
            strcpy(o->label,j->valuestring);
        } else if (!strcmp(k,"show_label") || !strcmp(k,"verify")) {
            if (!cJSON_IsBool(j)) return bad(e,k,"expected boolean");
            if (!strcmp(k,"verify")) o->verify=cJSON_IsTrue(j);else o->show_label=cJSON_IsTrue(j);
        } else if (!strcmp(k,"tolerance_A")) {
            if (!real(j,0,10,true,&o->tolerance)) return bad(e,k,"expected angstroms in (0,10]");
        } else if (!strcmp(k,"axis_radius_A")) {
            if (!real(j,1e-3,10,false,&o->axis_radius)) return bad(e,k,"expected angstroms in [0.001,10]");
        } else if (!strcmp(k,"padding_A")) {
            if (!real(j,0,100,false,&o->padding)) return bad(e,k,"expected angstroms in [0,100]");
        } else if (!strcmp(k,"plane_outline_width")) {
            if (!real(j,0,8,false,&o->plane_outline_width)) return bad(e,k,"expected pixel width in [0,8]");
        } else if (!strcmp(k,"label_scale")) {
            if (!real(j,0.25,8,false,&o->label_scale)) return bad(e,k,"expected factor in [0.25,8]");
        } else if (!strcmp(k,"axis_style")) {
            if (!cJSON_IsString(j) || (strcmp(j->valuestring,"solid") && strcmp(j->valuestring,"dashed"))) return bad(e,k,"expected solid or dashed");
            o->axis_dashed=!strcmp(j->valuestring,"dashed");
        } else if (!strcmp(k,"label_font")) {
            if (!cJSON_IsString(j) || (strcmp(j->valuestring,"italic") && strcmp(j->valuestring,"sans"))) return bad(e,k,"expected italic or sans");
            o->label_sans=!strcmp(j->valuestring,"sans");
        } else if (!strcmp(k,"plane_shape")) {
            if (!cJSON_IsString(j)) return bad(e,k,"expected auto, rectangle or disc");
            if (!strcmp(j->valuestring,"auto")) o->plane_shape=SYMMETRY_PLANE_AUTO;
            else if (!strcmp(j->valuestring,"rectangle")) o->plane_shape=SYMMETRY_PLANE_RECTANGLE;
            else if (!strcmp(j->valuestring,"disc")) o->plane_shape=SYMMETRY_PLANE_DISC;
            else return bad(e,k,"expected auto, rectangle or disc");
        } else if (!strcmp(k,"axis_color")) {
            if (!color_text(j,true,o->axis_color)) return bad(e,k,"expected #RRGGBB or #RRGGBBAA");
        } else if (!strcmp(k,"plane_color")) {
            if (!color_text(j,true,o->plane_color)) return bad(e,k,"expected #RRGGBB or #RRGGBBAA");
        } else if (!strcmp(k,"plane_outline_color")) {
            if (!color_text(j,true,o->plane_outline_color)) return bad(e,k,"expected #RRGGBB or #RRGGBBAA");
        } else if (!strcmp(k,"label_color")) {
            if (!color_text(j,false,o->label_color)) return bad(e,k,"expected opaque #RRGGBB");
        } else return bad(e,k,"unknown symmetry field");
    }
    if (axis && normal) return bad(e,"normal","axis and normal are the same direction; provide one");
    return true;
}

bool config_symmetry_validate(const Config *c, Error *e)
{
    const SymmetryOptions *o=&c->symmetry;
    if (!o->kind) {
        if (o->axis_set || o->origin_set) return bad(e,"operation","--symmetry-axis and --symmetry-origin require --symmetry OPERATION");
        return true;
    }
    unsigned sources=(unsigned)o->axis_set+(o->axis_atom_count ? 1u : 0u)+(o->plane_atom_count ? 1u : 0u);
    bool directed=o->kind==SYMMETRY_ROTATION || o->kind==SYMMETRY_IMPROPER || o->kind==SYMMETRY_MIRROR;
    if (directed && sources!=1)
        return bad(e,"axis","rotations, improper rotations and mirror planes require exactly one of axis (or normal), axis_atoms or plane_atoms");
    if (o->origin_set && o->origin_atom_count) return bad(e,"origin_atoms","provide origin_A or origin_atoms, not both");
    if (c->backend==BACKEND_CAIRO) return fail_at(e,3,"unsupported_backend","image.backend","symmetry elements require the Z-buffer backend");
    return true;
}

static bool add(cJSON *object, const char *key, cJSON *item)
{
    if (!item) return false;
    if (!cJSON_AddItemToObject(object,key,item)) {cJSON_Delete(item);return false;}
    return true;
}
static cJSON *index_array(const size_t *values, size_t count)
{
    cJSON *a=cJSON_CreateArray();
    for (size_t i=0;a && i<count;++i) {
        cJSON *v=cJSON_CreateNumber((double)values[i]);
        if (!v || !cJSON_AddItemToArray(a,v)) {cJSON_Delete(v);cJSON_Delete(a);return NULL;}
    }
    return a;
}
cJSON *config_symmetry_json(const Config *c)
{
    const SymmetryOptions *o=&c->symmetry;
    if (!o->kind) return NULL;
    cJSON *j=cJSON_CreateObject();
    if (!j) return NULL;
    bool ok=add(j,"operation",cJSON_CreateString(o->operation));
    if (o->axis_set) ok=ok && add(j,"axis",cJSON_CreateDoubleArray(o->axis.x,3));
    if (o->axis_atom_count) ok=ok && add(j,"axis_atoms",index_array(o->axis_atoms,o->axis_atom_count));
    if (o->plane_atom_count) ok=ok && add(j,"plane_atoms",index_array(o->plane_atoms,o->plane_atom_count));
    if (o->origin_set) ok=ok && add(j,"origin_A",cJSON_CreateDoubleArray(o->origin.x,3));
    if (o->origin_atom_count) ok=ok && add(j,"origin_atoms",index_array(o->origin_atoms,o->origin_atom_count));
    if (o->label[0]) ok=ok && add(j,"label",cJSON_CreateString(o->label));
    ok=ok && add(j,"show_label",cJSON_CreateBool(o->show_label)) && add(j,"verify",cJSON_CreateBool(o->verify)) &&
        add(j,"tolerance_A",cJSON_CreateNumber(o->tolerance)) && add(j,"axis_radius_A",cJSON_CreateNumber(o->axis_radius)) &&
        add(j,"axis_style",cJSON_CreateString(o->axis_dashed ? "dashed" : "solid")) &&
        add(j,"axis_color",cJSON_CreateString(o->axis_color)) &&
        add(j,"plane_shape",cJSON_CreateString(o->plane_shape==SYMMETRY_PLANE_DISC ? "disc" : o->plane_shape==SYMMETRY_PLANE_RECTANGLE ? "rectangle" : "auto")) &&
        add(j,"plane_color",cJSON_CreateString(o->plane_color)) &&
        add(j,"plane_outline_color",cJSON_CreateString(o->plane_outline_color)) &&
        add(j,"plane_outline_width",cJSON_CreateNumber(o->plane_outline_width)) &&
        add(j,"padding_A",cJSON_CreateNumber(o->padding)) && add(j,"label_scale",cJSON_CreateNumber(o->label_scale)) &&
        add(j,"label_font",cJSON_CreateString(o->label_sans ? "sans" : "italic")) &&
        add(j,"label_color",cJSON_CreateString(o->label_color));
    if (!ok) {cJSON_Delete(j);return NULL;}
    return j;
}

/* Image of a point under the operation about origin with unit axis u. */
static Vec3 image(SymmetryKind kind, unsigned order, Vec3 origin, Vec3 u, Vec3 point)
{
    Vec3 d=vsub(point,origin);
    if (kind==SYMMETRY_INVERSION) d=vmul(d,-1);
    if (kind==SYMMETRY_ROTATION || kind==SYMMETRY_IMPROPER) {
        double angle=2*pi/(double)order,cs=cos(angle),sn=sin(angle);
        d=vadd(vadd(vmul(d,cs),vmul(cross(u,d),sn)),vmul(u,dot(u,d)*(1-cs)));
    }
    if (kind==SYMMETRY_MIRROR || kind==SYMMETRY_IMPROPER) d=vsub(d,vmul(u,2*dot(u,d)));
    return vadd(origin,d);
}

Vec3 symmetry_pose(const SymmetryMark *mark, double rotation, double reflection, Vec3 point)
{
    Vec3 d=vsub(point,mark->origin),u=mark->axis;
    if ((mark->kind==SYMMETRY_ROTATION || mark->kind==SYMMETRY_IMPROPER) && rotation!=0) {
        double angle=rotation*2*pi/(double)mark->order,cs=cos(angle),sn=sin(angle);
        d=vadd(vadd(vmul(d,cs),vmul(cross(u,d),sn)),vmul(u,dot(u,d)*(1-cs)));
    }
    if (mark->kind==SYMMETRY_MIRROR || mark->kind==SYMMETRY_IMPROPER) d=vsub(d,vmul(u,2*reflection*dot(u,d)));
    if (mark->kind==SYMMETRY_INVERSION) d=vmul(d,1-2*reflection);
    return vadd(mark->origin,d);
}

static void automatic_label(const SymmetryOptions *o, char out[32])
{
    const char *text=o->operation;
    if (!strncmp(text,"sigma",5) || !strncmp(text,"\xcf\x83",2)) {
        const char *suffix=text+(text[0]=='s' ? 5 : 2);
        if (*suffix=='_') ++suffix;
        snprintf(out,32,"\xcf\x83%s%s",*suffix ? "_" : "",suffix);
    } else if (o->kind==SYMMETRY_INVERSION && !isdigit((unsigned char)text[1])) strcpy(out,"i");
    else if (isdigit((unsigned char)text[1])) snprintf(out,32,"%c_%u",toupper((unsigned char)text[0]),o->order);
    else strcpy(out,"E");
}

static bool atom_index(const Structure *s, size_t index, const char *field, Error *e)
{
    return index<s->count || bad(e,field,"index exceeds selected input's atom count");
}

/* Flat two-sided layer: boundary edges carry the outline, internal ones do not. */
static void triangle(Scene *scene, const SymmetryOptions *o, size_t definition, Vec3 a, Vec3 b, Vec3 c, uint8_t edges)
{
    FaceTriangle *t=&scene->faces[scene->triangle_count++];memset(t,0,sizeof *t);
    t->vertices[0]=a;t->vertices[1]=b;t->vertices[2]=c;t->edges=edges;
    decode_color(o->plane_color,t->color);decode_color(o->plane_outline_color,t->outline_color);
    t->outline_width=o->plane_outline_width;t->face=definition;t->instance=scene->face_count;
}

static bool plane_build(const Config *c, const Structure *s, const Vec3 *position, Vec3 origin, Vec3 u,
                        Scene *scene, size_t scratch, Error *e)
{
    const SymmetryOptions *o=&c->symmetry;
    /* Any in-plane frame; then principal axes of the projected atom positions. */
    Vec3 seed=fabs(u.x[0])<0.6 ? (Vec3){{1,0,0}} : (Vec3){{0,1,0}};
    Vec3 e1=unit(cross(u,seed)),e2=cross(u,e1);
    double sxx=0,sxy=0,syy=0;
    for (size_t i=0;i<s->count;++i) {
        Vec3 d=vsub(position[i],origin);double x=dot(d,e1),y=dot(d,e2);
        sxx+=x*x;sxy+=x*y;syy+=y*y;
    }
    double mean=(sxx+syy)/2,spread=hypot((sxx-syy)/2,sxy),major=mean+spread,minor=mean-spread;
    bool isotropic=major<1e-9 || minor>0.85*major;
    if (!isotropic) {
        double angle=0.5*atan2(2*sxy,sxx-syy);
        Vec3 p=vadd(vmul(e1,cos(angle)),vmul(e2,sin(angle)));
        e1=unit(p);e2=cross(u,e1);
    }
    bool disc=o->plane_shape==SYMMETRY_PLANE_DISC || (o->plane_shape==SYMMETRY_PLANE_AUTO && isotropic);
    double lo[2]={0,0},hi[2]={0,0},reach=0;
    for (size_t i=0;i<s->count;++i) {
        Vec3 d=vsub(position[i],origin);double r=element_radius(c,s->atoms[i].element);
        double x=dot(d,e1),y=dot(d,e2);
        lo[0]=fmin(lo[0],x-r);hi[0]=fmax(hi[0],x+r);lo[1]=fmin(lo[1],y-r);hi[1]=fmax(hi[1],y+r);
        reach=fmax(reach,hypot(x,y)+r);
    }
    for (size_t d=0;d<2;++d) {lo[d]-=o->padding;hi[d]+=o->padding;}
    reach+=o->padding;
    if (!isfinite(reach) || reach<1e-6) return bad(e,"padding_A","mirror plane has no extent; increase padding_A");
    size_t count=disc ? SYMMETRY_DISC_SEGMENTS : 2;
    size_t live=scene_memory_bytes(scene)+scratch;
    if (live>VR_MEMORY_BUDGET || scene->triangle_count+count>(VR_MEMORY_BUDGET-live)/sizeof(FaceTriangle))
        return fail(e,6,"symmetry plane exceeds 1 GiB allocation budget");
    FaceTriangle *faces=realloc(scene->faces,(scene->triangle_count+count)*sizeof *faces);
    if (!faces) return fail(e,6,"cannot allocate symmetry plane");
    scene->faces=faces;
    SymmetryMark *mark=&scene->symmetry;bool anchors=!mark->anchor_count;
    if (disc) {
        Vec3 ring[SYMMETRY_DISC_SEGMENTS];
        for (size_t k=0;k<SYMMETRY_DISC_SEGMENTS;++k) {
            double angle=2*pi*(double)k/SYMMETRY_DISC_SEGMENTS;
            ring[k]=vadd(origin,vadd(vmul(e1,reach*cos(angle)),vmul(e2,reach*sin(angle))));
        }
        for (size_t k=0;k<SYMMETRY_DISC_SEGMENTS;++k)
            triangle(scene,o,c->face_count,origin,ring[k],ring[(k+1)%SYMMETRY_DISC_SEGMENTS],2);
        if (anchors) for (size_t k=0;k<VR_SYMMETRY_ANCHORS;++k)
            mark->anchors[mark->anchor_count++]=ring[k*(SYMMETRY_DISC_SEGMENTS/VR_SYMMETRY_ANCHORS)];
    } else {
        Vec3 corner[4];
        for (size_t k=0;k<4;++k) {
            double x=(k==0 || k==3) ? lo[0] : hi[0],y=k<2 ? lo[1] : hi[1];
            corner[k]=vadd(origin,vadd(vmul(e1,x),vmul(e2,y)));
        }
        triangle(scene,o,c->face_count,corner[0],corner[1],corner[2],3);
        triangle(scene,o,c->face_count,corner[0],corner[2],corner[3],6);
        if (anchors) for (size_t k=0;k<4;++k) mark->anchors[mark->anchor_count++]=corner[k];
    }
    ++scene->face_count;
    return true;
}

static bool axis_build(const Config *c, const Structure *s, const Vec3 *position, Vec3 origin, Vec3 u,
                       Scene *scene, size_t scratch, Error *e)
{
    const SymmetryOptions *o=&c->symmetry;double lo=0,hi=0,size=0;
    for (size_t i=0;i<s->count;++i) {
        Vec3 d=vsub(position[i],origin);
        double t=dot(d,u),r=element_radius(c,s->atoms[i].element);
        lo=fmin(lo,t-r);hi=fmax(hi,t+r);size=fmax(size,length(d)+r);
    }
    /* An axis normal to a flat molecule would otherwise be a stub hidden among
     * the atoms: give each half at least three quarters of the molecular radius. */
    lo=fmin(lo,-0.75*size);hi=fmax(hi,0.75*size);
    double reach=fmax(o->padding,0.25)+0.35;
    lo-=reach;hi+=reach;
    Primitive p={0};p.kind=SYMMETRY_AXIS;p.radius=o->axis_radius;decode_color(o->axis_color,p.color);
    const double dash=0.32,gap=0.20;
    size_t pieces=o->axis_dashed ? (size_t)fmin(4096,ceil((hi-lo+gap)/(dash+gap))) : 1;
    /* Centre the dash pattern so both tips end with ink. */
    double pattern=(double)pieces*dash+(double)(pieces-1)*gap,start=o->axis_dashed ? (lo+hi-pattern)/2 : lo;
    for (size_t k=0;k<pieces;++k) {
        double a=o->axis_dashed ? start+(double)k*(dash+gap) : lo,b=o->axis_dashed ? a+dash : hi;
        p.a=vadd(origin,vmul(u,a));p.b=vadd(origin,vmul(u,b));
        if (!scene_append(scene,p,scratch,e)) return false;
    }
    SymmetryMark *mark=&scene->symmetry;
    mark->anchors[0]=vadd(origin,vmul(u,o->axis_dashed ? start : lo));
    mark->anchors[1]=vadd(origin,vmul(u,o->axis_dashed ? start+pattern : hi));mark->anchor_count=2;
    return true;
}

bool scene_symmetry_build(const Config *c, const Structure *s, const Vec3 *fractional,
                          const Vec3 basis[3], Scene *scene, size_t scratch, Error *e)
{
    const SymmetryOptions *o=&c->symmetry;
    if (!o->kind) return true;
    if (!s->count) return bad(e,"operation","a symmetry operation requires at least one atom");
    for (size_t k=0;k<o->axis_atom_count;++k) if (!atom_index(s,o->axis_atoms[k],"axis_atoms",e)) return false;
    for (size_t k=0;k<o->plane_atom_count;++k) if (!atom_index(s,o->plane_atoms[k],"plane_atoms",e)) return false;
    for (size_t k=0;k<o->origin_atom_count;++k) if (!atom_index(s,o->origin_atoms[k],"origin_atoms",e)) return false;
    if (scratch>VR_MEMORY_BUDGET || s->count>(VR_MEMORY_BUDGET-scratch)/sizeof(Vec3))
        return fail(e,6,"symmetry positions exceed 1 GiB allocation budget");
    Vec3 *position=malloc(s->count*sizeof *position);
    if (!position) return fail(e,6,"cannot allocate symmetry positions");
    bool ok=false;
    Vec3 centroid={{0,0,0}};
    for (size_t i=0;i<s->count;++i) {position[i]=cartesian(basis,fractional[i]);centroid=vadd(centroid,position[i]);}
    centroid=vmul(centroid,1/(double)s->count);
    SymmetryMark *mark=&scene->symmetry;memset(mark,0,sizeof *mark);
    mark->order=o->order;mark->deviation=-1;
    bool directed=o->kind==SYMMETRY_ROTATION || o->kind==SYMMETRY_IMPROPER || o->kind==SYMMETRY_MIRROR;
    Vec3 u={{0,0,1}},origin=centroid,through=centroid;bool anchored=false;
    if (directed) {
        if (o->axis_atom_count) {
            u=vsub(position[o->axis_atoms[1]],position[o->axis_atoms[0]]);
            through=position[o->axis_atoms[0]];anchored=true;
        } else if (o->plane_atom_count) {
            Vec3 a=position[o->plane_atoms[0]];
            u=cross(vsub(position[o->plane_atoms[1]],a),vsub(position[o->plane_atoms[2]],a));
            through=a;anchored=o->kind!=SYMMETRY_ROTATION;
        } else u=o->axis;
        double norm=length(u);
        if (!isfinite(norm) || norm<1e-9) {
            bad(e,o->axis_atom_count ? "axis_atoms" : o->plane_atom_count ? "plane_atoms" : "axis",
                "selected atoms coincide or are collinear; the direction is undefined");goto done;
        }
        u=vmul(u,1/norm);
    }
    if (o->origin_set) origin=o->origin;
    else if (o->origin_atom_count) {
        origin=(Vec3){{0,0,0}};
        for (size_t k=0;k<o->origin_atom_count;++k) origin=vadd(origin,position[o->origin_atoms[k]]);
        origin=vmul(origin,1/(double)o->origin_atom_count);
    } else if (anchored) {
        /* Nearest point to the centroid on the atom-defined line or plane. */
        Vec3 d=vsub(centroid,through);
        origin=o->axis_atom_count ? vadd(through,vmul(u,dot(d,u))) : vsub(centroid,vmul(u,dot(d,u)));
    }
    mark->kind=o->kind;mark->origin=origin;mark->axis=directed ? u : (Vec3){{0,0,0}};
    if (o->label[0]) strcpy(mark->label,o->label);else automatic_label(o,mark->label);
    if (o->verify && o->kind!=SYMMETRY_IDENTITY) {
        if (s->count>SYMMETRY_VERIFY_WORK/s->count) {
            bad(e,"verify","structure is too large to verify; set verify false to draw the element anyway");goto done;
        }
        double worst=0;size_t worst_atom=0;
        for (size_t i=0;i<s->count;++i) {
            if (!(i%256) && !check_cancel(e)) goto done;
            Vec3 target=image(o->kind,o->order,origin,u,position[i]);double best=DBL_MAX;
            for (size_t j=0;j<s->count;++j) if (s->atoms[j].element==s->atoms[i].element)
                best=fmin(best,length(vsub(position[j],target)));
            if (best>worst) {worst=best;worst_atom=i;}
        }
        mark->deviation=worst;mark->deviation_atom=worst_atom;
        if (!(worst<=o->tolerance)) {
            fail_at(e,5,"symmetry_mismatch","symmetry",
                    "symmetry: %s does not map the structure onto itself: atom %zu (%s) lands %.3f A from the nearest "
                    "like atom (tolerance_A %.3g). Check the axis/origin, raise tolerance_A, or set verify false",
                    o->operation,worst_atom,element_symbol(s->atoms[worst_atom].element),worst,o->tolerance);
            goto done;
        }
    }
    if ((o->kind==SYMMETRY_ROTATION || o->kind==SYMMETRY_IMPROPER) && !axis_build(c,s,position,origin,u,scene,scratch,e)) goto done;
    if ((o->kind==SYMMETRY_MIRROR || o->kind==SYMMETRY_IMPROPER) && !plane_build(c,s,position,origin,u,scene,scratch,e)) goto done;
    ok=true;
done:
    free(position);
    if (!ok) memset(mark,0,sizeof *mark);
    return ok;
}
