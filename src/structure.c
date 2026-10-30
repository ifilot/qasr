/* SPDX-License-Identifier: GPL-3.0-only */
#include "structure.h"
#include "vec.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const symbols[] = {
    "?", "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar",
    "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr",
    "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe",
    "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu",
    "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac",
    "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"
};
unsigned element_id(const char *symbol)
{
    for (unsigned i = 1; i < sizeof symbols / sizeof *symbols; ++i)
        if (!strcmp(symbol, symbols[i])) return i;
    return 0;
}
const char *element_symbol(unsigned id) { return id <= 118 ? symbols[id] : "?"; }
double cell_volume(const Vec3 a[3])
{
    return a[0].x[0]*(a[1].x[1]*a[2].x[2]-a[1].x[2]*a[2].x[1])
         - a[0].x[1]*(a[1].x[0]*a[2].x[2]-a[1].x[2]*a[2].x[0])
         + a[0].x[2]*(a[1].x[0]*a[2].x[1]-a[1].x[1]*a[2].x[0]);
}
bool lattice_valid(const Vec3 a[3])
{
    double scale = 1;
    for (size_t i = 0; i < 3; ++i) {
        double norm = hypot(hypot(a[i].x[0], a[i].x[1]), a[i].x[2]);
        if (!isfinite(norm) || norm < 1e-12 || norm > 1e9) return false;
        scale *= norm;
    }
    double v = fabs(cell_volume(a));
    return isfinite(v) && v > 1e-12 * scale;
}
void structure_free(Structure *s) { free(s->atoms); memset(s, 0, sizeof *s); }

static const char *const format_names[] = {
    "auto", "poscar", "outcar", "xml", "xyz", "extxyz", "gaussian-input", "gaussian-output", "ams"
};
const char *format_name(Format f)
{
    return (unsigned)f < sizeof format_names/sizeof *format_names ? format_names[f] : "unknown";
}
bool format_parse(const char *name, Format *f)
{
    if (!strcmp(name,"contcar")) { *f=FORMAT_POSCAR; return true; }
    if (!strcmp(name,"gjf") || !strcmp(name,"com")) { *f=FORMAT_GAUSSIAN_INPUT; return true; }
    for (size_t i=0;i<sizeof format_names/sizeof *format_names;++i)
        if (!strcmp(name,format_names[i])) { *f=(Format)i; return true; }
    return false;
}

bool structure_basis(const Structure *s, Vec3 basis[3], Vec3 reciprocal[3], Error *e)
{
    int present[3], missing[3], n=0, m=0;
    for (int d=0;d<3;++d) {
        basis[d]=s->lattice[d];
        if (s->pbc[d] && !s->cell_present[d]) return fail(e,5,"periodic direction %d requires a cell vector",d);
        if (s->cell_present[d]) {
            double norm=length(basis[d]);
            if (!isfinite(norm) || norm<1e-12 || norm>1e9) return fail(e,5,"invalid cell vector %d",d);
            present[n++]=d;
        } else missing[m++]=d;
    }
    if (!n) {
        for (int d=0;d<3;++d) basis[d]=(Vec3){{d==0,d==1,d==2}};
    } else if (n==1) {
        Vec3 u=unit(basis[present[0]]);
        int axis=0;
        for (int d=1;d<3;++d) if (fabs(u.x[d])<fabs(u.x[axis])) axis=d;
        Vec3 seed={{0,0,0}}; seed.x[axis]=1;
        basis[missing[0]]=unit(cross(u,seed));
        basis[missing[1]]=unit(cross(u,basis[missing[0]]));
    } else if (n==2) {
        Vec3 normal=cross(unit(basis[present[0]]),unit(basis[present[1]]));
        if (length(normal)<=1e-12) return fail(e,5,"cell vectors are linearly dependent");
        basis[missing[0]]=unit(normal);
    }
    if (!lattice_valid(basis)) return fail(e,5,"cell vectors are degenerate or ill-conditioned");
    double volume=cell_volume(basis);
    reciprocal[0]=vmul(cross(basis[1],basis[2]),1/volume);
    reciprocal[1]=vmul(cross(basis[2],basis[0]),1/volume);
    reciprocal[2]=vmul(cross(basis[0],basis[1]),1/volume);
    return true;
}
bool structure_validate(const Structure *s, Error *e)
{
    if (!s->count || s->count>VR_MAX_ATOMS || !s->atoms) return fail(e,5,"invalid atom count");
    Vec3 basis[3], reciprocal[3];
    if (!structure_basis(s,basis,reciprocal,e)) return false;
    for (size_t i=0;i<s->count;++i) {
        if (!s->atoms[i].element || s->atoms[i].element>118) return fail(e,5,"invalid element at atom %zu",i);
        for (int d=0;d<3;++d)
            if (!isfinite(s->atoms[i].position.x[d]) || fabs(s->atoms[i].position.x[d])>1e12)
                return fail(e,5,"coordinate outside supported range at atom %zu",i);
    }
    return true;
}
