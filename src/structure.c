/* SPDX-License-Identifier: GPL-3.0-only */
#include "structure.h"
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
