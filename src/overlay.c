/* SPDX-License-Identifier: GPL-3.0-only */
#include "overlay.h"
#include "vec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Original fixed 5x7 glyphs. Fractional cell coverage is accumulated before
 * compositing a glyph, avoiding seams between adjacent filled cells. */
typedef struct { unsigned code; uint8_t rows[7]; } Glyph;
static const Glyph font[]={
 {' ',{0,0,0,0,0,0,0}},{'?',{14,17,1,2,4,0,4}},
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}},{'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}},{'9',{14,17,17,15,1,1,14}},
 {'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}},{'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}},{'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}},{'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}},{'N',{17,25,21,19,17,17,17}},
 {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,27,17}},{'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}},{'Z',{31,1,2,4,8,16,31}},
 {'a',{0,0,14,1,15,17,15}},{'b',{16,16,30,17,17,17,30}},
 {'c',{0,0,14,17,16,17,14}},{'d',{1,1,15,17,17,17,15}},
 {'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},
 {'g',{0,0,15,17,15,1,14}},{'h',{16,16,30,17,17,17,17}},
 {'i',{4,0,12,4,4,4,14}},{'j',{2,0,6,2,2,18,12}},
 {'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},
 {'m',{0,0,26,21,21,21,21}},{'n',{0,0,30,17,17,17,17}},
 {'o',{0,0,14,17,17,17,14}},{'p',{0,0,30,17,30,16,16}},
 {'q',{0,0,15,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},
 {'s',{0,0,15,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},
 {'u',{0,0,17,17,17,19,13}},{'v',{0,0,17,17,17,10,4}},
 {'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},
 {'y',{0,0,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}},
 {'-',{0,0,0,31,0,0,0}},{'+',{0,4,4,31,4,4,0}},
 {'.',{0,0,0,0,0,4,4}},{0x80,{4,10,14,17,31,17,17}}
};
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

static unsigned character(const char **text)
{
    const unsigned char *p=(const unsigned char *)*text;
    if (p[0]==0xc3 && p[1]==0x85) { *text+=2;return 0x80; }
    ++*text;return p[0];
}

static double text_width(const char *text, double unit)
{
    size_t n=0;while (*text) { (void)character(&text);++n; }
    return n ? ((double)n*6-1)*unit : 0;
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

static void text(Image *image, double x, double y, const char *value, double unit, const uint8_t color[4])
{
    while (*value) {
        unsigned code=character(&value);const uint8_t *rows=font[1].rows;
        for (size_t i=0;i<sizeof font/sizeof *font;++i) if (font[i].code==code) { rows=font[i].rows;break; }
        double coverage[16][16]={{0}};
        int bx=(int)floor(x),by=(int)floor(y);
        for (size_t row=0;row<7;++row) for (size_t col=0;col<5;++col) if (rows[row]&(1u<<(4-col))) {
            double x0=x+(double)col*unit,y0=y+(double)row*unit,x1=x0+unit,y1=y0+unit;
            for (int py=(int)floor(y0);py<(int)ceil(y1);++py) for (int px=(int)floor(x0);px<(int)ceil(x1);++px)
                if (py-by>=0 && py-by<16 && px-bx>=0 && px-bx<16)
                    coverage[py-by][px-bx]+=overlap(x0,x1,px,px+1)*overlap(y0,y1,py,py+1);
        }
        for (int py=0;py<16;++py) for (int px=0;px<16;++px)
            if (coverage[py][px]>0) blend(image,bx+px,by+py,color,coverage[py][px]);
        x+=6*unit;
    }
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
        if (s->count>VR_MAX_CANDIDATES/VR_LABEL_SAMPLES ||
            (s->count && s->atom_count>VR_MAX_CANDIDATES/(s->count*VR_LABEL_SAMPLES)))
            return fail(e,6,"label visibility exceeds the 200-million-candidate work budget");
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
    if (!(c->labels || c->axes || c->legend || c->scale_bar)) return true;
    size_t labels=0,candidates=0;
    LabelOrder *order=NULL;LabelBox *placed=NULL;
    if (c->labels) {
        order=malloc(s->atom_count*sizeof *order);placed=malloc(s->atom_count*sizeof *placed);
        if (!order || !placed) { free(order);free(placed);return fail(e,6,"label placement allocation failed"); }
        for (size_t i=0;i<s->count;++i) if (s->items[i].kind==SPHERE && anchors && anchors[i].visible)
            order[candidates++]=(LabelOrder){i,anchors[i].depth};
        qsort(order,candidates,sizeof *order,label_order);
    }
    for (size_t candidate=0;candidate<candidates;++candidate) {
        size_t i=order[candidate].index;
        const Primitive *p=&s->items[i];
        char label[32];
        if (c->labels==1) snprintf(label,sizeof label,"%s",element_symbol(p->element));
        else snprintf(label,sizeof label,"%zu",i);
        double w=text_width(label,1.25),h=8.75;
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
        rectangle(image,left-3,top-2,left+w+3,top+h+2,plate);
        text(image,left,top,label,1.25,ink);placed[labels++]=box;
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
    if (c->benchmark && c->labels) fprintf(stderr,"overlay_labels=%zu overlay_labels_skipped=%zu\n",labels,candidates-labels);
    return true;
}
