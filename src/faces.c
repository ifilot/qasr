/* SPDX-License-Identifier: GPL-3.0-only */
#include "scene.h"
#include "cJSON.h"
#include "vec.h"
#include <ctype.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

static bool bad(Error *e, size_t face, const char *field, const char *message)
{
    char path[128];snprintf(path,sizeof path,"faces[%zu]%s%s",face,*field ? "." : "",field);
    return fail_at(e,2,"invalid_face",path,"%s: %s",path,message);
}
static bool rgba(const cJSON *j, uint8_t out[4])
{
    if (!cJSON_IsString(j)) return false;
    size_t n=strlen(j->valuestring);
    if ((n!=7 && n!=9) || j->valuestring[0]!='#') return false;
    for (size_t k=1;k<n;++k) if (!isxdigit((unsigned char)j->valuestring[k])) return false;
    decode_color(j->valuestring,out);return true;
}
void config_faces_free(Config *c)
{
    for (size_t i=0;c->faces && i<c->face_count;++i) {
        free(c->faces[i].vertices);free(c->faces[i].indices);free(c->faces[i].offsets);
    }
    free(c->faces);c->faces=NULL;c->face_count=0;cJSON_Delete(c->faces_json);c->faces_json=NULL;
}
bool config_faces(Config *c, const cJSON *array, Error *e)
{
    if (!cJSON_IsArray(array) || cJSON_GetArraySize(array)>(int)VR_MAX_FACES)
        return fail_at(e,2,"invalid_faces","faces","faces must be an array of at most 4096 polygons");
    config_faces_free(c);
    c->face_count=(size_t)cJSON_GetArraySize(array);
    c->faces=calloc(c->face_count ? c->face_count : 1,sizeof *c->faces);
    c->faces_json=cJSON_Duplicate(array,1);
    if (!c->faces || !c->faces_json) {config_faces_free(c);return fail(e,6,"cannot allocate polygon definitions");}
    size_t at=0,total=0;
    for (const cJSON *j=array->child;j;j=j->next,++at) {
        if (!cJSON_IsObject(j)) return bad(e,at,"","expected polygon object");
        FaceDefinition *f=&c->faces[at];f->repeat=true;f->outline_width=0;
        decode_color("#43a6cf80",f->color);decode_color("#23556e",f->outline_color);
        const cJSON *vertices=cJSON_GetObjectItemCaseSensitive(j,"vertices"),
                    *indices=cJSON_GetObjectItemCaseSensitive(j,"atom_indices"),
                    *offsets=cJSON_GetObjectItemCaseSensitive(j,"cell_offsets");
        if (!!vertices==!!indices) return bad(e,at,"","provide vertices or atom_indices, exclusively");
        const cJSON *points=vertices ? vertices : indices;
        if (!cJSON_IsArray(points) || cJSON_GetArraySize(points)<3 || cJSON_GetArraySize(points)>(int)VR_MAX_FACE_VERTICES)
            return bad(e,at,"","each polygon requires 3..64 ordered vertices");
        f->count=(size_t)cJSON_GetArraySize(points);total+=f->count;
        if (total>65536) return fail_at(e,6,"face_vertex_limit","faces","faces exceed 65536 input vertices");
        if (vertices) f->vertices=calloc(f->count,sizeof *f->vertices);
        else {f->indices=calloc(f->count,sizeof *f->indices);f->offsets=calloc(f->count,sizeof *f->offsets);}
        if ((vertices && !f->vertices) || (indices && (!f->indices || !f->offsets))) return fail(e,6,"cannot allocate polygon vertices");
        for (const cJSON *v=j->child;v;v=v->next) {
            const char *k=v->string;
            if (!strcmp(k,"vertices") || !strcmp(k,"atom_indices") || !strcmp(k,"cell_offsets")) continue;
            if (!strcmp(k,"color")) {if (!rgba(v,f->color)) return bad(e,at,k,"expected #RRGGBB or #RRGGBBAA");}
            else if (!strcmp(k,"outline_color")) {if (!rgba(v,f->outline_color)) return bad(e,at,k,"expected #RRGGBB or #RRGGBBAA");}
            else if (!strcmp(k,"outline_width")) {
                if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>8) return bad(e,at,k,"expected pixel width in [0,8]");
                f->outline_width=v->valuedouble;
            } else if (!strcmp(k,"repeat")) {
                if (!cJSON_IsBool(v)) return bad(e,at,k,"expected boolean");
                f->repeat=cJSON_IsTrue(v);
            } else if (!strcmp(k,"coordinates")) {
                if (!vertices || !cJSON_IsString(v) || (strcmp(v->valuestring,"cartesian") && strcmp(v->valuestring,"fractional")))
                    return bad(e,at,k,"coordinates requires vertices and cartesian or fractional");
                f->fractional=!strcmp(v->valuestring,"fractional");
            } else return bad(e,at,k,"unknown polygon field");
        }
        if (offsets && (!indices || !cJSON_IsArray(offsets) || (size_t)cJSON_GetArraySize(offsets)!=f->count))
            return bad(e,at,"cell_offsets","one three-integer lattice offset is required per atom index");
        size_t i=0;
        for (const cJSON *v=points->child;v;v=v->next,++i) {
            if (vertices) {
                if (!cJSON_IsArray(v) || cJSON_GetArraySize(v)!=3) return bad(e,at,"vertices","expected three finite coordinates per vertex");
                size_t d=0;for (const cJSON *x=v->child;x;x=x->next,++d) {
                    if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble) || fabs(x->valuedouble)>1e12) return bad(e,at,"vertices","coordinates must be finite and within ±1e12");
                    f->vertices[i].x[d]=x->valuedouble;
                }
            } else {
                if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<0 || v->valuedouble>=VR_MAX_ATOMS || floor(v->valuedouble)!=v->valuedouble)
                    return bad(e,at,"atom_indices","expected zero-based atom indices");
                f->indices[i]=(size_t)v->valuedouble;
            }
        }
        i=0;for (const cJSON *v=offsets ? offsets->child : NULL;v;v=v->next,++i) {
            if (!cJSON_IsArray(v) || cJSON_GetArraySize(v)!=3) return bad(e,at,"cell_offsets","expected three integers per offset");
            size_t d=0;for (const cJSON *x=v->child;x;x=x->next,++d) {
                if (!cJSON_IsNumber(x) || !isfinite(x->valuedouble) || fabs(x->valuedouble)>1024 || floor(x->valuedouble)!=x->valuedouble)
                    return bad(e,at,"cell_offsets","offsets must be integers in [-1024,1024]");
                f->offsets[i][d]=(int)x->valuedouble;
            }
        }
    }
    return true;
}

typedef struct {double x,y;} Point;
static double turn(Point a, Point b, Point c) {return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
static bool on_segment(Point a, Point b, Point p, double eps)
{
    return fabs(turn(a,b,p))<=eps && p.x>=fmin(a.x,b.x)-eps && p.x<=fmax(a.x,b.x)+eps &&
           p.y>=fmin(a.y,b.y)-eps && p.y<=fmax(a.y,b.y)+eps;
}
/* Ear clipping accepts concave loops and either winding. Projection selects
 * the dominant Newell normal; original 3D vertices remain unchanged. */
static bool triangulate(const Vec3 *v, size_t n, size_t triangles[VR_MAX_FACE_VERTICES-2][3], size_t *count, size_t face, Error *e)
{
    Vec3 normal={{0,0,0}}, base=v[0];double extent=0;
    for (size_t i=0;i<n;++i) {
        Vec3 a=vsub(v[i],base),b=vsub(v[(i+1)%n],base);
        normal=vadd(normal,cross(a,b));extent=fmax(extent,length(a));
    }
    double norm=length(normal);
    if (!isfinite(norm) || extent<1e-12 || norm<1e-12*extent*extent) return bad(e,face,"vertices","degenerate polygon or inconsistent/self-intersecting boundary");
    size_t drop=0;for (size_t d=1;d<3;++d) if (fabs(normal.x[d])>fabs(normal.x[drop])) drop=d;
    Point p[VR_MAX_FACE_VERTICES];size_t ring[VR_MAX_FACE_VERTICES];
    for (size_t i=0;i<n;++i) {
        Vec3 a=vmul(vsub(v[i],base),1/extent);
        p[i]=(Point){a.x[(drop+1)%3],a.x[(drop+2)%3]};ring[i]=i;
    }
    const double eps=1e-12;
    for (size_t i=0;i<n;++i) {
        size_t next=(i+1)%n;
        if (hypot(p[i].x-p[next].x,p[i].y-p[next].y)<eps) return bad(e,face,"vertices","repeated adjacent vertices or edge parallel to polygon projection");
        for (size_t j=i+1;j<n;++j) {
            size_t end=(j+1)%n;if (j==next || end==i) continue;
            double a=turn(p[i],p[next],p[j]),b=turn(p[i],p[next],p[end]),
                   c=turn(p[j],p[end],p[i]),d=turn(p[j],p[end],p[next]);
            if (((a>eps && b< -eps) || (a< -eps && b>eps)) && ((c>eps && d< -eps) || (c< -eps && d>eps)))
                return bad(e,face,"vertices","self-intersecting polygon boundary");
            if (on_segment(p[i],p[next],p[j],eps) || on_segment(p[i],p[next],p[end],eps) ||
                on_segment(p[j],p[end],p[i],eps) || on_segment(p[j],p[end],p[next],eps))
                return bad(e,face,"vertices","touching or overlapping polygon edges");
        }
    }
    double area=0;for (size_t i=0;i<n;++i) area+=p[i].x*p[(i+1)%n].y-p[(i+1)%n].x*p[i].y;
    double sign=area>0 ? 1 : -1;size_t remaining=n;*count=0;
    while (remaining>3) {
        bool found=false;
        for (size_t i=0;i<remaining;++i) {
            size_t a=ring[(i+remaining-1)%remaining],b=ring[i],c=ring[(i+1)%remaining];
            if (sign*turn(p[a],p[b],p[c])<=eps) continue;
            bool occupied=false;
            for (size_t k=0;k<remaining;++k) {
                size_t q=ring[k];if (q==a || q==b || q==c) continue;
                if (sign*turn(p[a],p[b],p[q])>= -eps && sign*turn(p[b],p[c],p[q])>= -eps && sign*turn(p[c],p[a],p[q])>= -eps) {occupied=true;break;}
            }
            if (occupied) continue;
            triangles[*count][0]=a;triangles[*count][1]=b;triangles[(*count)++][2]=c;
            memmove(ring+i,ring+i+1,(remaining-i-1)*sizeof *ring);--remaining;found=true;break;
        }
        if (!found) return bad(e,face,"vertices","polygon cannot be triangulated; remove collinear or folded boundary vertices");
    }
    if (sign*turn(p[ring[0]],p[ring[1]],p[ring[2]])<=eps) return bad(e,face,"vertices","degenerate final triangle");
    memcpy(triangles[(*count)++],ring,3*sizeof *ring);return true;
}

bool scene_faces_build(const Config *c, const Structure *s, const Vec3 *fractional,
                       const Vec3 basis[3], Scene *scene, size_t scratch, Error *e)
{
    size_t total=0;
    for (size_t i=0;i<c->face_count;++i) {
        const FaceDefinition *f=&c->faces[i];size_t copies=f->repeat ? c->repeat[0]*c->repeat[1]*c->repeat[2] : 1;
        size_t add=(f->count-2)*copies;
        if (add>262144u-total) return fail_at(e,6,"face_triangle_limit","faces","expanded polygons exceed 262144 triangles");
        total+=add;
    }
    if (!total) return true;
    size_t retained=scene_memory_bytes(scene)+scratch;
    if (retained>VR_MEMORY_BUDGET || total>(VR_MEMORY_BUDGET-retained)/sizeof(FaceTriangle)) return fail(e,6,"polygon scene exceeds 1 GiB allocation budget");
    scene->faces=calloc(total,sizeof *scene->faces);
    if (!scene->faces) return fail(e,6,"cannot allocate polygon scene");
    for (size_t i=0;i<c->face_count;++i) {
        if (!check_cancel(e)) return false;
        const FaceDefinition *f=&c->faces[i];Vec3 v[VR_MAX_FACE_VERTICES];
        if (f->fractional && !(s->cell_present[0] && s->cell_present[1] && s->cell_present[2]))
            return bad(e,i,"coordinates","fractional vertices require a complete three-dimensional cell");
        for (size_t k=0;k<f->count;++k) {
            if (f->indices) {
                if (f->indices[k]>=s->count) return bad(e,i,"atom_indices","index exceeds selected input's atom count");
                Vec3 offset={{0,0,0}};
                for (size_t d=0;d<3;++d) {
                    if (f->offsets[k][d] && !s->pbc[d]) return bad(e,i,"cell_offsets","nonzero offset in a nonperiodic direction");
                    offset.x[d]=f->offsets[k][d];
                }
                v[k]=cartesian(basis,vadd(fractional[f->indices[k]],offset));
            } else v[k]=f->fractional ? cartesian(basis,f->vertices[k]) : f->vertices[k];
            if (!isfinite(length(v[k])) || length(v[k])>1e12) return bad(e,i,"vertices","resolved vertex exceeds coordinate limit");
        }
        size_t triangles[VR_MAX_FACE_VERTICES-2][3],nt=0;
        if (!triangulate(v,f->count,triangles,&nt,i,e)) return false;
        int lower[3]={0,0,0},upper[3]={1,1,1};
        if (f->repeat) for (size_t d=0;d<3;++d) {lower[d]=d<2 ? -(int)c->expand_xy[d] : 0;upper[d]=lower[d]+(int)c->repeat[d];}
        for (int x=lower[0];x<upper[0];++x) for (int y=lower[1];y<upper[1];++y) for (int z=lower[2];z<upper[2];++z) {
            if (!check_cancel(e)) return false;
            Vec3 shift=cartesian(basis,(Vec3){{x,y,z}});
            for (size_t t=0;t<nt;++t) {
                FaceTriangle *p=&scene->faces[scene->triangle_count++];
                p->face=i;p->instance=scene->face_count;p->origin[0]=x;p->origin[1]=y;p->origin[2]=z;
                memcpy(p->color,f->color,4);memcpy(p->outline_color,f->outline_color,4);p->outline_width=f->outline_width;
                for (size_t k=0;k<3;++k) {
                    size_t a=triangles[t][k],b=triangles[t][(k+1)%3];
                    p->vertices[k]=vadd(v[a],shift);
                    if ((a+1)%f->count==b || (b+1)%f->count==a) p->edges|=(uint8_t)(1u<<k);
                }
            }
            ++scene->face_count;
        }
    }
    return true;
}
