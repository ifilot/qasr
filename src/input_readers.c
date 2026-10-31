/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include "vec.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BOHR_A 0.529177210903
#define MAX_COLUMNS 256
#define MAX_PROPERTIES 64

static char *trim_text(char *p)
{
    while (isspace((unsigned char)*p)) ++p;
    size_t n=strlen(p);
    while (n && isspace((unsigned char)p[n-1])) p[--n]='\0';
    return p;
}
static bool problem(Reader *r, Error *e, const char *message)
{
    return fail(e,4,"%s:%zu: %s",r->path,r->line,message);
}
static bool required_line(Reader *r, Error *e)
{
    int rc=read_line(r,e);
    return rc>0 || (rc==0 && problem(r,e,"unexpected end of geometry"));
}
static bool input_policy(const InputOptions *o, Error *e)
{
    if (o->recover) return fail(e,2,"--recover is only meaningful for OUTCAR");
    if (o->species_count) return fail(e,2,"--species is supported only for VASP inputs; these formats carry per-atom elements");
    return true;
}
static bool append_atom(Structure *s, size_t *capacity, Atom a, Reader *r, Error *e)
{
    if (s->count==VR_MAX_ATOMS) return problem(r,e,"atom limit exceeded");
    if (s->count==*capacity) {
        size_t next=*capacity ? *capacity+*capacity/2 : 64;
        if (next>VR_MAX_ATOMS) next=VR_MAX_ATOMS;
        Atom *p=realloc(s->atoms,next*sizeof *p);
        if (!p) return fail(e,6,"cannot allocate input atoms");
        s->atoms=p; *capacity=next;
    }
    s->atoms[s->count++]=a;
    return true;
}
static bool accept_frame(Reader *r, const InputOptions *o, Structure *candidate,
                         Structure *selected, size_t frame, Error *e)
{
    if (frame>=VR_MAX_FRAMES) return problem(r,e,"frame limit exceeded");
    if (!structure_validate(candidate,e)) return false;
    if (o->visit && !o->visit(candidate,frame,o->visit_context,e)) return false;
    if (o->final_frame || o->frame==frame) {
        structure_free(selected); *selected=*candidate; memset(candidate,0,sizeof *candidate);
        selected->source.frame=frame;
    }
    structure_free(candidate);
    return true;
}
static bool finish_frames(Reader *r, Structure *s, size_t frames, Format format, Error *e)
{
    if (!s->count) return problem(r,e,"requested frame not found");
    s->source.frame_count=frames; s->source.format=format;
    return true;
}
static bool bool_token(const char *p, bool *out)
{
    if (!strcasecmp(p,"T") || !strcasecmp(p,"true") || !strcmp(p,"1")) { *out=true; return true; }
    if (!strcasecmp(p,"F") || !strcasecmp(p,"false") || !strcmp(p,"0")) { *out=false; return true; }
    return false;
}
static bool vector_text(char *text, Vec3 *out)
{
    char *words[4];
    size_t n=split_words(text,words,4);
    if (n!=3) return false;
    for (int d=0;d<3;++d) if (!number(words[d],&out->x[d])) return false;
    return true;
}

/* Extended XYZ header values are whitespace-delimited or single/double quoted.
 * Unsupported quote syntax is rejected rather than misreading geometry metadata. */
static bool header_pair(char **cursor, char **key, char **value)
{
    char *p=*cursor;
    while (isspace((unsigned char)*p)) ++p;
    if (!*p) { *cursor=p; *key=NULL; return true; }
    *key=p;
    while (*p && *p!='=' && !isspace((unsigned char)*p)) ++p;
    if (*p!='=') return false;
    *p++='\0';
    if (*p=='\'' || *p=='"') {
        char quote=*p++; *value=p;
        while (*p && *p!=quote) { if (*p=='\\') return false; ++p; }
        if (!*p) return false;
        *p++='\0';
        if (*p && !isspace((unsigned char)*p)) return false;
    } else {
        *value=p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) *p++='\0';
    }
    *cursor=p;
    return **key && **value;
}
typedef struct { char type; size_t offset, count; } Property;
static bool properties(char *value, Property props[MAX_PROPERTIES], size_t *np,
                        size_t *columns, int *species, int *z, int *position)
{
    *np=*columns=0; *species=*z=*position=-1;
    char *p=value;
    while (*p) {
        char *name=p, *type=strchr(p,':');
        if (!type) return false;
        *type++='\0';
        char *count=strchr(type,':');
        if (!count) return false;
        *count++='\0';
        char *next=strchr(count,':');
        if (next) { *next++='\0'; if (!*next) return false; }
        size_t n;
        if (!*name || strlen(type)!=1 || !strchr("SRIL",*type) ||
            !integer(count,MAX_COLUMNS,&n) || !n || *np==MAX_PROPERTIES || n>MAX_COLUMNS-*columns) return false;
        if (!strcmp(name,"species")) {
            if (*species>=0 || *type!='S' || n!=1) return false;
            *species=(int)*columns;
        } else if (!strcmp(name,"Z")) {
            if (*z>=0 || *type!='I' || n!=1) return false;
            *z=(int)*columns;
        } else if (!strcmp(name,"pos")) {
            if (*position>=0 || *type!='R' || n!=3) return false;
            *position=(int)*columns;
        }
        props[(*np)++]=(Property){*type,*columns,n}; *columns+=n;
        if (!next) break;
        p=next;
    }
    return *position>=0 && (*species>=0 || *z>=0);
}
static bool xyz_header(Reader *r, Structure *s, Property props[MAX_PROPERTIES], size_t *np,
                       size_t *columns, int *species, int *z, int *position, Error *e)
{
    char *cursor=r->text, *key, *value, *seen[128]; size_t nseen=0;
    bool have_properties=false, have_pbc=false, have_lattice=false;
    while (*cursor) {
        if (!header_pair(&cursor,&key,&value)) return problem(r,e,"unsupported Extended XYZ header syntax");
        if (!key) break;
        if (nseen==128) return problem(r,e,"too many Extended XYZ header fields");
        for (size_t i=0;i<nseen;++i) if (!strcmp(key,seen[i])) return problem(r,e,"duplicate Extended XYZ header field");
        seen[nseen++]=key;
        if (!strcmp(key,"Properties")) {
            if (!properties(value,props,np,columns,species,z,position)) return problem(r,e,"invalid Extended XYZ Properties; require species:S:1 or Z:I:1 and pos:R:3");
            have_properties=true;
        } else if (!strcmp(key,"Lattice")) {
            char *words[10]; size_t n=split_words(value,words,10);
            if (n!=9) return problem(r,e,"Lattice requires nine components");
            for (int d=0;d<3;++d) {
                for (int j=0;j<3;++j) if (!number(words[d*3+j],&s->lattice[d].x[j])) return problem(r,e,"invalid lattice component");
                s->cell_present[d]=length(s->lattice[d])>0;
            }
            have_lattice=true;
        } else if (!strcmp(key,"pbc")) {
            char *words[4]; size_t n=split_words(value,words,4);
            if (n!=3) return problem(r,e,"pbc requires three booleans");
            for (int d=0;d<3;++d) if (!bool_token(words[d],&s->pbc[d])) return problem(r,e,"invalid pbc value");
            have_pbc=true;
        }
    }
    if (!have_properties) return problem(r,e,"Extended XYZ requires Properties");
    if (have_lattice && !have_pbc) for (int d=0;d<3;++d) s->pbc[d]=true;
    return true;
}
bool read_xyz(Reader *r, const InputOptions *o, Structure *s, Error *e)
{
    if (!input_policy(o,e)) return false;
    size_t frames=0; int rc; bool any_extended=false, ok=false;
    Structure candidate={0};
    while ((rc=read_line(r,e))>0) {
        size_t n;
        if (!integer(trim_text(r->text),VR_MAX_ATOMS,&n) || !n) { problem(r,e,"expected positive XYZ atom count"); goto done; }
        if (!required_line(r,e)) goto done;
        bool extended=o->format==FORMAT_EXTXYZ || strstr(r->text,"Properties=") || strstr(r->text,"Lattice=") || strstr(r->text,"pbc=");
        any_extended|=extended;
        Property props[MAX_PROPERTIES]; size_t np=0, columns=4;
        int species=0,z=-1,position=1;
        if (extended && !xyz_header(r,&candidate,props,&np,&columns,&species,&z,&position,e)) goto done;
        candidate.atoms=calloc(n,sizeof *candidate.atoms); candidate.count=n;
        if (!candidate.atoms) { fail(e,6,"cannot allocate XYZ atoms"); goto done; }
        for (size_t i=0;i<n;++i) {
            if (!required_line(r,e)) goto done;
            char *words[MAX_COLUMNS];
            if (split_words(r->text,words,MAX_COLUMNS)!=columns) { problem(r,e,"XYZ property column count mismatch"); goto done; }
            for (size_t p=0;p<np;++p) for (size_t k=0;k<props[p].count;++k) {
                const char *word=words[props[p].offset+k]; double v; bool b;
                if ((props[p].type=='R' && !number(word,&v)) ||
                    (props[p].type=='I' && (!number(word,&v) || floor(v)!=v)) ||
                    (props[p].type=='L' && !bool_token(word,&b))) { problem(r,e,"invalid Extended XYZ property value"); goto done; }
                if (props[p].type=='R' && (int)props[p].offset==position)
                    candidate.atoms[i].position.x[k]=v;
            }
            unsigned element=species>=0 ? element_id(words[species]) : 0;
            if (species>=0 && !element) { problem(r,e,"unknown XYZ element"); goto done; }
            if (z>=0) {
                size_t atomic;
                if (!integer(words[z],118,&atomic) || !atomic || (element && element!=atomic)) { problem(r,e,"invalid or conflicting atomic number"); goto done; }
                element=(unsigned)atomic;
            }
            candidate.atoms[i].element=element;
            if (!extended) for (int d=0;d<3;++d) if (!number(words[position+d],&candidate.atoms[i].position.x[d])) { problem(r,e,"invalid XYZ position"); goto done; }
        }
        if (!accept_frame(r,o,&candidate,s,frames++,e)) goto done;
    }
    if (rc<0) goto done;
    ok=finish_frames(r,s,frames,any_extended ? FORMAT_EXTXYZ : FORMAT_XYZ,e);
done:
    structure_free(&candidate); return ok;
}

static unsigned gaussian_element(const char *text)
{
    size_t z;
    if (integer(text,118,&z)) return (unsigned)z;
    char symbol[3]={0}; size_t n=0;
    while (isalpha((unsigned char)text[n]) && n<2) { symbol[n]=text[n]; ++n; }
    const char *p=text+n;
    while (isdigit((unsigned char)*p)) ++p;
    return *p ? 0 : element_id(symbol);
}
bool read_gaussian_input(Reader *r, const InputOptions *o, Structure *s, Error *e)
{
    if (!input_policy(o,e)) return false;
    char route[VR_MAX_LINE+1]={0}; size_t used=0; int rc;
    bool found=false, ok=false; Structure candidate={0}; size_t capacity=0, nv=0;
    while ((rc=read_line(r,e))>0) {
        char *line=trim_text(r->text);
        if (*line=='#') { found=true; break; }
        if (*line && *line!='%') { problem(r,e,"expected Gaussian Link 0 or route section"); goto done; }
    }
    if (rc<0) goto done;
    if (!found) { problem(r,e,"Gaussian route section missing"); goto done; }
    do {
        char *line=trim_text(r->text);
        if (!*line) break;
        for (;*line;++line) {
            if (used>=VR_MAX_LINE-1) { problem(r,e,"route section too long"); goto done; }
            route[used++]=(char)tolower((unsigned char)*line);
        }
        route[used++]=' '; route[used]='\0';
    } while ((rc=read_line(r,e))>0);
    if (rc<=0) { if (!e->code) problem(r,e,"incomplete route section"); goto done; }
    double scale=1;
    char *units=strstr(route,"units");
    if (units) {
        units+=5;
        while (isspace((unsigned char)*units)) ++units;
        if (*units++!='=') { problem(r,e,"unsupported Gaussian Units option"); goto done; }
        while (isspace((unsigned char)*units)) ++units;
        bool parenthesis=*units=='(';
        if (parenthesis) ++units;
        while (isspace((unsigned char)*units)) ++units;
        char unit[16]; size_t un=0;
        while (isalpha((unsigned char)*units) && un<sizeof unit-1) unit[un++]=*units++;
        unit[un]='\0';
        if (!strcmp(unit,"bohr") || !strcmp(unit,"au")) scale=BOHR_A;
        else if (strcmp(unit,"angstrom")) { problem(r,e,"unsupported Gaussian Units option"); goto done; }
        if (parenthesis) {
            while (isspace((unsigned char)*units)) ++units;
            if (*units++!=')') { problem(r,e,"unsupported Gaussian Units options"); goto done; }
        }
        if (*units && !isspace((unsigned char)*units)) { problem(r,e,"unsupported Gaussian Units option"); goto done; }
    }
    char compact[VR_MAX_LINE+1]; size_t cn=0;
    for (size_t i=0;i<used;++i) if (!isspace((unsigned char)route[i])) compact[cn++]=route[i];
    compact[cn]='\0';
    if (strstr(compact,"geom=check") || strstr(compact,"geom=allcheck") || strstr(compact,"geom=(check") || strstr(compact,"geom=(allcheck")) {
        problem(r,e,"checkpoint geometry is unsupported; provide explicit Cartesian coordinates"); goto done;
    }
    /* Title section, including arbitrary text, ends at a blank line. */
    bool title=false;
    while ((rc=read_line(r,e))>0) { if (!*trim_text(r->text)) { if (title) break; } else title=true; }
    if (rc<=0 || !required_line(r,e)) { if (!e->code) problem(r,e,"missing charge and multiplicity"); goto done; }
    char *words[8]; size_t n=split_words(r->text,words,8); double charge; size_t multiplicity;
    if (n!=2 || !number(words[0],&charge) || floor(charge)!=charge ||
        !integer(words[1],1000,&multiplicity) || !multiplicity) { problem(r,e,"unsupported charge/multiplicity line"); goto done; }
    while ((rc=read_line(r,e))>0) {
        if (!*trim_text(r->text)) break;
        for (char *p=r->text;*p;++p) if (*p==',') *p=' ';
        n=split_words(r->text,words,8);
        if (n!=4 && n!=5) { problem(r,e,"only Gaussian Cartesian atom rows are supported (no Z-matrix or ONIOM)"); goto done; }
        size_t offset=n==5 ? 2 : 1;
        if (n==5 && strcmp(words[1],"0") && strcmp(words[1],"-1")) { problem(r,e,"unsupported Gaussian freeze code"); goto done; }
        Atom atom={0};
        for (int d=0;d<3;++d) {
            double value;
            if (!number(words[offset+(size_t)d],&value)) { problem(r,e,"invalid Cartesian coordinate"); goto done; }
            atom.position.x[d]=value*scale;
        }
        if (!strcasecmp(words[0],"TV")) {
            if (nv==3 || n!=4) { problem(r,e,"invalid Gaussian translation vector"); goto done; }
            candidate.lattice[nv]=atom.position; candidate.cell_present[nv]=candidate.pbc[nv]=true; ++nv;
        } else {
            if (nv) { problem(r,e,"atoms must precede translation vectors"); goto done; }
            atom.element=gaussian_element(words[0]);
            if (!atom.element) { problem(r,e,"unsupported Gaussian element, ghost atom or label"); goto done; }
            if (!append_atom(&candidate,&capacity,atom,r,e)) goto done;
        }
    }
    if (rc<0) goto done;
    /* Connectivity/basis data are not geometry, but still enforce input limits. */
    while ((rc=read_line(r,e))>0) if (strstr(r->text,"--Link1--")) { problem(r,e,"Gaussian Link1 input is unsupported; split jobs into separate files"); goto done; }
    if (rc<0 || !accept_frame(r,o,&candidate,s,0,e)) goto done;
    ok=finish_frames(r,s,1,FORMAT_GAUSSIAN_INPUT,e);
done:
    structure_free(&candidate); return ok;
}

static bool dashed(const char *line)
{
    size_t n=0;
    for (;*line;++line) { if (*line=='-') ++n; else if (!isspace((unsigned char)*line)) return false; }
    return n>=3;
}
bool read_gaussian_output(Reader *r, const InputOptions *o, Structure *s, Error *e)
{
    if (!input_policy(o,e)) return false;
    Structure selected[2]={0}, candidate={0}; size_t frames[2]={0};
    int rc; bool ok=false;
    while ((rc=read_line(r,e))>0) {
        int kind=strstr(r->text,"Standard orientation:") ? 1 : strstr(r->text,"Input orientation:") ? 0 : -1;
        if (kind<0) continue;
        if (!required_line(r,e) || !dashed(trim_text(r->text))) { if (!e->code) problem(r,e,"invalid Gaussian orientation header"); goto done; }
        bool angstrom=false; size_t headers=0;
        while ((rc=read_line(r,e))>0 && !dashed(trim_text(r->text))) {
            if (++headers>8) { problem(r,e,"invalid orientation header"); goto done; }
            if (strstr(r->text,"Coordinates (Angstroms)")) angstrom=true;
        }
        if (rc<=0 || !angstrom) { if (!e->code) problem(r,e,"orientation table must declare Coordinates (Angstroms)"); goto done; }
        size_t capacity=0;
        while ((rc=read_line(r,e))>0 && !dashed(trim_text(r->text))) {
            char *words[7]; size_t center,z; double type;
            if (split_words(r->text,words,7)!=6 || !integer(words[0],VR_MAX_ATOMS,&center) || center!=candidate.count+1 ||
                !integer(words[1],118,&z) || !z || !number(words[2],&type) || floor(type)!=type) {
                problem(r,e,"unsupported or malformed Gaussian orientation row (ghost/periodic centers unsupported)"); goto done;
            }
            Atom atom={.element=(unsigned)z};
            for (int d=0;d<3;++d) if (!number(words[3+d],&atom.position.x[d])) { problem(r,e,"invalid orientation coordinate"); goto done; }
            if (!append_atom(&candidate,&capacity,atom,r,e)) goto done;
        }
        if (rc<=0) { if (!e->code) problem(r,e,"truncated Gaussian orientation table"); goto done; }
        if (!accept_frame(r,o,&candidate,&selected[kind],frames[kind]++,e)) goto done;
    }
    if (rc<0) goto done;
    int kind=frames[1] ? 1 : 0;
    *s=selected[kind]; memset(&selected[kind],0,sizeof selected[kind]);
    ok=finish_frames(r,s,frames[kind],FORMAT_GAUSSIAN_OUTPUT,e);
done:
    structure_free(&candidate); structure_free(&selected[0]); structure_free(&selected[1]); return ok;
}

static bool block_unit(const char *text, double *scale)
{
    if (!*text || !strcasecmp(text,"[Angstrom]")) { *scale=1; return true; }
    if (!strcasecmp(text,"[Bohr]")) { *scale=BOHR_A; return true; }
    return false;
}
bool read_ams(Reader *r, const InputOptions *o, Structure *s, Error *e)
{
    if (!input_policy(o,e)) return false;
    Structure candidate={0}; size_t capacity=0,nv=0;
    bool in_system=false, have_system=false, closed=false, atoms=false, lattice=false, fractional=false, fraction_seen=false;
    double atom_scale=1, lattice_scale=1; int rc, block=0; bool ok=false;
    while ((rc=read_line(r,e))>0) {
        char *line=trim_text(r->text);
        if (!*line || *line=='#' || *line=='!') continue;
        char *comment=strpbrk(line,"#!"); if (comment) *comment='\0';
        line=trim_text(line);
        char *tail=line; while (*tail && !isspace((unsigned char)*tail)) ++tail;
        if (*tail) *tail++='\0';
        tail=trim_text(tail);
        if (!strcasecmp(line,"System")) {
            if (have_system || *tail) { problem(r,e,"support requires one unnamed AMS System block"); goto done; }
            have_system=in_system=true; continue;
        }
        if (!in_system) continue; /* Engine/task settings are outside the geometry contract. */
        if (!strcasecmp(line,"End")) {
            if (*tail) { problem(r,e,"unexpected text after End"); goto done; }
            if (block) block=0; else { in_system=false; closed=true; }
            continue;
        }
        if (block) {
            Atom atom={0};
            if (block==1) {
                atom.element=element_id(line);
                char *words[32]; size_t n=split_words(tail,words,32);
                if (!atom.element || n<3 || n>32) { problem(r,e,"invalid AMS atom row"); goto done; }
                for (int d=0;d<3;++d) if (!number(words[d],&atom.position.x[d])) { problem(r,e,"invalid AMS coordinate"); goto done; }
                /* Trailing key=value atom annotations do not change geometry. */
                for (size_t k=3;k<n;++k) if (!strchr(words[k],'=')) { problem(r,e,"unsupported AMS atom annotation"); goto done; }
                if (!append_atom(&candidate,&capacity,atom,r,e)) goto done;
            } else {
                if (nv==3) { problem(r,e,"too many AMS lattice vectors"); goto done; }
                char row[VR_MAX_LINE+2]; int written=snprintf(row,sizeof row,"%s %s",line,tail);
                if (written<0 || (size_t)written>=sizeof row || !vector_text(row,&candidate.lattice[nv])) { problem(r,e,"invalid AMS lattice vector"); goto done; }
                candidate.lattice[nv]=vmul(candidate.lattice[nv],lattice_scale);
                candidate.cell_present[nv]=candidate.pbc[nv]=true; ++nv;
            }
        } else if (!strcasecmp(line,"Atoms")) {
            if (atoms || !block_unit(tail,&atom_scale)) { problem(r,e,"duplicate Atoms or unsupported units"); goto done; }
            atoms=true; block=1;
        } else if (!strcasecmp(line,"Lattice")) {
            if (lattice || !block_unit(tail,&lattice_scale)) { problem(r,e,"duplicate Lattice or unsupported units"); goto done; }
            lattice=true; block=2;
        } else if (!strcasecmp(line,"FractionalCoords")) {
            if (fraction_seen || !bool_token(tail,&fractional)) { problem(r,e,"invalid FractionalCoords"); goto done; }
            fraction_seen=true;
        } else if (!strcasecmp(line,"Charge")) {
            double charge; if (!number(tail,&charge)) { problem(r,e,"invalid AMS charge"); goto done; }
        } else {
            problem(r,e,"unsupported AMS System keyword; use explicit Atoms/Lattice geometry"); goto done;
        }
    }
    if (rc<0) goto done;
    if (!have_system || !closed || in_system || block || !atoms || (lattice && !nv) || (fractional && !nv)) { problem(r,e,"incomplete AMS System geometry"); goto done; }
    if (fractional && ((nv==1 && (fabs(candidate.lattice[0].x[1])>1e-12 || fabs(candidate.lattice[0].x[2])>1e-12)) ||
        (nv==2 && (fabs(candidate.lattice[0].x[2])>1e-12 || fabs(candidate.lattice[1].x[2])>1e-12)))) {
        problem(r,e,"AMS fractional chains require x alignment and slabs require the xy plane"); goto done;
    }
    for (size_t i=0;i<candidate.count;++i) {
        Vec3 raw=candidate.atoms[i].position, p={{0,0,0}};
        for (int d=0;d<3;++d) {
            if (fractional && (size_t)d<nv) p=vadd(p,vmul(candidate.lattice[d],raw.x[d]));
            else p.x[d]+=raw.x[d]*atom_scale;
        }
        candidate.atoms[i].position=p;
    }
    if (!accept_frame(r,o,&candidate,s,0,e)) goto done;
    ok=finish_frames(r,s,1,FORMAT_AMS,e);
done:
    structure_free(&candidate); return ok;
}
