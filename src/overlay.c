/* SPDX-License-Identifier: GPL-3.0-only */
#include "overlay.h"
#include "vec.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ink[4]={24,28,32,255}, plate[4]={255,255,255,224};
typedef struct { size_t index; double depth; } LabelOrder;
typedef struct { double left,top,right,bottom; } LabelBox;

size_t overlay_label_scratch_bytes(const Scene *s)
{
    return s->atom_count*(sizeof(LabelOrder)+sizeof(LabelBox));
}

static int label_order(const void *left, const void *right)
{
    const LabelOrder *a=left,*b=right;
    if (a->depth>b->depth) return -1;
    if (a->depth<b->depth) return 1;
    return (a->index>b->index)-(a->index<b->index);
}

static uint8_t byte(double value)
{
    return (uint8_t)fmax(0,fmin(255,floor(value+0.5)));
}

static void blend(Image *image, int x, int y, const uint8_t color[4], double coverage)
{
    if (x<0 || y<0 || (size_t)x>=image->width || (size_t)y>=image->height || coverage<=0) return;
    uint8_t *p=image->pixels+4*((size_t)y*image->width+(size_t)x);
    double a=fmin(1,coverage)*(double)color[3]/255, background=(double)p[3]/255;
    double result=a+background*(1-a);
    for (size_t d=0;d<3;++d) p[d]=result>0 ? byte(((double)color[d]*a+(double)p[d]*background*(1-a))/result) : 0;
    p[3]=byte(result*255);
}

static double overlap(double a, double b, double lo, double hi)
{
    return fmax(0,fmin(b,hi)-fmax(a,lo));
}

static void rectangle(Image *image, double x0, double y0, double x1, double y1, const uint8_t color[4])
{
    int left=(int)fmax(0,floor(x0)), right=(int)fmin((double)image->width,ceil(x1));
    int top=(int)fmax(0,floor(y0)), bottom=(int)fmin((double)image->height,ceil(y1));
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x)
        blend(image,x,y,color,overlap(x0,x1,x,x+1)*overlap(y0,y1,y,y+1));
}

static void circle(Image *image, double cx, double cy, double radius, const uint8_t color[4])
{
    int left=(int)fmax(0,floor(cx-radius-1)), right=(int)fmin((double)image->width,ceil(cx+radius+1));
    int top=(int)fmax(0,floor(cy-radius-1)), bottom=(int)fmin((double)image->height,ceil(cy+radius+1));
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x)
        blend(image,x,y,color,radius+0.5-hypot((double)x+0.5-cx,(double)y+0.5-cy));
}

static void line(Image *image, double x0, double y0, double x1, double y1, double width, const uint8_t color[4])
{
    double dx=x1-x0,dy=y1-y0,length2=dx*dx+dy*dy;
    int left=(int)fmax(0,floor(fmin(x0,x1)-width)),right=(int)fmin((double)image->width,ceil(fmax(x0,x1)+width));
    int top=(int)fmax(0,floor(fmin(y0,y1)-width)),bottom=(int)fmin((double)image->height,ceil(fmax(y0,y1)+width));
    for (int y=top;y<bottom;++y) for (int x=left;x<right;++x) {
        double px=(double)x+0.5-x0,py=(double)y+0.5-y0;
        double t=length2>0 ? fmax(0,fmin(1,(px*dx+py*dy)/length2)) : 0;
        blend(image,x,y,color,width/2+0.5-hypot(px-t*dx,py-t*dy));
    }
}

/* All annotation text uses embedded outline glyphs: polygons in 1/1000 em
 * generated from libre fonts at development time, so no font library or file
 * is needed at run time. With markup, an underscore starts a subscript that
 * lasts for the following letters and digits. */
typedef struct { uint16_t code; int16_t advance; uint16_t first, count; } LabelGlyph;
typedef struct { uint16_t first, count; } LabelContour;
typedef struct {
    const LabelGlyph *glyphs; size_t glyph_count;
    const LabelContour *contours; const int16_t (*points)[2];
    int16_t cap_height;
} LabelFont;
#include "label_font.inc"
#define SUBSCRIPT_SCALE 0.68
#define SUBSCRIPT_DROP 0.28 /* Fraction of the cap height below the baseline. */

static unsigned codepoint(const char **text)
{
    const unsigned char *p=(const unsigned char *)*text;
    if ((p[0]&0xe0)==0xc0 && (p[1]&0xc0)==0x80) { *text+=2;return ((unsigned)(p[0]&0x1f)<<6)|(p[1]&0x3f); }
    ++*text;return p[0];
}
static const LabelGlyph *label_glyph(const LabelFont *f, unsigned code)
{
    const LabelGlyph *fallback=f->glyphs;
    for (size_t i=0;i<f->glyph_count;++i) {
        if (f->glyphs[i].code==code) return &f->glyphs[i];
        if (f->glyphs[i].code=='?') fallback=&f->glyphs[i];
    }
    return fallback;
}
static bool subscript_step(bool markup, unsigned code, bool *subscript)
{
    if (!markup) return true;
    if (code=='_') {*subscript=true;return false;}
    if (*subscript && !((code>='0' && code<='9') || (code>='a' && code<='z') || (code>='A' && code<='Z'))) *subscript=false;
    return true;
}
/* Width is the sum of advances; height spans the capitals plus any subscript. */
static void label_size(const LabelFont *f, const char *value, double cap, bool markup, double *width, double *height)
{
    bool subscript=false,dropped=false;double w=0,scale=cap/f->cap_height;
    while (*value) {
        unsigned code=codepoint(&value);
        if (!subscript_step(markup,code,&subscript)) continue;
        w+=label_glyph(f,code)->advance*scale*(subscript ? SUBSCRIPT_SCALE : 1);dropped=dropped || subscript;
    }
    *width=w;*height=cap*(1+(dropped ? SUBSCRIPT_DROP : 0));
}
/* Nonzero winding of the glyph around a point, and the distance to its outline. */
static int glyph_winding(const LabelFont *f, const LabelGlyph *g, double x, double y, double *distance)
{
    int winding=0;double nearest=DBL_MAX;
    for (size_t c=g->first;c<(size_t)g->first+g->count;++c) {
        const int16_t (*p)[2]=f->points+f->contours[c].first;size_t n=f->contours[c].count;
        for (size_t k=0;k<n;++k) {
            double ax=p[k][0],ay=p[k][1],bx=p[(k+1)%n][0],by=p[(k+1)%n][1];
            if ((ay<=y)!=(by<=y)) {
                double side=(bx-ax)*(y-ay)-(by-ay)*(x-ax);
                if (by>ay ? side>0 : side<0) winding+=by>ay ? 1 : -1;
            }
            if (distance) {
                double dx=bx-ax,dy=by-ay,l2=dx*dx+dy*dy;
                double u=l2>0 ? fmax(0,fmin(1,((x-ax)*dx+(y-ay)*dy)/l2)) : 0;
                nearest=fmin(nearest,hypot(x-ax-u*dx,y-ay-u*dy));
            }
        }
    }
    if (distance) *distance=nearest;
    return winding;
}
/* Draw from the top-left of the capital box. With a positive halo, paint the
 * glyphs grown by that many pixels instead (a soft-edged surround). */
static void label_text(Image *image, const LabelFont *f, double x, double y, const char *value, double cap, double halo, const uint8_t color[4], bool markup)
{
    bool subscript=false;double baseline=y+cap;
    while (*value) {
        unsigned code=codepoint(&value);
        if (!subscript_step(markup,code,&subscript)) continue;
        const LabelGlyph *g=label_glyph(f,code);
        double scale=cap/f->cap_height*(subscript ? SUBSCRIPT_SCALE : 1),base=baseline+(subscript ? SUBSCRIPT_DROP*cap : 0);
        double lo[2]={DBL_MAX,DBL_MAX},hi[2]={-DBL_MAX,-DBL_MAX};
        for (size_t c=g->first;c<(size_t)g->first+g->count;++c) for (size_t k=0;k<f->contours[c].count;++k) {
            const int16_t *p=f->points[f->contours[c].first+k];
            for (int d=0;d<2;++d) {lo[d]=fmin(lo[d],p[d]);hi[d]=fmax(hi[d],p[d]);}
        }
        if (g->count) {
            int left=(int)floor(x+lo[0]*scale-halo-1),right=(int)ceil(x+hi[0]*scale+halo+1);
            int top=(int)floor(base-hi[1]*scale-halo-1),bottom=(int)ceil(base-lo[1]*scale+halo+1);
            for (int py=top;py<bottom;++py) for (int px=left;px<right;++px) {
                double coverage=0;
                if (halo>0) {
                    double distance,gx=((double)px+0.5-x)/scale,gy=(base-((double)py+0.5))/scale;
                    coverage=glyph_winding(f,g,gx,gy,&distance) ? 1 : halo+0.5-distance*scale;
                } else for (int sy=0;sy<4;++sy) for (int sx=0;sx<4;++sx) {
                    double gx=((double)px+((double)sx+0.5)/4-x)/scale,gy=(base-((double)py+((double)sy+0.5)/4))/scale;
                    if (glyph_winding(f,g,gx,gy,NULL)) coverage+=1.0/16;
                }
                blend(image,px,py,color,coverage);
            }
        }
        x+=g->advance*scale;
    }
}
/* Legend, axes and scale-bar text: upright sans with a capital height of
 * seven units, drawn from the top-left of the capital box. */
static double text_width(const char *value, double unit)
{
    double w,h;label_size(&label_sans,value,7*unit,false,&w,&h);return w;
}
static void text(Image *image, double x, double y, const char *value, double unit, const uint8_t color[4])
{
    label_text(image,&label_sans,x,y,value,7*unit,0,color,false);
}
static const LabelFont *symmetry_font(const Config *c) {return c->symmetry.label_sans ? &label_sans : &label_italic;}

static double symmetry_unit(const Config *c)
{
    double shorter=(double)(c->width<c->height ? c->width : c->height);
    return c->symmetry.label_scale*fmax(1.6,shorter/210);
}
static double symmetry_marker_radius(const Config *c) {return fmax(5,3.0*symmetry_unit(c));}

/* Label rectangle in image pixels (y downward): left, top, right, bottom,
 * excluding the halo. Axis labels continue beyond the upper tip; plane labels
 * sit outside the boundary point furthest toward the upper right. */
bool overlay_symmetry_label_box(const Config *c, const Scene *s, const Camera *camera, bool anchored_only, double box[4])
{
    const SymmetryMark *m=&s->symmetry;
    if (!m->kind || !c->symmetry.show_label || !m->label[0]) return false;
    double unit=symmetry_unit(c),w,h,gap=1.6*unit+2,cx,cy;
    label_size(symmetry_font(c),m->label,7*unit,true,&w,&h);
    if (w<=0) return false;
    const double diagonal=0.7071067811865476;
    if (m->kind==SYMMETRY_IDENTITY) {
        if (anchored_only) return false;
        double inset=0.05*(double)(c->width<c->height ? c->width : c->height);
        box[0]=(double)camera->viewport_x+inset;
        box[1]=(double)(camera->height-camera->viewport_y-camera->viewport_height)+inset;
        box[2]=box[0]+w;box[3]=box[1]+h;return true;
    }
    Vec3 origin=camera_project(camera,m->origin);
    double tx=origin.x[0],ty=origin.x[1],dx=diagonal,dy=diagonal,clear=gap;
    if (m->kind==SYMMETRY_INVERSION) {
        if (anchored_only) return false;
        clear+=symmetry_marker_radius(c)+0.8*unit;
    } else if (m->anchor_count>=2 && (m->kind==SYMMETRY_ROTATION || m->kind==SYMMETRY_IMPROPER)) {
        Vec3 a=camera_project(camera,m->anchors[0]),b=camera_project(camera,m->anchors[1]);
        bool first=a.x[1]>b.x[1]+1 || (a.x[1]>=b.x[1]-1 && a.x[0]>=b.x[0]);
        Vec3 tip=first ? a : b,other=first ? b : a;
        double span=hypot(tip.x[0]-other.x[0],tip.x[1]-other.x[1]);
        tx=tip.x[0];ty=tip.x[1];
        if (span>=1) {dx=(tip.x[0]-other.x[0])/span;dy=(tip.x[1]-other.x[1])/span;}
    } else if (m->anchor_count) {
        double best=-DBL_MAX;
        for (size_t k=0;k<m->anchor_count;++k) {
            Vec3 p=camera_project(camera,m->anchors[k]);double score=p.x[1]+0.35*p.x[0];
            if (score>best) {best=score;tx=p.x[0];ty=p.x[1];}
        }
        double span=hypot(tx-origin.x[0],ty-origin.x[1]);
        if (span>=1) {dx=(tx-origin.x[0])/span;dy=(ty-origin.x[1])/span;}
    }
    double reach=clear+fabs(dx)*w/2+fabs(dy)*h/2;
    cx=tx+dx*reach;cy=(double)camera->height-(ty+dy*reach);
    box[0]=cx-w/2;box[1]=cy-h/2;box[2]=cx+w/2;box[3]=cy+h/2;
    return true;
}

static void symmetry(const Config *c, const Scene *s, const Camera *camera, Image *image)
{
    const SymmetryMark *m=&s->symmetry;
    static const uint8_t halo[4]={255,255,255,235};
    double unit=symmetry_unit(c);
    if (m->kind==SYMMETRY_INVERSION) {
        uint8_t color[4];decode_color(c->symmetry.axis_color,color);
        Vec3 p=camera_project(camera,m->origin);double radius=symmetry_marker_radius(c);
        double x=p.x[0],y=(double)camera->height-p.x[1];
        static const uint8_t white[4]={255,255,255,255};
        /* Ringed dot: stays legible on dark atoms, bonds and the background. */
        circle(image,x,y,radius+fmax(1.5,0.25*radius),halo);circle(image,x,y,radius,color);
        circle(image,x,y,0.62*radius,white);circle(image,x,y,0.34*radius,color);
    }
    double box[4];
    if (!overlay_symmetry_label_box(c,s,camera,false,box)) return;
    uint8_t ink_color[4];decode_color(c->symmetry.label_color,ink_color);
    label_text(image,symmetry_font(c),box[0],box[1],m->label,7*unit,fmax(1.5,0.6*unit),halo,true);
    label_text(image,symmetry_font(c),box[0],box[1],m->label,7*unit,0,ink_color,true);
}

static size_t elements(const Config *c, const Scene *s, size_t counts[119], uint8_t colors[119][4])
{
    size_t total=0;memset(counts,0,119*sizeof *counts);
    for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE) {
        unsigned z=s->items[i].element<=118 ? s->items[i].element : 0;
        if (!counts[z]) {
            ++total;
            if (z) element_color(c,z,colors[z]);else memcpy(colors[z],s->items[i].color,4);
        }
        ++counts[z];
    }
    return total;
}

bool overlay_layout(const Config *c, const Scene *s, Camera *camera, Error *e)
{
    camera->viewport_x=camera->viewport_y=0;
    camera->viewport_width=c->width;camera->viewport_height=c->height;
    camera->legend_columns=camera->legend_rows=camera->legend_column_width=0;
    camera->legend_width=camera->legend_height=camera->footer_height=0;
    camera->scale_bar_A=camera->scale_bar_pixels=0;camera->scale_bar_label[0]='\0';
    if (!(c->labels || c->axes || c->legend || c->scale_bar)) return true;
    if (c->width<64 || c->height<64) return fail(e,2,"annotations require image dimensions of at least 64 pixels");
    camera->footer_height=c->axes ? 72 : c->scale_bar ? 42 : 0;
    if (c->height<camera->footer_height+32) return fail(e,2,"requested annotations leave too little vertical space for geometry");
    camera->viewport_y=camera->footer_height;
    camera->viewport_height=c->height-camera->footer_height;
    if (c->scale_bar && c->width<(c->axes ? 176u : 88u))
        return fail(e,2,"scale bar and axes require a wider image");
    if (c->legend) {
        size_t counts[119];uint8_t colors[119][4];size_t n=elements(c,s,counts,colors);
        size_t available=camera->viewport_height>36 ? (camera->viewport_height-36)/18 : 0;
        if (!available) return fail(e,2,"image is too short for an element legend");
        camera->legend_rows=n<available ? n : available;
        camera->legend_columns=n ? (n+available-1)/available : 1;
        double max_width=text_width("ELEMENTS",1.5)+16;
        for (unsigned z=0;z<=118;++z) if (counts[z]) {
            char label[32];snprintf(label,sizeof label,"%s %zu",element_symbol(z),counts[z]);
            max_width=fmax(max_width,text_width(label,1.5)+36);
        }
        camera->legend_column_width=(size_t)ceil(max_width);
        camera->legend_width=camera->legend_columns*camera->legend_column_width;
        camera->legend_height=28+camera->legend_rows*18;
        if (camera->legend_width+48>c->width) return fail(e,2,"element legend leaves too little space for geometry; increase image width/height");
        camera->viewport_width=c->width-camera->legend_width-16;
    }
    if (c->labels) {
        size_t digits=1;
        if (c->labels==2) for (size_t n=s->atom_count ? s->atom_count-1 : 0;n>=10;n/=10) ++digits;
        else digits=2;
        if ((double)camera->viewport_width<(double)digits*7.5+12)
            return fail(e,2,"atom labels do not fit the geometry viewport");
    }
    return true;
}

bool overlay_scale_bar(const Config *c, Camera *camera, Error *e)
{
    if (!c->scale_bar) return true;
    double available=(double)c->width-(c->axes ? 112 : 24);
    double max_pixels=fmin(120,available),max_length=max_pixels/camera->scale;
    if (!isfinite(max_length) || max_length<=0) return fail(e,5,"unrepresentable scale bar");
    double power=pow(10,floor(log10(max_length))),length=power;
    if (5*power<=max_length) length=5*power;
    else if (2*power<=max_length) length=2*power;
    camera->scale_bar_A=length;camera->scale_bar_pixels=length*camera->scale;
    if (!isfinite(camera->scale_bar_pixels) || camera->scale_bar_pixels<=0)
        return fail(e,5,"unrepresentable scale bar pixels");
    snprintf(camera->scale_bar_label,sizeof camera->scale_bar_label,"%.3g \xc3\x85",length);
    return true;
}

static void axes(Image *image, const Camera *camera)
{
    static const uint8_t colors[3][4]={{198,45,45,255},{35,137,67,255},{45,92,204,255}};
    double ox=(double)image->width-48,oy=(double)image->height-36;
    rectangle(image,(double)image->width-96,(double)image->height-68,(double)image->width-8,(double)image->height-4,plate);
    for (size_t d=0;d<3;++d) {
        double dx=26*camera->right.x[d],dy=-26*camera->up.x[d],len=hypot(dx,dy);
        double ex=ox+dx,ey=oy+dy;
        if (len<1e-8) {
            circle(image,ox,oy,4,colors[d]);circle(image,ox,oy,2,plate);
            if (camera->view.x[d]>0) circle(image,ox,oy,1.2,colors[d]);
            else { line(image,ox-2,oy-2,ox+2,oy+2,1,colors[d]);line(image,ox-2,oy+2,ox+2,oy-2,1,colors[d]); }
            ex=ox+8;ey=oy+8;
        } else {
            line(image,ox,oy,ex,ey,1.8,colors[d]);
            line(image,ex,ey,ex-6*dx/len+3*dy/len,ey-6*dy/len-3*dx/len,1.5,colors[d]);
            line(image,ex,ey,ex-6*dx/len-3*dy/len,ey-6*dy/len+3*dx/len,1.5,colors[d]);
            ex+=5*dx/len;ey+=5*dy/len;
        }
        char label[2]={(char)('X'+(int)d),0};
        double tx=fmax((double)image->width-90,fmin((double)image->width-18,ex-3));
        double ty=fmax((double)image->height-65,fmin((double)image->height-15,ey-4));
        text(image,tx,ty,label,1.25,colors[d]);
    }
}

bool overlay_render(const Config *c, const Scene *s, const Camera *camera,
                    const LabelAnchor *anchors, Image *image, Error *e)
{
    if (!(c->labels || c->axes || c->legend || c->scale_bar)) {
        if (s->symmetry.kind) symmetry(c,s,camera,image);
        return true;
    }
    size_t labels=0,candidates=0;
    LabelOrder *order=NULL;LabelBox *placed=NULL;
    if (c->labels) {
        order=malloc(s->atom_count*sizeof *order);placed=malloc(s->atom_count*sizeof *placed);
        if (!order || !placed) { free(order);free(placed);return fail(e,6,"label placement allocation failed"); }
        for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE && anchors && anchors[i].visible)
            order[candidates++]=(LabelOrder){i,anchors[i].depth};
        qsort(order,candidates,sizeof *order,label_order);
    }
    /* One lettering size per image, from the smallest labelled atom: large
     * enough to read, small enough to sit inside that atom. Never smaller than
     * the former fixed size. */
    double cap=8.75,smallest=DBL_MAX,shorter=(double)(image->width<image->height ? image->width : image->height);
    /* Every drawable atom counts, visible or not, so the size holds across frames. */
    for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE && s->items[i].color[3])
        smallest=fmin(smallest,s->items[i].radius*camera->scale);
    if (smallest<DBL_MAX) cap=fmax(cap,fmin(0.5*smallest,shorter/32));
    for (size_t candidate=0;candidate<candidates;++candidate) {
        size_t i=order[candidate].index;
        const Primitive *p=&s->items[i];
        char label[32];
        if (c->labels==1) snprintf(label,sizeof label,"%s",element_symbol(p->element));
        else snprintf(label,sizeof label,"%zu",i);
        double w,h;label_size(&label_sans,label,cap,false,&w,&h);
        double left=fmax((double)camera->viewport_x+3,fmin((double)(camera->viewport_x+camera->viewport_width)-w-3,anchors[i].x-w/2));
        double top=fmax((double)(image->height-camera->viewport_y-camera->viewport_height)+3,
                        fmin((double)(image->height-camera->viewport_y)-h-3,anchors[i].y-h/2));
        LabelBox box={left-3,top-2,left+w+3,top+h+2};
        bool collision=false;
        for (size_t j=0;j<labels;++j) {
            const LabelBox *b=&placed[j];
            if (box.left<b->right+2 && box.right+2>b->left && box.top<b->bottom+2 && box.bottom+2>b->top) {
                collision=true;break;
            }
        }
        if (collision) continue;
        /* Choose dark or light lettering from what is actually underneath, in
         * any style or tone, and surround it thinly in the opposite shade. */
        double luminance=0,weight=0;
        for (int y=(int)box.top;y<(int)box.bottom;++y) for (int x=(int)box.left;x<(int)box.right;++x) {
            if (x<0 || y<0 || (size_t)x>=image->width || (size_t)y>=image->height) continue;
            const uint8_t *q=image->pixels+4*((size_t)y*image->width+(size_t)x);double alpha=(double)q[3]/255;
            luminance+=alpha*(0.2126*q[0]+0.7152*q[1]+0.0722*q[2])+(1-alpha)*255;weight+=1;
        }
        /* Blend through a narrow band rather than flipping at one threshold. */
        double mean=weight>0 ? luminance/weight : 255,light=fmax(0,fmin(1,(mean-112)/32));
        double opacity=anchors[i].opacity;
        uint8_t letter[4],surround[4];
        for (size_t d=0;d<3;++d) {
            letter[d]=byte(255+((double)ink[d]-255)*light);
            surround[d]=byte((double)ink[d]+(255-(double)ink[d])*light);
        }
        letter[3]=byte(255*opacity);surround[3]=byte(170*opacity);
        label_text(image,&label_sans,left,top,label,cap,fmax(1,0.11*cap),surround,false);
        label_text(image,&label_sans,left,top,label,cap,0,letter,false);placed[labels++]=box;
    }
    free(order);free(placed);
    if (c->legend) {
        size_t counts[119];uint8_t colors[119][4];(void)elements(c,s,counts,colors);
        double left=(double)(image->width-camera->legend_width)-8;
        rectangle(image,left,8,(double)image->width-8,8+(double)camera->legend_height,plate);
        text(image,left+8,13,"ELEMENTS",1.5,ink);
        size_t index=0;
        for (unsigned z=0;z<=118;++z) if (counts[z]) {
            size_t column=index/camera->legend_rows,row=index%camera->legend_rows;
            double x=left+8+(double)(column*camera->legend_column_width),y=35+(double)(row*18);
            const uint8_t white[4]={255,255,255,255};
            circle(image,x+6,y+5,6,ink);circle(image,x+6,y+5,5,white);circle(image,x+6,y+5,5,colors[z]);
            char label[32];snprintf(label,sizeof label,"%s %zu",element_symbol(z),counts[z]);
            text(image,x+20,y,label,1.5,ink);++index;
        }
    }
    if (c->scale_bar) {
        double available=(double)image->width-(c->axes ? 112 : 24);
        double y=(double)image->height-20,x=12;
        rectangle(image,8,(double)image->height-42,16+available,(double)image->height-5,plate);
        line(image,x,y,x+camera->scale_bar_pixels,y,2,ink);
        line(image,x,y-4,x,y+4,1.5,ink);line(image,x+camera->scale_bar_pixels,y-4,x+camera->scale_bar_pixels,y+4,1.5,ink);
        double w=text_width(camera->scale_bar_label,1.25);
        double tx=fmax(12,fmin(12+available-w,12+camera->scale_bar_pixels/2-w/2));
        text(image,tx,(double)image->height-36,camera->scale_bar_label,1.25,ink);
    }
    if (c->axes) axes(image,camera);
    if (s->symmetry.kind) symmetry(c,s,camera,image);
    if (c->benchmark && c->labels) fprintf(stderr,"overlay_labels=%zu overlay_labels_skipped=%zu\n",labels,candidates-labels);
    return true;
}
