/* SPDX-License-Identifier: GPL-3.0-only */
#include "render.h"
#include "vec.h"
#include <cairo.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

/* Benchmark prototype: deterministic primitive-center ordering. This cannot
 * resolve intersecting surfaces or a cylinder whose depth changes along its
 * length. Keep it separate from the per-sample depth correctness baseline. */
typedef struct {
    const Primitive *primitive;
    Vec3 a,b;
    double radius, depth, fade;
    size_t order;
} PaintPrimitive;

static int paint_order(const void *left, const void *right)
{
    const PaintPrimitive *a=left, *b=right;
    if (a->depth<b->depth) return -1;
    if (a->depth>b->depth) return 1;
    return (a->order>b->order)-(a->order<b->order);
}

static void color_components(const PaintPrimitive *p, double shade, double white, double rgba[4])
{
    shade*=1-p->fade;
    white=white*(1-p->fade)+p->fade;
    for (size_t k=0;k<3;++k) rgba[k]=fmin(1,((double)p->primitive->color[k]/255)*shade+white);
    rgba[3]=(double)p->primitive->color[3]/255;
}

static void paint_color(cairo_t *cr, const PaintPrimitive *p, double shade, double white)
{
    double rgba[4];color_components(p,shade,white,rgba);
    cairo_set_source_rgba(cr,rgba[0],rgba[1],rgba[2],rgba[3]);
}

static void gradient_stop(cairo_pattern_t *pattern, double offset, const PaintPrimitive *p, double shade)
{
    double rgba[4];color_components(p,shade,0,rgba);
    cairo_pattern_add_color_stop_rgba(pattern,offset,rgba[0],rgba[1],rgba[2],rgba[3]);
}

static void circle(cairo_t *cr, double x, double y, double radius)
{
    cairo_new_sub_path(cr);
    cairo_arc(cr,x,y,radius,0,6.2831853071795864769);
}

static void paint_sphere(cairo_t *cr, const Config *c, const PaintPrimitive *p)
{
    double x=p->a.x[0], y=p->a.x[1], r=p->radius;
    cairo_save(cr);circle(cr,x,y,r);cairo_clip(cr);
    if (c->style==STYLE_ILLUSTRATED) {
        paint_color(cr,p,0.82,0);cairo_paint(cr);
        paint_color(cr,p,0.90,0.10);
        circle(cr,x+0.10*r,y-0.10*r,0.88*r);cairo_fill(cr);
        paint_color(cr,p,0.45,0.55);
        circle(cr,x+0.16*r,y-0.18*r,0.47*r);cairo_fill(cr);
        if (c->outline_width>0) {
            cairo_set_source_rgba(cr,0,0,0,(double)p->primitive->color[3]/255);
            cairo_set_line_width(cr,2*c->outline_width);
            circle(cr,x,y,r);cairo_stroke(cr);
        }
    } else {
        cairo_pattern_t *gradient=cairo_pattern_create_radial(x-0.3*r,y+0.3*r,0,x,y,r);
        gradient_stop(gradient,0,p,1);
        gradient_stop(gradient,0.65,p,0.75);
        gradient_stop(gradient,1,p,0.25);
        cairo_set_source(cr,gradient);cairo_paint(cr);cairo_pattern_destroy(gradient);
    }
    cairo_restore(cr);
}

static void ellipse(cairo_t *cr, double x, double y, double major, double minor, double angle)
{
    if (minor<1e-12 || major<1e-12) return;
    cairo_save(cr);cairo_translate(cr,x,y);cairo_rotate(cr,angle);
    cairo_scale(cr,minor,major);circle(cr,0,0,1);cairo_restore(cr);
}

static void paint_cylinder(cairo_t *cr, const Config *c, const PaintPrimitive *p)
{
    double dx=p->b.x[0]-p->a.x[0], dy=p->b.x[1]-p->a.x[1];
    double length2d=hypot(dx,dy), r=p->radius;
    Vec3 axis=vsub(p->primitive->b,p->primitive->a);
    double length3d=length(axis);
    if (length3d<=1e-14) return;
    double nx=length2d>1e-12 ? -dy/length2d : 1;
    double ny=length2d>1e-12 ? dx/length2d : 0;
    double axial=fmin(1,fabs(p->b.x[2]-p->a.x[2])/length3d);
    /* World depths are unscaled; projected xy/r are in output pixels. */
    cairo_new_path(cr);
    cairo_move_to(cr,p->a.x[0]-r*nx,p->a.x[1]-r*ny);
    cairo_line_to(cr,p->b.x[0]-r*nx,p->b.x[1]-r*ny);
    cairo_line_to(cr,p->b.x[0]+r*nx,p->b.x[1]+r*ny);
    cairo_line_to(cr,p->a.x[0]+r*nx,p->a.x[1]+r*ny);cairo_close_path(cr);
    double angle=atan2(dy,dx);
    ellipse(cr,p->a.x[0],p->a.x[1],r,r*axial,angle);
    ellipse(cr,p->b.x[0],p->b.x[1],r,r*axial,angle);
    cairo_set_fill_rule(cr,CAIRO_FILL_RULE_WINDING);
    cairo_save(cr);cairo_clip_preserve(cr);
    if (p->primitive->kind==CELL_EDGE) paint_color(cr,p,1,0);
    else if (c->style==STYLE_ILLUSTRATED) paint_color(cr,p,0.64,0);
    else {
        double x=(p->a.x[0]+p->b.x[0])/2, y=(p->a.x[1]+p->b.x[1])/2;
        cairo_pattern_t *gradient=cairo_pattern_create_linear(x-r*nx,y-r*ny,x+r*nx,y+r*ny);
        gradient_stop(gradient,0,p,0.25);gradient_stop(gradient,0.4,p,0.9);gradient_stop(gradient,1,p,0.3);
        cairo_set_source(cr,gradient);cairo_pattern_destroy(gradient);
    }
    cairo_paint(cr);
    if (c->style==STYLE_ILLUSTRATED && p->primitive->kind==CYLINDER && c->outline_width>0) {
        cairo_set_source_rgba(cr,0,0,0,(double)p->primitive->color[3]/255);
        cairo_set_line_width(cr,2*c->outline_width);cairo_stroke(cr);
    } else cairo_new_path(cr);
    cairo_restore(cr);
}

bool render_cairo(const Config *c, const Scene *s, const Camera *camera, size_t source_bytes, Image *out, Error *e)
{
    if (c->adaptive_aa) return fail(e,3,"adaptive sampling is a Z-buffer experiment; Cairo uses native coverage or uniform supersampling");
    size_t count=c->width*c->height, factor=c->aa_scale;
    size_t retained=scene_memory_bytes(s)+source_bytes;
    const size_t reserve=8u*1024u*1024u;
    if (retained>VR_MEMORY_BUDGET-reserve || s->count>(VR_MEMORY_BUDGET-reserve-retained)/sizeof(PaintPrimitive))
        return fail(e,6,"Cairo scene exceeds 1 GiB allocation budget");
    retained+=s->count*sizeof(PaintPrimitive);
    if (count>(VR_MEMORY_BUDGET-reserve-retained)/(4*factor*factor+4))
        return fail(e,6,"Cairo image exceeds 1 GiB allocation budget");
    PaintPrimitive *items=malloc(s->count*sizeof *items);
    uint8_t *pixels=malloc(count*4);
    if (!items || !pixels) { free(items);free(pixels);return fail(e,6,"Cairo allocation failed"); }
    double near=-DBL_MAX, far=DBL_MAX;
    for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE) {
        double z=dot(s->items[i].a,camera->view);near=fmax(near,z);far=fmin(far,z);
    }
    for (size_t i=0;i<s->count;++i) {
        PaintPrimitive *p=&items[i];p->primitive=&s->items[i];p->order=i;
        p->a=camera_project(camera,p->primitive->a);p->b=camera_project(camera,p->primitive->b);
        p->depth=(p->a.x[2]+p->b.x[2])/2;
        p->fade=near>far ? c->depth_fade*fmin(1,fmax(0,(near-p->depth)/(near-far))) : 0;
        p->a.x[1]=(double)c->height-p->a.x[1];p->b.x[1]=(double)c->height-p->b.x[1];
        p->radius=p->primitive->kind==CELL_EDGE ? c->cell_width/2 : p->primitive->radius*camera->scale;
    }
    qsort(items,s->count,sizeof *items,paint_order);
    cairo_surface_t *surface=cairo_image_surface_create(CAIRO_FORMAT_ARGB32,(int)(c->width*factor),(int)(c->height*factor));
    cairo_t *cr=cairo_create(surface);
    cairo_scale(cr,(double)factor,(double)factor);
    cairo_set_antialias(cr,CAIRO_ANTIALIAS_BEST);
    uint8_t background[4];decode_color(c->background,background);
    cairo_set_source_rgba(cr,(double)background[0]/255,(double)background[1]/255,(double)background[2]/255,(double)background[3]/255);
    cairo_set_operator(cr,CAIRO_OPERATOR_SOURCE);cairo_paint(cr);cairo_set_operator(cr,CAIRO_OPERATOR_OVER);
    for (size_t i=0;i<s->count;++i) {
        if (!items[i].primitive->color[3]) continue;
        if (items[i].primitive->kind==SPHERE) paint_sphere(cr,c,&items[i]);
        else paint_cylinder(cr,c,&items[i]);
    }
    cairo_status_t status=cairo_status(cr);
    if (status==CAIRO_STATUS_SUCCESS) status=cairo_surface_status(surface);
    if (status!=CAIRO_STATUS_SUCCESS) {
        cairo_destroy(cr);cairo_surface_destroy(surface);free(items);free(pixels);
        return fail(e,6,"Cairo rendering failed: %s",cairo_status_to_string(status));
    }
    cairo_surface_flush(surface);
    const uint8_t *data=cairo_image_surface_get_data(surface);
    size_t stride=(size_t)cairo_image_surface_get_stride(surface);
    for (size_t y=0;y<c->height;++y) for (size_t x=0;x<c->width;++x) {
        unsigned alpha=0, sums[3]={0,0,0};
        for (size_t dy=0;dy<factor;++dy) for (size_t dx=0;dx<factor;++dx) {
            uint32_t value;
            memcpy(&value,data+(factor*y+dy)*stride+4*(factor*x+dx),4);
            alpha+=value>>24;
            sums[0]+=(value>>16)&255u;sums[1]+=(value>>8)&255u;sums[2]+=value&255u;
        }
        uint8_t *pixel=pixels+4*(y*c->width+x);
        pixel[3]=(uint8_t)((alpha+factor*factor/2)/(factor*factor));
        for (size_t k=0;k<3;++k) pixel[k]=(uint8_t)(alpha ? (255*sums[k]+alpha/2)/alpha : 0);
    }
    cairo_destroy(cr);cairo_surface_destroy(surface);free(items);
    out->width=c->width;out->height=c->height;out->pixels=pixels;
    return true;
}
