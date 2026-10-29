/* SPDX-License-Identifier: GPL-3.0-only */
#include "config.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool color(const char *s)
{
    size_t n = strlen(s);
    if ((n != 7 && n != 9) || *s != '#') return false;
    for (size_t i = 1; i < n; ++i) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static bool positive_json(const cJSON *j, double *out)
{
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble) || j->valuedouble <= 0) return false;
    *out = j->valuedouble;
    return true;
}

static bool size_json(const cJSON *j, size_t limit, size_t *out)
{
    double x;
    if (!positive_json(j, &x) || x > (double)limit || floor(x) != x) return false;
    *out = (size_t)x;
    return true;
}

static bool vector_json(const cJSON *j, Vec3 *out)
{
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) != 3) return false;
    for (int i = 0; i < 3; ++i) {
        const cJSON *v = cJSON_GetArrayItem(j, i);
        if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble)) return false;
        out->x[i] = v->valuedouble;
    }
    return true;
}

static bool species_text(const char *text, Config *c)
{
    if (strlen(text) >= 512) return false;
    char copy[512];
    strcpy(copy, text);
    c->species_count = 0;
    char *start = copy;
    for (;;) {
        char *comma = strchr(start, ',');
        if (comma) *comma = '\0';
        unsigned z = element_id(start);
        if (!z || c->species_count == VR_MAX_SPECIES) return false;
        c->species[c->species_count++] = z;
        if (!comma) return true;
        start = comma + 1;
    }
}

static bool vector_text(const char *text, Vec3 *out)
{
    if (strlen(text) >= 384) return false;
    char copy[384];
    strcpy(copy, text);
    char *p = copy;
    for (int i = 0; i < 3; ++i) {
        char *comma = strchr(p, ',');
        if ((i < 2) != (comma != NULL)) return false;
        if (comma) *comma = '\0';
        if (!number(p, &out->x[i])) return false;
        p = comma ? comma + 1 : NULL;
    }
    return true;
}

static bool repeat_text(const char *text, size_t out[3])
{
    if (strlen(text) >= 128) return false;
    char copy[128];
    strcpy(copy, text);
    char *p = copy;
    for (int i = 0; i < 3; ++i) {
        char *comma = strchr(p, ',');
        if ((i < 2) != (comma != NULL)) return false;
        if (comma) *comma = '\0';
        if (!integer(p, 1024, &out[i]) || !out[i]) return false;
        p = comma ? comma + 1 : NULL;
    }
    return true;
}

static bool expand_text(const char *text, size_t out[2])
{
    if (strlen(text) >= 128) return false;
    char copy[128]; strcpy(copy, text);
    char *comma = strchr(copy, ',');
    if (!comma || strchr(comma+1, ',')) return false;
    *comma = '\0';
    return integer(copy, 511, &out[0]) && integer(comma+1, 511, &out[1]);
}

static bool image_style(const char *text, PeriodicImageStyle *out)
{
    if (!strcmp(text, "normal")) *out = IMAGES_NORMAL;
    else if (!strcmp(text, "muted")) *out = IMAGES_MUTED;
    else if (!strcmp(text, "greyscale")) *out = IMAGES_GREYSCALE;
    else return false;
    return true;
}

static bool toggle(const char *s, bool *v)
{
    if (!strcmp(s, "on")) *v = true;
    else if (!strcmp(s, "off")) *v = false;
    else return false;
    return true;
}

static bool label(const char *s, int *v)
{
    if (!strcmp(s, "none")) *v = 0;
    else if (!strcmp(s, "element")) *v = 1;
    else if (!strcmp(s, "index")) *v = 2;
    else return false;
    return true;
}

static bool style(const char *s, RenderStyle *v)
{
    if (!strcmp(s,"shaded")) *v=STYLE_SHADED;
    else if (!strcmp(s,"illustrated")) *v=STYLE_ILLUSTRATED;
    else return false;
    return true;
}

static bool antialias(const char *s, Config *c)
{
    c->adaptive_aa=false;
    if (!strcmp(s,"native")) c->aa_scale=1;
    else if (!strcmp(s,"2x")) c->aa_scale=2;
    else if (!strcmp(s,"4x")) c->aa_scale=4;
    else if (!strcmp(s,"adaptive")) { c->aa_scale=2; c->adaptive_aa=true; }
    else return false;
    return true;
}

static bool backend(const char *s, RenderBackend *value)
{
    if (!strcmp(s,"zbuffer")) *value=BACKEND_ZBUFFER;
    else if (!strcmp(s,"cairo")) *value=BACKEND_CAIRO;
    else return false;
    return true;
}

static bool encoder(const char *s, PngEncoder *value)
{
    if (!strcmp(s,"libpng")) *value=ENCODER_LIBPNG;
    else if (!strcmp(s,"libdeflate")) *value=ENCODER_LIBDEFLATE;
    else if (!strcmp(s,"cairo")) *value=ENCODER_CAIRO;
    else return false;
    return true;
}

static bool png_filter(const char *s, PngFilter *value)
{
    if (!strcmp(s,"adaptive")) *value=FILTER_ADAPTIVE;
    else if (!strcmp(s,"none")) *value=FILTER_NONE;
    else if (!strcmp(s,"sub")) *value=FILTER_SUB;
    else if (!strcmp(s,"up")) *value=FILTER_UP;
    else return false;
    return true;
}

/* cJSON deliberately accepts some non-JSON number spellings. Check the lexical
 * grammar before decoding, and forbid NUL strings (C schema keys use strcmp). */
static bool json_lexical(const char *s)
{
    const char *p = s;
    while (*p) {
        if (*p == '"') {
            ++p;
            while (*p && *p != '"') {
                if ((unsigned char)*p < 32) return false;
                if (*p == '\\') {
                    ++p;
                    if (!*p) return false;
                    if (*p == 'u' && !strncmp(p + 1, "0000", 4)) return false;
                }
                ++p;
            }
            if (!*p) return false;
            ++p;
        } else if (*p == '-' || isdigit((unsigned char)*p)) {
            if (*p == '-') ++p;
            if (*p == '0') ++p;
            else {
                if (*p < '1' || *p > '9') return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (*p == '.') {
                ++p;
                if (!isdigit((unsigned char)*p)) return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (*p == 'e' || *p == 'E') {
                ++p;
                if (*p == '+' || *p == '-') ++p;
                if (!isdigit((unsigned char)*p)) return false;
                while (isdigit((unsigned char)*p)) ++p;
            }
            if (*p && !strchr(" \t\r\n,]}", *p)) return false;
        } else {
            if ((unsigned char)*p < 32 && !strchr("\t\r\n", *p)) return false;
            ++p;
        }
    }
    return true;
}

static bool unique_keys(const cJSON *j, Error *e)
{
    if (cJSON_IsObject(j)) {
        for (const cJSON *a = j->child; a; a = a->next)
            for (const cJSON *b = a->next; b; b = b->next)
                if (!strcmp(a->string, b->string)) return fail(e, 2, "config: duplicate key '%s'", a->string);
    }
    for (const cJSON *a = j->child; a; a = a->next) if (!unique_keys(a, e)) return false;
    return true;
}

static bool settings_group(const cJSON *group, const char *name, Config *c, Error *e)
{
    if (!cJSON_IsObject(group)) return fail(e, 2, "config.%s: expected object", name);
    for (const cJSON *j = group->child; j; j = j->next) {
        const char *k = j->string;
        bool valid = false;
        if (!strcmp(name, "image")) {
            if (!strcmp(k,"style")) valid=cJSON_IsString(j) && style(j->valuestring,&c->style);
            else if (!strcmp(k,"aa")) valid=cJSON_IsString(j) && antialias(j->valuestring,c);
            else if (!strcmp(k,"backend")) valid=cJSON_IsString(j) && backend(j->valuestring,&c->backend);
            else if (!strcmp(k,"png_encoder")) valid=cJSON_IsString(j) && encoder(j->valuestring,&c->png.encoder);
            else if (!strcmp(k,"png_filter")) valid=cJSON_IsString(j) && png_filter(j->valuestring,&c->png.filter);
            else if (!strcmp(k,"png_compression")) {
                valid=cJSON_IsString(j) && !strcmp(j->valuestring,"auto");
                if (valid) { c->png.compression=-1;continue; }
                valid=cJSON_IsNumber(j) && isfinite(j->valuedouble) &&
                    j->valuedouble>=0 && j->valuedouble<=9 && floor(j->valuedouble)==j->valuedouble;
                if (valid) c->png.compression=(int)j->valuedouble;
            }
            else if (!strcmp(k,"depth_fade")) {
                valid=cJSON_IsNumber(j) && isfinite(j->valuedouble) && j->valuedouble>=0 && j->valuedouble<=1;
                if (valid) c->depth_fade=j->valuedouble;
            }
            else if (!strcmp(k,"outline_width")) {
                valid=cJSON_IsNumber(j) && isfinite(j->valuedouble) && j->valuedouble>=0 && j->valuedouble<=8;
                if (valid) c->outline_width=j->valuedouble;
            }
            else if (!strcmp(k, "width")) valid = size_json(j, 16384, &c->width);
            else if (!strcmp(k, "height")) valid = size_json(j, 16384, &c->height);
            else if (!strcmp(k, "background") && cJSON_IsString(j) && color(j->valuestring)) {
                strcpy(c->background, j->valuestring); valid = true;
            }
        } else if (!strcmp(name, "camera")) {
            if (!strcmp(k, "projection")) valid = cJSON_IsString(j) && !strcmp(j->valuestring, "orthographic");
            else if (!strcmp(k, "view")) {
                c->view_top=cJSON_IsString(j) && !strcmp(j->valuestring,"top");
                valid=c->view_top || vector_json(j,&c->view);
            }
            else if (!strcmp(k, "up")) { valid = vector_json(j, &c->up); c->up_explicit=true; }
            else if (!strcmp(k, "margin")) {
                valid = cJSON_IsNumber(j) && isfinite(j->valuedouble) && j->valuedouble >= 0 && j->valuedouble < 0.5;
                if (valid) c->margin = j->valuedouble;
            }
        } else if (!strcmp(name, "geometry")) {
            if (!strcmp(k, "sphere_scale")) valid = positive_json(j, &c->sphere_scale);
            else if (!strcmp(k, "bond_radius_A")) valid = positive_json(j, &c->bond_radius);
            else if (!strcmp(k, "image_colors")) valid = cJSON_IsString(j) && image_style(j->valuestring, &c->image_colors);
            else if (!strcmp(k, "expand_xy") && cJSON_IsArray(j) && cJSON_GetArraySize(j) == 2) {
                valid = true;
                for (int i = 0; i < 2; ++i) {
                    const cJSON *v = cJSON_GetArrayItem(j, i);
                    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble < 0 ||
                        v->valuedouble > 511 || floor(v->valuedouble) != v->valuedouble) valid = false;
                    else c->expand_xy[i] = (size_t)v->valuedouble;
                }
            }
            else if (!strcmp(k, "repeat") && cJSON_IsArray(j) && cJSON_GetArraySize(j) == 3) {
                valid = true;
                for (int i = 0; i < 3; ++i) if (!size_json(cJSON_GetArrayItem(j, i), 1024, &c->repeat[i])) valid = false;
            }
        } else if (!strcmp(name, "overlays")) {
            if (!strcmp(k, "labels")) valid = cJSON_IsString(j) && label(j->valuestring, &c->labels);
            else if (!strcmp(k,"cell_width")) valid=positive_json(j,&c->cell_width) && c->cell_width<=32;
            else if (cJSON_IsBool(j)) {
                bool b = cJSON_IsTrue(j);
                if (!strcmp(k, "cell")) { c->cell = b; valid = true; }
                else if (!strcmp(k, "axes")) { c->axes = b; valid = true; }
                else if (!strcmp(k, "legend")) { c->legend = b; valid = true; }
                else if (!strcmp(k, "scale_bar")) { c->scale_bar = b; valid = true; }
            }
        }
        if (!valid) return fail(e, 2, "config.%s.%s: unknown key or invalid value", name, k);
    }
    return true;
}

static bool element_settings(const cJSON *group, Config *c, Error *e)
{
    if (!cJSON_IsObject(group)) return fail(e, 2, "config.elements: expected object");
    for (const cJSON *j = group->child; j; j = j->next) {
        unsigned z = element_id(j->string);
        if (!z || !cJSON_IsObject(j)) return fail(e, 2, "config.elements.%s: unknown element or invalid object", j->string);
        for (const cJSON *v = j->child; v; v = v->next) {
            if (!strcmp(v->string, "radius_A") && positive_json(v, &c->element_radius[z])) continue;
            if (!strcmp(v->string, "color") && cJSON_IsString(v) && color(v->valuestring)) {
                strcpy(c->element_color[z], v->valuestring); continue;
            }
            return fail(e, 2, "config.elements.%s.%s: unknown key or invalid value", j->string, v->string);
        }
    }
    return true;
}

static bool cutoff_settings(const cJSON *group, Config *c, Error *e)
{
    if (!cJSON_IsObject(group)) return fail(e, 2, "config.bond_cutoffs_A: expected object");
    for (const cJSON *j = group->child; j; j = j->next) {
        char pair[8];
        if (strlen(j->string) >= sizeof pair) return fail(e, 2, "config: invalid element pair '%s'", j->string);
        strcpy(pair, j->string);
        char *dash = strchr(pair, '-');
        if (!dash) return fail(e, 2, "config: invalid element pair '%s'", pair);
        *dash++ = '\0';
        unsigned a = element_id(pair), b = element_id(dash);
        double d;
        if (!a || !b || c->cutoffs[a][b] || !positive_json(j, &d))
            return fail(e, 2, "config.bond_cutoffs_A.%s: unknown pair, duplicate unordered pair, or invalid cutoff", j->string);
        c->cutoffs[a][b] = c->cutoffs[b][a] = d;
    }
    return true;
}

static bool read_config(const char *path, Config *c, Error *e)
{
    FILE *f = fopen(path, "rb");
    if (!f) return fail(e, 7, "%s: cannot open config", path);
    char *text = malloc(VR_MAX_CONFIG_BYTES + 1);
    if (!text) { fclose(f); return fail(e, 6, "config: allocation failed"); }
    size_t n = fread(text, 1, VR_MAX_CONFIG_BYTES, f);
    bool io = ferror(f) != 0;
    int extra = fgetc(f);
    io = io || ferror(f) != 0;
    if (fclose(f)) io = true;
    if (io || extra != EOF || memchr(text, 0, n)) {
        free(text); return fail(e, io ? 7 : 2, "%s: read failure, size limit exceeded, or embedded NUL", path);
    }
    text[n] = '\0';
    cJSON *root = json_lexical(text) ? cJSON_ParseWithOpts(text, NULL, 1) : NULL;
    free(text);
    if (!root) return fail(e, 2, "%s: invalid JSON (depth limit 32)", path);
    bool ok = cJSON_IsObject(root) && unique_keys(root, e);
    if (!ok && !e->code) fail(e, 2, "%s: config root must be an object", path);
    for (const cJSON *j = root->child; ok && j; j = j->next) {
        const char *k = j->string;
        if (!strcmp(k, "image") || !strcmp(k, "camera") || !strcmp(k, "geometry") || !strcmp(k, "overlays"))
            ok = settings_group(j, k, c, e);
        else if (!strcmp(k, "elements")) ok = element_settings(j, c, e);
        else if (!strcmp(k, "bond_cutoffs_A")) ok = cutoff_settings(j, c, e);
        else if (!strcmp(k, "species") && cJSON_IsArray(j)) {
            int count = cJSON_GetArraySize(j);
            ok = count > 0 && count <= (int)VR_MAX_SPECIES;
            c->species_count = (size_t)count;
            for (int i = 0; ok && i < count; ++i) {
                const cJSON *v = cJSON_GetArrayItem(j, i);
                unsigned z = cJSON_IsString(v) ? element_id(v->valuestring) : 0;
                if (!z) ok = false;
                else c->species[i] = z;
            }
            if (!ok) fail(e, 2, "config.species: expected 1..118 element symbols");
        } else ok = fail(e, 2, "config: unknown key or invalid value '%s'", k);
    }
    cJSON_Delete(root);
    return ok;
}

static bool validate(Config *c, Error *e)
{
    bool expanded = c->expand_xy[0] || c->expand_xy[1];
    if (expanded) {
        if (c->repeat[0] != 1 || c->repeat[1] != 1 || c->repeat[2] != 1)
            return fail(e, 2, "--expand-xy cannot be combined with nontrivial --repeat; select one expansion");
        c->repeat[0] = 2*c->expand_xy[0]+1;
        c->repeat[1] = 2*c->expand_xy[1]+1;
    }
    if (c->image_colors == IMAGES_AUTO) c->image_colors = expanded ? IMAGES_MUTED : IMAGES_NORMAL;
    if (c->png.filter==FILTER_DEFAULT)
        c->png.filter=c->png.encoder==ENCODER_LIBDEFLATE ? FILTER_NONE : FILTER_ADAPTIVE;
    if (c->png.compression==-1 && c->png.encoder!=ENCODER_LIBDEFLATE) c->png.compression=1;
    if (c->png.encoder==ENCODER_CAIRO && (c->png.compression!=1 || c->png.filter!=FILTER_ADAPTIVE))
        return fail(e,2,"Cairo PNG uses internal compression/filter settings; omit PNG tuning overrides");
    if (c->width > 67108864u / c->height) return fail(e, 2, "image exceeds 67108864 pixels");
    size_t product = 1;
    for (int i = 0; i < 3; ++i) {
        if (product > VR_MAX_INSTANCES / c->repeat[i]) return fail(e, 2, "repeat exceeds instance limit");
        product *= c->repeat[i];
    }
    Vec3 v = c->view, u = c->up;
    double vn = hypot(hypot(v.x[0], v.x[1]), v.x[2]);
    double un = hypot(hypot(u.x[0], u.x[1]), u.x[2]);
    if (!isfinite(vn) || !isfinite(un) || vn == 0 || un == 0) return fail(e, 2, "view/up must be finite nonzero vectors");
    if (c->view_top) return true; /* The surface normal is known after parsing the lattice. */
    for (int i = 0; i < 3; ++i) { v.x[i] /= vn; u.x[i] /= un; }
    double cx = v.x[1]*u.x[2] - v.x[2]*u.x[1];
    double cy = v.x[2]*u.x[0] - v.x[0]*u.x[2];
    double cz = v.x[0]*u.x[1] - v.x[1]*u.x[0];
    if (hypot(hypot(cx, cy), cz) < 1e-12) return fail(e, 2, "view and up must not be collinear");
    return true;
}

bool config_parse(int argc, char **argv, Config *c, Error *e)
{
    memset(c, 0, sizeof *c);
    c->width = c->height = 512;
    c->aa_scale=2;
    c->png.compression=-1;c->png.filter=FILTER_DEFAULT;
#ifdef VR_HAVE_LIBDEFLATE
    c->png.encoder=ENCODER_LIBDEFLATE;
#endif
    c->repeat[0] = c->repeat[1] = c->repeat[2] = 1;
    c->view = (Vec3){{1, 1, 1}}; c->up = (Vec3){{0, 0, 1}};
    c->margin = 0.06; c->sphere_scale = 1; c->bond_radius = 0.12;
    c->outline_width=1;
    c->cell_width=2;
    strcpy(c->background, "#ffffff"); c->cell = true; c->final_frame = true;
    const char *config_path = NULL;
    /* Apply JSON first regardless of its position among explicit options. */
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) break;
        if (!strcmp(argv[i], "--config")) {
            if (config_path || i + 1 == argc) return fail(e, 2, "--config requires exactly one file");
            config_path = argv[++i];
        } else if (argv[i][0] == '-' && strcmp(argv[i], "--inspect") && strcmp(argv[i], "--inspect-scene") && strcmp(argv[i], "--recover") && strcmp(argv[i], "--benchmark-stages")) {
            if (i + 1 < argc) ++i;
        }
    }
    if (config_path && !read_config(config_path, c, e)) return false;
    bool positional = false;
    const char *seen[64]; size_t nseen = 0;
    for (int i = 1; i < argc; ++i) {
        const char *k = argv[i];
        if (!positional && !strcmp(k, "--")) { positional = true; continue; }
        if (positional || k[0] != '-') {
            if (c->input) return fail(e, 2, "exactly one input is required");
            c->input = k; continue;
        }
        const char *canonical = !strcmp(k, "--output") ? "-o" : k;
        for (size_t j = 0; j < nseen; ++j) if (!strcmp(canonical, seen[j])) return fail(e, 2, "duplicate option '%s'", k);
        if (nseen == 64) return fail(e, 2, "too many options");
        seen[nseen++] = canonical;
        if (!strcmp(k, "--inspect")) { c->inspect = true; continue; }
        if (!strcmp(k, "--inspect-scene")) { c->inspect = true; c->inspect_scene = true; continue; }
        if (!strcmp(k, "--recover")) { c->recover = true; continue; }
        if (!strcmp(k, "--benchmark-stages")) { c->benchmark = true; continue; }
        if (i + 1 == argc) return fail(e, 2, "option '%s' requires a value", k);
        const char *v = argv[++i];
        bool ok = false;
        if (!strcmp(k, "--config")) ok = true;
        else if (!strcmp(k, "-o") || !strcmp(k, "--output")) { c->output = v; ok = *v && strcmp(v, "-"); }
        else if (!strcmp(k, "--width")) ok = integer(v, 16384, &c->width) && c->width;
        else if (!strcmp(k, "--height")) ok = integer(v, 16384, &c->height) && c->height;
        else if (!strcmp(k, "--repeat")) ok = repeat_text(v, c->repeat);
        else if (!strcmp(k, "--expand-xy")) ok = expand_text(v, c->expand_xy);
        else if (!strcmp(k, "--image-colors")) ok = image_style(v, &c->image_colors);
        else if (!strcmp(k,"--style")) ok=style(v,&c->style);
        else if (!strcmp(k,"--aa")) ok=antialias(v,c);
        else if (!strcmp(k,"--backend")) ok=backend(v,&c->backend);
        else if (!strcmp(k,"--png-encoder")) ok=encoder(v,&c->png.encoder);
        else if (!strcmp(k,"--png-filter")) ok=png_filter(v,&c->png.filter);
        else if (!strcmp(k,"--png-compression")) {
            size_t level;
            ok=!strcmp(v,"auto");
            if (ok) c->png.compression=-1;
            else { ok=integer(v,9,&level);if (ok) c->png.compression=(int)level; }
        }
        else if (!strcmp(k,"--outline-width")) ok=number(v,&c->outline_width) && c->outline_width>=0 && c->outline_width<=8;
        else if (!strcmp(k,"--cell-width")) ok=number(v,&c->cell_width) && c->cell_width>0 && c->cell_width<=32;
        else if (!strcmp(k, "--view")) {
            c->view_top=!strcmp(v,"top");
            ok=c->view_top || vector_text(v,&c->view);
        }
        else if (!strcmp(k, "--up")) { ok = vector_text(v, &c->up); c->up_explicit=true; }
        else if (!strcmp(k, "--sphere-scale")) ok = number(v, &c->sphere_scale) && c->sphere_scale > 0;
        else if (!strcmp(k, "--bond-radius")) ok = number(v, &c->bond_radius) && c->bond_radius > 0;
        else if (!strcmp(k, "--background")) { ok = color(v); if (ok) strcpy(c->background, v); }
        else if (!strcmp(k, "--cell")) ok = toggle(v, &c->cell);
        else if (!strcmp(k, "--axes")) ok = toggle(v, &c->axes);
        else if (!strcmp(k, "--legend")) ok = toggle(v, &c->legend);
        else if (!strcmp(k, "--scale-bar")) ok = toggle(v, &c->scale_bar);
        else if (!strcmp(k, "--labels")) ok = label(v, &c->labels);
        else if (!strcmp(k, "--species")) ok = species_text(v, c);
        else if (!strcmp(k, "--frame")) {
            c->final_frame = !strcmp(v, "final");
            ok = c->final_frame || integer(v, VR_MAX_FRAMES - 1, &c->frame);
        } else if (!strcmp(k, "--threads")) ok = !strcmp(v, "auto") || (integer(v, 256, &c->threads) && c->threads);
        else if (!strcmp(k, "--format")) {
            ok = true;
            if (!strcmp(v, "auto")) c->format = FORMAT_AUTO;
            else if (!strcmp(v, "poscar") || !strcmp(v, "contcar")) c->format = FORMAT_POSCAR;
            else if (!strcmp(v, "outcar")) c->format = FORMAT_OUTCAR;
            else if (!strcmp(v, "xml")) c->format = FORMAT_XML;
            else ok = false;
        }
        if (!ok) return fail(e, 2, "unknown option or invalid value: %s %s", k, v);
    }
    if (!c->input) return fail(e, 2, "input required; use --help for usage");
    if (!c->inspect && !c->output) return fail(e, 2, "-o OUTPUT.png or --inspect is required");
    if (c->inspect && c->output) return fail(e, 2, "--inspect writes JSON to stdout; omit -o");
    return validate(c, e);
}
