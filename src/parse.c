/* SPDX-License-Identifier: GPL-3.0-only */
#include "parse.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    return s;
}

static bool required(Reader *r, Error *e)
{
    int rc = read_line(r, e);
    if (!rc) return fail(e, 4, "%s:%zu: unexpected end of geometry", r->path, r->line + 1);
    return rc > 0;
}

static bool numeric_line(char *line, double *values, size_t min, size_t max)
{
    char *words[8];
    size_t n = split_words(line, words, 8);
    if (n < min || n > max) return false;
    for (size_t i = 0; i < n; ++i) if (!number(words[i], &values[i])) return false;
    return true;
}

static bool counts_line(char *line, size_t counts[VR_MAX_SPECIES], size_t *n, size_t *total)
{
    char *words[VR_MAX_SPECIES];
    *n = split_words(line, words, VR_MAX_SPECIES);
    *total = 0;
    if (!*n || *n > VR_MAX_SPECIES) return false;
    for (size_t i = 0; i < *n; ++i) {
        if (!integer(words[i], VR_MAX_ATOMS, &counts[i]) || counts[i] > VR_MAX_ATOMS - *total) return false;
        *total += counts[i];
    }
    return *total > 0;
}

static bool allocate_atoms(Structure *s, size_t count, const unsigned *species, const size_t *counts, size_t ns, Error *e)
{
    if (count > SIZE_MAX / sizeof *s->atoms) return fail(e, 6, "atom allocation overflow");
    s->atoms = calloc(count, sizeof *s->atoms);
    if (!s->atoms) return fail(e, 6, "cannot allocate %zu atoms", count);
    s->count = count;
    size_t at = 0;
    for (size_t i = 0; i < ns; ++i)
        for (size_t j = 0; j < counts[i]; ++j) s->atoms[at++].element = species[i];
    return true;
}

static bool parse_poscar(Reader *r, const Config *c, Structure *s, Error *e)
{
    if (!required(r, e) || !required(r, e)) return false; /* title, scale */
    double scale[3] = {0};
    char *words[VR_MAX_SPECIES];
    size_t n = split_words(r->text, words, VR_MAX_SPECIES);
    if (n != 1 && n != 3) return fail(e, 4, "%s:%zu: POSCAR requires one or three scaling factors", r->path, r->line);
    for (size_t i = 0; i < n; ++i)
        if (!number(words[i], &scale[i]) || scale[i] == 0 || (n == 3 && scale[i] < 0))
            return fail(e, 4, "%s:%zu: invalid POSCAR scaling factor", r->path, r->line);
    for (size_t i = 0; i < 3; ++i) {
        if (!required(r, e)) return false;
        if (!numeric_line(r->text, s->lattice[i].x, 3, 3)) return fail(e, 4, "%s:%zu: expected three lattice components", r->path, r->line);
    }
    if (!lattice_valid(s->lattice)) return fail(e, 5, "%s:%zu: degenerate or unsupported lattice", r->path, r->line);
    if (n == 1) {
        if (scale[0] < 0) scale[0] = cbrt(-scale[0] / fabs(cell_volume(s->lattice)));
        scale[1] = scale[2] = scale[0];
    }
    for (size_t i = 0; i < 3; ++i)
        for (size_t j = 0; j < 3; ++j) s->lattice[i].x[j] *= scale[j];
    if (!lattice_valid(s->lattice)) return fail(e, 5, "%s:%zu: scaled lattice is degenerate or outside limits", r->path, r->line);
    if (!required(r, e)) return false;
    unsigned species[VR_MAX_SPECIES] = {0};
    size_t ns = split_words(r->text, words, VR_MAX_SPECIES);
    if (!ns || ns > VR_MAX_SPECIES) return fail(e, 4, "%s:%zu: invalid species/count line", r->path, r->line);
    size_t dummy;
    bool old = integer(words[0], VR_MAX_ATOMS, &dummy);
    size_t counts[VR_MAX_SPECIES], total = 0;
    if (old) {
        for (size_t i = 0; i < ns; ++i) {
            if (!integer(words[i], VR_MAX_ATOMS, &counts[i]) || counts[i] > VR_MAX_ATOMS - total)
                return fail(e, 4, "%s:%zu: invalid atom counts", r->path, r->line);
            total += counts[i];
        }
        if (!c->species_count) return fail(e, 5, "%s:%zu: POSCAR without element names requires --species or config.species", r->path, r->line);
    } else {
        for (size_t i = 0; i < ns; ++i) {
            species[i] = element_id(words[i]);
            if (!species[i]) return fail(e, 5, "%s:%zu: unknown element '%s'; use exact chemical symbols", r->path, r->line, words[i]);
        }
        if (!required(r, e)) return false;
        size_t nc;
        if (!counts_line(r->text, counts, &nc, &total) || nc != ns) return fail(e, 4, "%s:%zu: species/count mismatch or invalid atom counts", r->path, r->line);
    }
    if (!total) return fail(e, 4, "%s:%zu: structure contains no atoms", r->path, r->line);
    if (c->species_count) {
        if (c->species_count != ns) return fail(e, 5, "%s:%zu: explicit species count mismatch", r->path, r->line);
        for (size_t i = 0; i < ns; ++i) {
            if (!old && species[i] != c->species[i]) return fail(e, 5, "%s:%zu: explicit species conflicts with POSCAR names", r->path, r->line);
            species[i] = c->species[i];
        }
    }
    if (!required(r, e)) return false;
    char *mode = trim(r->text);
    bool selective = *mode == 's' || *mode == 'S';
    if (selective) { if (!required(r, e)) return false; mode = trim(r->text); }
    bool cartesian = *mode == 'c' || *mode == 'C' || *mode == 'k' || *mode == 'K';
    /* Deliberately reject misspelled modes instead of interpreting arbitrary text as Direct. */
    if (!cartesian && *mode != 'd' && *mode != 'D') return fail(e, 4, "%s:%zu: expected Direct or Cartesian coordinate mode", r->path, r->line);
    if (!allocate_atoms(s, total, species, counts, ns, e)) return false;
    for (size_t i = 0; i < total; ++i) {
        if (!required(r, e)) return false;
        n = split_words(r->text, words, VR_MAX_SPECIES);
        if (n < (selective ? 6u : 3u) || n > VR_MAX_SPECIES) return fail(e, 4, "%s:%zu: incomplete atom coordinates/flags", r->path, r->line);
        double p[3];
        for (size_t j = 0; j < 3; ++j) {
            if (!number(words[j], &p[j])) return fail(e, 4, "%s:%zu: non-finite or invalid atom coordinate", r->path, r->line);
            if (selective && (strlen(words[j + 3]) != 1 || !strchr("TtFf", words[j + 3][0])))
                return fail(e, 4, "%s:%zu: invalid selective dynamics flag", r->path, r->line);
        }
        for (size_t j = 0; j < 3; ++j) {
            double x = cartesian ? p[j] * scale[j] : p[0]*s->lattice[0].x[j] + p[1]*s->lattice[1].x[j] + p[2]*s->lattice[2].x[j];
            if (!isfinite(x) || fabs(x) > 1e12) return fail(e, 5, "%s:%zu: Cartesian coordinate outside supported range", r->path, r->line);
            s->atoms[i].position.x[j] = x;
        }
    }
    /* Geometry-only parser ignores velocities/predictor data, but enforces text limits to EOF. */
    int rc;
    while ((rc = read_line(r, e)) > 0) {}
    if (rc < 0) return false;
    if (!c->final_frame && c->frame != 0) return fail(e, 4, "%s: POSCAR has only frame 0", r->path);
    if (c->recover) return fail(e, 2, "--recover is only meaningful for OUTCAR");
    s->format = FORMAT_POSCAR; s->frame_count = 1;
    return true;
}

#define MAX_METADATA (VR_MAX_SPECIES * 4)
typedef struct { unsigned elements[MAX_METADATA]; size_t n; } SpeciesMetadata;

static bool add_metadata(SpeciesMetadata *m, const char *symbol, Reader *r, Error *e)
{
    char base[3] = {0};
    size_t n = 0;
    while (isalpha((unsigned char)symbol[n]) && n < 2) { base[n] = symbol[n]; ++n; }
    unsigned z = element_id(base);
    if (!z || (symbol[n] && symbol[n] != '_' && symbol[n] != ':' && !isspace((unsigned char)symbol[n])))
        return fail(e, 5, "%s:%zu: unrecognized OUTCAR species '%s'", r->path, r->line, symbol);
    if (m->n == MAX_METADATA) return fail(e, 4, "%s:%zu: excessive OUTCAR species metadata", r->path, r->line);
    m->elements[m->n++] = z;
    return true;
}

static bool resolve_species(const SpeciesMetadata *m, size_t ns, unsigned *species, bool *have, Reader *r, Error *e)
{
    if (!m->n) return true;
    if (m->n % ns) return fail(e, 5, "%s:%zu: OUTCAR species metadata/count mismatch", r->path, r->line);
    for (size_t i = 0; i < m->n; ++i) {
        unsigned z = m->elements[i];
        if ((i >= ns && z != m->elements[i % ns]) || (*have && species[i % ns] != z))
            return fail(e, 5, "%s:%zu: conflicting OUTCAR species metadata", r->path, r->line);
    }
    memcpy(species, m->elements, ns * sizeof *species);
    *have = true;
    return true;
}

static bool separator(const char *s)
{
    size_t n = 0;
    for (; *s; ++s) {
        if (*s == '-') ++n;
        else if (!isspace((unsigned char)*s)) return false;
    }
    return n >= 3;
}

/* Only a short numeric row at physical EOF is recoverable. An invalid token
 * (NaN, text, etc.) remains malformed even when --recover is requested. */
static bool partial_row(Reader *r, const char *original)
{
    if (!r->unterminated) return false;
    char copy[VR_MAX_LINE + 2];
    strcpy(copy, original);
    char *words[6];
    size_t n = split_words(copy, words, 6);
    if (n >= 6) return false;
    for (size_t i = 0; i < n; ++i) {
        double value;
        if (!number(words[i], &value)) return false;
    }
    return true;
}

static bool parse_outcar(Reader *r, const Config *c, Structure *s, Error *e)
{
    SpeciesMetadata titel = {0}, vrhfin = {0}, potcar = {0};
    size_t counts[VR_MAX_SPECIES] = {0}, ns = 0, total = 0, nions = 0;
    Vec3 lattice[3] = {0};
    bool have_lattice = false, footer = false, completed = false, incomplete = false;
    Structure candidate = {0};
    size_t frames = 0;
    int rc;
    bool ok = false;
    while ((rc = read_line(r, e)) > 0) {
        char *line = trim(r->text);
        if (!strncmp(line, "vasp.", 5) && frames) { fail(e, 4, "%s:%zu: concatenated OUTCAR unsupported", r->path, r->line); goto done; }
        if (strstr(line, "General timing and accounting informations for this job")) { footer = true; continue; }
        const char footer_end[] = "Voluntary context switches:";
        if (footer && !strncmp(line, footer_end, sizeof footer_end - 1)) {
            size_t switches;
            char *words[2];
            size_t n = split_words(line + sizeof footer_end - 1, words, 2);
            if (n != 1 || !integer(words[0], SIZE_MAX, &switches)) {
                fail(e, 4, "%s:%zu: invalid completion footer", r->path, r->line); goto done;
            }
            completed = true; continue;
        }
        if (!strncmp(line, "TITEL", 5) && strchr(line, '=')) {
            if (frames) { fail(e, 4, "%s:%zu: species metadata after geometry (concatenated OUTCAR unsupported)", r->path, r->line); goto done; }
            char *words[8];
            size_t n = split_words(strchr(line, '=') + 1, words, 8);
            if (n < 2 || n > 8 || !add_metadata(&titel, words[1], r, e)) {
                if (!e->code) fail(e, 4, "%s:%zu: malformed TITEL", r->path, r->line);
                goto done;
            }
        } else if (!strncmp(line, "VRHFIN", 6) && strchr(line, '=')) {
            if (frames || !add_metadata(&vrhfin, trim(strchr(line, '=') + 1), r, e)) {
                if (!e->code) fail(e, 4, "%s:%zu: late OUTCAR species metadata", r->path, r->line);
                goto done;
            }
        } else if (!strncmp(line, "POTCAR:", 7)) {
            char *words[8];
            size_t n = split_words(line + 7, words, 8);
            if (frames || n < 2 || n > 8 || !add_metadata(&potcar, words[1], r, e)) {
                if (!e->code) fail(e, 4, "%s:%zu: malformed/late POTCAR metadata", r->path, r->line);
                goto done;
            }
        } else if (strstr(line, "ions per type") && strchr(line, '=')) {
            size_t next_counts[VR_MAX_SPECIES], next_ns, next_total;
            if (!counts_line(strchr(line, '=') + 1, next_counts, &next_ns, &next_total)) {
                fail(e, 4, "%s:%zu: invalid ions per type", r->path, r->line); goto done;
            }
            if (ns && (ns != next_ns || memcmp(counts, next_counts, ns * sizeof *counts))) {
                fail(e, 4, "%s:%zu: changed species counts", r->path, r->line); goto done;
            }
            ns = next_ns; total = next_total;
            memcpy(counts, next_counts, ns * sizeof *counts);
        } else if (strstr(line, "NIONS") && strchr(strstr(line, "NIONS"), '=')) {
            char *words[2];
            size_t n = split_words(strchr(strstr(line, "NIONS"), '=') + 1, words, 2);
            size_t next;
            if (!n || !integer(words[0], VR_MAX_ATOMS, &next) || !next || (nions && nions != next)) {
                fail(e, 4, "%s:%zu: invalid or changing NIONS", r->path, r->line); goto done;
            }
            nions = next;
        } else if (strstr(line, "direct lattice vectors") && strstr(line, "reciprocal lattice vectors")) {
            for (size_t i = 0; i < 3; ++i) {
                if (!required(r, e)) {
                    if (!e->code || e->code == 4) incomplete = feof(r->file) != 0;
                    if (incomplete) goto end_of_stream;
                    goto done;
                }
                double values[6];
                bool partial = partial_row(r, r->text);
                if (!numeric_line(r->text, values, 6, 6)) {
                    if (partial) { incomplete = true; goto end_of_stream; }
                    fail(e, 4, "%s:%zu: malformed OUTCAR lattice row", r->path, r->line); goto done;
                }
                for (size_t j = 0; j < 3; ++j) lattice[i].x[j] = values[j];
            }
            if (!lattice_valid(lattice)) { fail(e, 5, "%s:%zu: degenerate OUTCAR lattice", r->path, r->line); goto done; }
            have_lattice = true;
            completed = false; footer = false;
        } else if (!strncmp(line, "POSITION", 8) && strstr(line, "TOTAL-FORCE")) {
            completed = false; footer = false;
            if (!ns || !have_lattice || (nions && nions != total)) { fail(e, 4, "%s:%zu: missing/inconsistent lattice or species counts before POSITION", r->path, r->line); goto done; }
            if (frames == VR_MAX_FRAMES) { fail(e, 4, "%s:%zu: ionic frame limit exceeded", r->path, r->line); goto done; }
            unsigned species[VR_MAX_SPECIES] = {0};
            bool have_species = false;
            if (!resolve_species(&titel, ns, species, &have_species, r, e) ||
                !resolve_species(&vrhfin, ns, species, &have_species, r, e) ||
                !resolve_species(&potcar, ns, species, &have_species, r, e)) goto done;
            if (c->species_count) {
                if (c->species_count != ns || (have_species && memcmp(c->species, species, ns * sizeof *species))) {
                    fail(e, 5, "%s:%zu: explicit species conflicts with OUTCAR metadata", r->path, r->line); goto done;
                }
                memcpy(species, c->species, ns * sizeof *species); have_species = true;
            }
            if (!have_species) { fail(e, 5, "%s:%zu: missing OUTCAR element identities; provide --species", r->path, r->line); goto done; }
            if (!candidate.atoms && !allocate_atoms(&candidate, total, species, counts, ns, e)) goto done;
            memcpy(candidate.lattice, lattice, sizeof lattice);
            if (!required(r, e)) { incomplete = feof(r->file) != 0; if (incomplete) goto end_of_stream; goto done; }
            if (!separator(r->text)) { fail(e, 4, "%s:%zu: expected POSITION table separator", r->path, r->line); goto done; }
            for (size_t i = 0; i < total; ++i) {
                if (!required(r, e)) { incomplete = feof(r->file) != 0; if (incomplete) goto end_of_stream; goto done; }
                double values[6];
                bool partial = partial_row(r, r->text);
                if (!numeric_line(r->text, values, 6, 6)) {
                    if (partial) { incomplete = true; goto end_of_stream; }
                    fail(e, 4, "%s:%zu: malformed POSITION row in ionic frame %zu", r->path, r->line, frames); goto done;
                }
                for (size_t j = 0; j < 3; ++j) {
                    if (fabs(values[j]) > 1e12) { fail(e, 5, "%s:%zu: coordinate outside supported range", r->path, r->line); goto done; }
                    candidate.atoms[i].position.x[j] = values[j];
                }
            }
            /* A closing rule proves the table has exactly the declared number of rows. */
            if (!required(r, e)) { incomplete = feof(r->file) != 0; if (incomplete) goto end_of_stream; goto done; }
            if (!separator(r->text)) { fail(e, 4, "%s:%zu: missing closing POSITION separator or count mismatch", r->path, r->line); goto done; }
            if (c->final_frame || frames == c->frame) {
                if (!s->atoms && !allocate_atoms(s, total, species, counts, ns, e)) goto done;
                memcpy(s->atoms, candidate.atoms, total * sizeof *s->atoms);
                memcpy(s->lattice, candidate.lattice, sizeof s->lattice);
                s->frame = frames;
            }
            ++frames;
        }
    }
    if (rc < 0) goto done;
end_of_stream:
    if (nions && nions != total) { fail(e, 4, "%s: NIONS and species counts disagree", r->path); goto done; }
    if (!s->atoms) { fail(e, 4, "%s: requested ionic frame not found (%zu complete frames)", r->path, frames); goto done; }
    if (!completed || incomplete) {
        if (!c->recover) { fail(e, 4, "%s: unfinished OUTCAR; %zu complete ionic frames; use --recover to accept a complete snapshot", r->path, frames); goto done; }
        s->recovered = true;
        e->code = 0; e->message[0] = '\0';
    }
    s->frame_count = frames; s->format = FORMAT_OUTCAR;
    ok = true;
done:
    structure_free(&candidate);
    return ok;
}

static bool detect_format(Reader *r, Format *format, Error *e)
{
    /* Probe declarations, BOMs and comments, then prefer a valid POSCAR
     * numeric header: its unrestricted title can itself begin with '<'. */
    unsigned char prefix[4096];
    size_t nread = fread(prefix, 1, sizeof prefix, r->file), at = 0;
    if (ferror(r->file)) return fail(e, 7, "%s: input read error", r->path);
    if (nread >= 3 && !memcmp(prefix, "\xef\xbb\xbf", 3)) at = 3;
    while (at < nread && isspace(prefix[at])) ++at;
    bool xml = at < nread && prefix[at] == '<';
    if (fseek(r->file, 0, SEEK_SET)) return fail(e, 7, "%s: input must be seekable for auto detection; use --format", r->path);
    bool poscar = true, outcar = false;
    for (size_t i = 0; i < 64; ++i) {
        int rc = read_line(r, e);
        if (rc < 0) return false;
        if (!rc) { if (i < 5) poscar = false; break; }
        char *line = trim(r->text);
        if (!strncmp(line, "vasp.", 5) && isdigit((unsigned char)line[5])) outcar = true;
        if (i >= 1 && i <= 4) {
            double values[6];
            if (i == 1) {
                char *words[4];
                size_t n = split_words(line, words, 4);
                if (n != 1 && n != 3) poscar = false;
                else for (size_t j = 0; j < n; ++j) if (!number(words[j], &values[j])) poscar = false;
            } else if (!numeric_line(line, values, 3, 3)) poscar = false;
        }
        if (i == 4 && poscar) break;
        if (i >= 4 && outcar) break;
        if (i >= 4 && xml) break;
    }
    if (poscar) *format = FORMAT_POSCAR;
    else if (outcar) *format = FORMAT_OUTCAR;
    else if (xml) *format = FORMAT_XML;
    else return fail(e, 3, "%s: unrecognized format; use --format poscar|contcar|outcar|xml for headerless inputs", r->path);
    if (fseek(r->file, 0, SEEK_SET)) return fail(e, 7, "%s: input must be seekable for auto detection; use --format", r->path);
    r->line = 0; r->bytes = 0;
    return true;
}

bool parse_structure(const Config *c, Structure *s, Error *e)
{
    Reader r = {0}; r.path = c->input;
    r.file = fopen(r.path, "rb");
    if (!r.file) return fail(e, 7, "%s: cannot open input", r.path);
    Format format = c->format;
    bool ok = format != FORMAT_AUTO || detect_format(&r, &format, e);
    if (ok && format == FORMAT_XML) {
#ifdef VR_HAVE_XML
        ok = parse_xml(r.file, c, s, e);
#else
        ok = fail(e, 3, "%s: XML input requires a build with ENABLE_XML=ON", r.path);
#endif
    } else if (ok) ok = format == FORMAT_POSCAR ? parse_poscar(&r, c, s, e) : parse_outcar(&r, c, s, e);
    if (fclose(r.file) && ok) ok = fail(e, 7, "%s: failed to close input", r.path);
    if (ok) {
        size_t count = s->count;
        for (size_t i = 0; i < 3; ++i) {
            if (count > VR_MAX_INSTANCES / c->repeat[i]) { ok = fail(e, 6, "repeated structure exceeds %u instances", VR_MAX_INSTANCES); break; }
            count *= c->repeat[i];
        }
    }
    if (!ok) structure_free(s);
    return ok;
}
