/* SPDX-License-Identifier: GPL-3.0-only */
#include "scene.h"
#include "overlay.h"
#include "vec.h"
#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool append(Scene *s, Primitive p, size_t scratch, Error *e)
{
    if (s->count == s->capacity) {
        size_t next = s->capacity ? s->capacity + s->capacity/2 : 256;
        if (next > (VR_MEMORY_BUDGET - scratch) / sizeof p ||
            s->capacity > (VR_MEMORY_BUDGET - scratch) / sizeof p - next)
            return fail(e, 6, "scene growth exceeds 1 GiB allocation budget");
        Primitive *items = realloc(s->items, next * sizeof p);
        if (!items) return fail(e, 6, "scene allocation failed");
        s->items = items; s->capacity = next;
    }
    s->items[s->count++] = p;
    return true;
}

/* Liang-Barsky clipping in fractional coordinates handles any skewed basis. */
static bool clip(Vec3 a, Vec3 b, const Config *c, double *low, double *high)
{
    *low = 0; *high = 1;
    for (int d = 0; d < 3; ++d) {
        double lower = d < 2 ? -(double)c->expand_xy[d] : 0;
        double upper = lower + (double)c->repeat[d];
        double delta = b.x[d] - a.x[d];
        if (delta == 0) { if (a.x[d] < lower || a.x[d] >= upper) return false; }
        else {
            double x = (lower-a.x[d]) / delta, y = (upper-a.x[d]) / delta;
            if (x > y) { double temp = x; x = y; y = temp; }
            *low = fmax(*low, x); *high = fmin(*high, y);
            if (*low >= *high) return false;
        }
    }
    return *high - *low > 1e-12;
}

static bool image_bounds(Vec3 delta, double cutoff, const Vec3 reciprocal[3], int lo[3], int hi[3], size_t *work, Error *e)
{
    size_t product = 1;
    for (int d = 0; d < 3; ++d) {
        double bound = cutoff * length(reciprocal[d]);
        double padding=1e-10+64*DBL_EPSILON*fmax(1,fabs(delta.x[d])+bound);
        double a = ceil(-delta.x[d] - bound - padding), b = floor(-delta.x[d] + bound + padding);
        if (!isfinite(a) || !isfinite(b) || a < -1000000 || b > 1000000)
            return fail(e, 6, "periodic image bounds exceed supported enumeration range");
        lo[d] = (int)a; hi[d] = (int)b;
        size_t n = b >= a ? (size_t)(hi[d] - lo[d] + 1) : 0;
        if (n && product > VR_MAX_CANDIDATES / n) return fail(e, 6, "periodic candidate budget exceeded");
        product *= n;
    }
    if (*work > VR_MAX_CANDIDATES - product) return fail(e, 6, "periodic candidate budget exceeded");
    *work += product;
    return true;
}

static bool expand_bond(const Config *c, const Structure *s, Scene *scene, Vec3 fa, Vec3 fb,
                        size_t i, size_t j, const int image[3], double distance, size_t scratch,
                        size_t *work, Error *e)
{
    int lo[3], hi[3];
    size_t product = 1;
    for (int d = 0; d < 3; ++d) {
        double lower = d < 2 ? -(double)c->expand_xy[d] : 0;
        lo[d] = (int)ceil(lower-fmax(fa.x[d], fb.x[d]));
        hi[d] = (int)floor(lower+(double)c->repeat[d] - fmin(fa.x[d], fb.x[d]));
        size_t n = (size_t)(hi[d] - lo[d] + 1);
        if (product > VR_MAX_CANDIDATES / n) return fail(e, 6, "bond expansion work limit exceeded");
        product *= n;
    }
    if (*work > VR_MAX_CANDIDATES - product) return fail(e, 6, "bond expansion work limit exceeded");
    *work += product;
    for (int x = lo[0]; x <= hi[0]; ++x) for (int y = lo[1]; y <= hi[1]; ++y) for (int z = lo[2]; z <= hi[2]; ++z) {
        Vec3 origin = {{x,y,z}}, a = vadd(fa,origin), b = vadd(fb,origin);
        double low, high;
        if (!clip(a, b, c, &low, &high)) continue;
        Vec3 delta = vsub(b,a);
        Primitive p = {0};
        p.a = cartesian(s->lattice, vadd(a,vmul(delta,low)));
        p.b = cartesian(s->lattice, vadd(a,vmul(delta,high)));
        if (length(vsub(p.b,p.a)) < 1e-12) continue;
        if (scene->bond_count == VR_MAX_BONDS) return fail(e, 6, "scene exceeds bond limit");
        p.radius = c->bond_radius; p.kind = CYLINDER;
        p.color[0] = p.color[1] = p.color[2] = 160; p.color[3] = 255;
        p.source_a = i; p.source_b = j; p.distance = distance;
        memcpy(p.image,image,sizeof p.image);
        p.origin[0]=x; p.origin[1]=y; p.origin[2]=z;
        if (!append(scene,p,scratch,e)) return false;
        ++scene->bond_count;
    }
    return true;
}

/* Transform only RGB: periodic context stays an opaque depth-tested surface,
 * with the element's original alpha rather than artificial transparency. */
static void image_color(PeriodicImageStyle style, const uint8_t base[4], uint8_t out[4])
{
    memcpy(out, base, 4);
    unsigned grey = (54u*base[0]+183u*base[1]+19u*base[2]+128u)/256u;
    for (size_t d = 0; d < 3; ++d) {
        if (style == IMAGES_MUTED) out[d] = (uint8_t)(((unsigned)base[d]+3u*grey+4u*255u+4u)/8u);
        else if (style == IMAGES_GREYSCALE) out[d] = (uint8_t)((3u*grey+255u+2u)/4u);
    }
}

bool scene_build(const Config *c, const Structure *s, Scene *scene, Error *e)
{
    Vec3 reciprocal[3];
    double volume = cell_volume(s->lattice);
    reciprocal[0] = vmul(cross(s->lattice[1],s->lattice[2]),1/volume);
    reciprocal[1] = vmul(cross(s->lattice[2],s->lattice[0]),1/volume);
    reciprocal[2] = vmul(cross(s->lattice[0],s->lattice[1]),1/volume);
    size_t scratch = s->count * (sizeof(Vec3) + sizeof(Atom)) + config_memory_bytes(c);
    AtomStyle *styles=NULL;
    Vec3 *fractional = malloc(s->count * sizeof *fractional);
    if (!fractional) return fail(e,6,"cannot allocate fractional positions");
    bool ok = false;
    if (!atom_styles_resolve(c,s,&styles,e)) goto done;
    if (styles) {
        size_t instances=s->count*c->repeat[0]*c->repeat[1]*c->repeat[2];
        scratch+=s->count*sizeof(AtomStyle)+instances*sizeof(RuleStamp);
        if (scratch>VR_MEMORY_BUDGET) {fail(e,6,"atom styles exceed aggregate allocation budget");goto done;}
        scene->style_rules=calloc(instances,sizeof *scene->style_rules);
        if (!scene->style_rules) {fail(e,6,"cannot allocate instance rule metadata");goto done;}
    }
    if (!isfinite(c->bond_radius) || c->bond_radius < 1e-9 || c->bond_radius > 1e6) {
        fail(e,5,"bond radius must be within [1e-9,1e6] angstroms"); goto done;
    }
    for (int d = 0; d < 3; ++d) scene->supercell[d] = vmul(s->lattice[d],(double)c->repeat[d]);
    for (size_t i = 0; i < s->count; ++i) {
        if (!check_cancel(e)) goto done;
        for (int d = 0; d < 3; ++d) {
            double f = dot(s->atoms[i].position,reciprocal[d]);
            if (!isfinite(f) || fabs(f) > 1e12) { fail(e,5,"fractional position exceeds precision limit"); goto done; }
            fractional[i].x[d] = f - floor(f);
        }
        Primitive p = {0};
        p.kind = SPHERE; p.element=s->atoms[i].element;
        p.radius = element_radius(c,s->atoms[i].element); p.source_a = i;
        if (!isfinite(p.radius) || p.radius < 1e-9 || p.radius > 1e6) { fail(e,5,"sphere radius must be within [1e-9,1e6] angstroms"); goto done; }
        AtomStyle base={.radius=p.radius,.rules={SIZE_MAX,SIZE_MAX}};
        element_color(c,s->atoms[i].element,base.color);
        if (styles) base=styles[i];
        int lx = -(int)c->expand_xy[0], ly = -(int)c->expand_xy[1];
        for (int x = lx; x < lx+(int)c->repeat[0]; ++x) for (int y = ly; y < ly+(int)c->repeat[1]; ++y) for (int z = 0; z < (int)c->repeat[2]; ++z) {
            Vec3 f = vadd(fractional[i],(Vec3){{(double)x,(double)y,(double)z}});
            p.a = p.b = cartesian(s->lattice,f);
            p.origin[0]=(int)x; p.origin[1]=(int)y; p.origin[2]=(int)z;
            AtomStyle instance=base;
            image_color(x || y || z ? c->image_colors : IMAGES_NORMAL, base.color, instance.color);
            if (c->cell_rule_count) atom_style_instance(c,i,p.element,p.origin,&instance);
            p.radius=instance.radius;memcpy(p.color,instance.color,4);
            if (!append(scene,p,scratch,e)) goto done;
            if (scene->style_rules) scene->style_rules[scene->atom_count]=instance.rules;
            ++scene->atom_count;
        }
    }
    size_t work = 0, expansion_work = 0;
    for (size_t i = 0; i < s->count; ++i) for (size_t j = i; j < s->count; ++j) {
        /* Account for pairs even when reciprocal bounds contain no integer image. */
        if (!(work%4096) && !check_cancel(e)) goto done;
        if (++work > VR_MAX_CANDIDATES) { fail(e,6,"pair search work budget exceeded"); goto done; }
        double cutoff = bond_cutoff(c,s->atoms[i].element,s->atoms[j].element);
        if (!isfinite(cutoff) || cutoff > 1e6) { fail(e,5,"bond cutoff exceeds 1e6 angstroms"); goto done; }
        Vec3 delta = vsub(fractional[j],fractional[i]);
        int lo[3], hi[3];
        double threshold=cutoff+32*DBL_EPSILON*fmax(1,cutoff);
        if (!image_bounds(delta,threshold,reciprocal,lo,hi,&work,e)) goto done;
        for (int x = lo[0]; x <= hi[0]; ++x) for (int y = lo[1]; y <= hi[1]; ++y) for (int z = lo[2]; z <= hi[2]; ++z) {
            if (i == j && !(x > 0 || (x == 0 && y > 0) || (x == 0 && y == 0 && z > 0))) continue;
            Vec3 shift = {{x,y,z}};
            double distance = length(cartesian(s->lattice,vadd(delta,shift)));
            /* Decimal inputs exactly at a cutoff may round to either side after
             * coordinate conversion. Keep a small, documented double-precision tolerance. */
            if (distance == 0 || distance > threshold) continue;
            ++scene->base_bond_count;
            int image[3] = {x,y,z};
            if (!expand_bond(c,s,scene,fractional[i],vadd(fractional[j],shift),i,j,image,distance,scratch,&expansion_work,e)) goto done;
        }
    }
    if (c->cell) {
        bool primary = c->expand_xy[0] || c->expand_xy[1] ||
                       c->image_colors == IMAGES_MUTED || c->image_colors == IMAGES_GREYSCALE;
        for (int axis = 0; axis < 3; ++axis) for (int mask = 0; mask < 4; ++mask) {
            Vec3 a = {{0,0,0}};
            int bit = 0;
            for (int d = 0; d < 3; ++d) if (d != axis) {
                if (mask & (1 << bit)) a = vadd(a,primary ? s->lattice[d] : scene->supercell[d]);
                ++bit;
            }
            Primitive p = {0}; p.kind = CELL_EDGE; p.a = a; p.b = vadd(a,primary ? s->lattice[axis] : scene->supercell[axis]);
            p.color[0] = p.color[1] = p.color[2] = 70; p.color[3] = 255;
            if (!append(scene,p,scratch,e)) goto done;
        }
    }
    ok = true;
done:
    free(fractional);free(styles);
    if (!ok) scene_free(scene);
    return ok;
}

size_t scene_memory_bytes(const Scene *s) {return s->capacity*sizeof(Primitive)+(s->style_rules ? s->atom_count*sizeof(RuleStamp) : 0);}
void scene_free(Scene *s) { free(s->items);free(s->style_rules); memset(s,0,sizeof *s); }

Vec3 camera_project(const Camera *c, Vec3 p)
{
    return (Vec3){{(dot(p,c->right)-c->center_x)*c->scale + (double)c->width/2,
                   (dot(p,c->up)-c->center_y)*c->scale + (double)c->height/2,
                   dot(p,c->view)}};
}

bool camera_fit(const Config *c, const Scene *s, Camera *camera, Error *e)
{
    Vec3 view=c->view, up=c->up;
    if (c->view_top) {
        view=unit(cross(s->supercell[0],s->supercell[1]));
        if (dot(view,s->supercell[2])<0) view=vmul(view,-1);
        if (!c->up_explicit) up=cross(view,unit(s->supercell[0]));
    }
    camera->view = unit(view);
    Vec3 right=cross(unit(up),camera->view);
    if (length(right)<1e-12) return fail(e,2,"up is collinear with the surface-normal top view");
    camera->right = unit(right);
    camera->up = cross(camera->view,camera->right);
    camera->width = c->width; camera->height = c->height;
    if (!overlay_layout(c,s,camera,e)) return false;
    double lo[2] = {DBL_MAX,DBL_MAX}, hi[2] = {-DBL_MAX,-DBL_MAX};
    for (size_t i = 0; i < s->count; ++i) {
        const Primitive *p = &s->items[i];
        double ax = dot(p->a,camera->right), bx = dot(p->b,camera->right);
        double ay = dot(p->a,camera->up), by = dot(p->b,camera->up);
        lo[0]=fmin(lo[0],fmin(ax,bx)-p->radius); hi[0]=fmax(hi[0],fmax(ax,bx)+p->radius);
        lo[1]=fmin(lo[1],fmin(ay,by)-p->radius); hi[1]=fmax(hi[1],fmax(ay,by)+p->radius);
    }
    double width = hi[0]-lo[0], height = hi[1]-lo[1];
    if (!isfinite(width) || !isfinite(height) || width <= 0 || height <= 0)
        return fail(e,5,"scene has invalid projected bounds");
    camera->center_x = lo[0]+width/2; camera->center_y = lo[1]+height/2;
    /* Reserve the full line width plus an AA guard even with a zero margin. */
    double guard=c->cell ? c->cell_width+1 : 2;
    double usable_x = fmax((double)camera->viewport_width*(1-2*c->margin)-guard,0.5);
    double usable_y = fmax((double)camera->viewport_height*(1-2*c->margin)-guard,0.5);
    camera->scale = fmin(usable_x/width,usable_y/height);
    if (!isfinite(camera->scale) || camera->scale<=0) return fail(e,5,"unrepresentable camera scale");
    if (c->labels || c->axes || c->legend || c->scale_bar) {
        camera->center_x+=((double)c->width/2-((double)camera->viewport_x+(double)camera->viewport_width/2))/camera->scale;
        camera->center_y+=((double)c->height/2-((double)camera->viewport_y+(double)camera->viewport_height/2))/camera->scale;
    }
    return overlay_scale_bar(c,camera,e);
}
