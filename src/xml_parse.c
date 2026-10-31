/* SPDX-License-Identifier: GPL-3.0-only */
#include "input.h"
#include <libxml/xmlreader.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define XML_DEPTH_LIMIT 64
#define XML_NODE_LIMIT 200000000ULL

typedef enum {
    OTHER, MODELING, CALCULATION, ATOMINFO, ATOM_COUNT, TYPE_COUNT,
    ATOM_ARRAY, TYPE_ARRAY, ATOM_SET, TYPE_SET, ATOM_ROW, TYPE_ROW,
    ATOM_CELL, TYPE_CELL, SNAPSHOT, CRYSTAL, BASIS, POSITIONS, BASIS_ROW, POSITION_ROW
} Tag;
typedef enum { INITIAL, IONIC, FINAL } SnapshotKind;
typedef struct {
    FILE *file;
    const InputOptions *config;
    Error *error;
    xmlTextReaderPtr reader;
    unsigned long long bytes, nodes;
    size_t line, line_bytes;
    Tag stack[XML_DEPTH_LIMIT + 1];
    char text[VR_MAX_LINE + 1];
    size_t text_size;
    int capture_depth;
    Structure candidate;
    size_t types, atom_rows, type_rows, cells, row_type, row_population;
    unsigned row_element;
    size_t populations[VR_MAX_SPECIES], declared_populations[VR_MAX_SPECIES];
    unsigned elements[VR_MAX_SPECIES], declared_elements[VR_MAX_SPECIES];
    size_t frames, basis_rows, position_rows;
    SnapshotKind kind;
    bool metadata_started, metadata_done, atoms_array, types_array;
    bool initial_seen, final_seen, basis_seen, positions_seen, root_seen, root_closed;
} Xml;

static bool problem(Xml *x, int code, const char *message)
{
    int line = x->reader ? xmlTextReaderGetParserLineNumber(x->reader) : 0;
    return fail(x->error, code, "%s:%zu: XML: %s", x->config->input,
                line > 0 ? (size_t)line : x->line, message);
}

/* Use an application-owned stream: libxml2 never opens the input as a URI,
 * decompresses it implicitly, or bypasses our byte/physical-line limits. */
static int xml_input_read(void *context, char *buffer, int length)
{
    Xml *x = context;
    if (x->error->code) return -1;
    size_t n = fread(buffer, 1, (size_t)length, x->file);
    if (ferror(x->file)) { problem(x, 7, "input read error"); return -1; }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)buffer[i];
        if (++x->bytes > VR_MAX_INPUT_BYTES || !c) {
            problem(x, 4, "input byte limit exceeded or embedded NUL (use UTF-8 XML)"); return -1;
        }
        if (c == '\n') { ++x->line; x->line_bytes = 0; }
        else if (++x->line_bytes > VR_MAX_LINE) {
            problem(x, 4, "input line limit exceeded"); return -1;
        }
    }
    return (int)n;
}

static void reader_error(void *context, const char *message,
                         xmlParserSeverities severity, xmlTextReaderLocatorPtr locator)
{
    Xml *x = context;
    (void)message; (void)locator;
    if (severity != XML_PARSER_SEVERITY_WARNING && severity != XML_PARSER_SEVERITY_VALIDITY_WARNING && !x->error->code)
        problem(x, 4, "malformed or unfinished document; a complete </modeling> is required");
}

static bool named(xmlTextReaderPtr r, const char *value)
{
    xmlChar *name = xmlTextReaderGetAttribute(r, BAD_CAST "name");
    bool result = name && !xmlStrcmp(name, BAD_CAST value);
    xmlFree(name);
    return result;
}

static bool is_tag(const xmlChar *name, const char *value)
{
    return name && !xmlStrcmp(name, BAD_CAST value);
}

static bool captures(Tag tag)
{
    return tag == ATOM_COUNT || tag == TYPE_COUNT || tag == ATOM_CELL ||
           tag == TYPE_CELL || tag == BASIS_ROW || tag == POSITION_ROW;
}

static char *trimmed(char *s)
{
    while (isspace((unsigned char)*s)) ++s;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n-1])) s[--n] = '\0';
    return s;
}

static bool vector(char *s, double v[3])
{
    for (size_t i = 0; i < 3; ++i) {
        while (isspace((unsigned char)*s)) ++s;
        char *end = s;
        while (*end && !isspace((unsigned char)*end)) ++end;
        char tail = *end; *end = '\0';
        if (!number(s, &v[i])) return false;
        s = end + (tail ? 1 : 0);
    }
    while (isspace((unsigned char)*s)) ++s;
    return !*s;
}

static bool save_snapshot(Xml *x, Structure *s, size_t frame)
{
    if (!s->atoms) {
        s->atoms = malloc(x->candidate.count * sizeof *s->atoms);
        if (!s->atoms) return problem(x, 6, "cannot allocate selected geometry");
    }
    memcpy(s->atoms, x->candidate.atoms, x->candidate.count * sizeof *s->atoms);
    memcpy(s->lattice, x->candidate.lattice, sizeof s->lattice);
    s->count = x->candidate.count; s->source.frame = frame;
    return true;
}

static bool start(Xml *x, int depth)
{
    const xmlChar *name = xmlTextReaderConstName(x->reader);
    Tag parent = depth ? x->stack[depth-1] : OTHER, tag = OTHER;
    if (x->capture_depth >= 0) return problem(x, 4, "nested markup in a geometry/metadata scalar");
    if (!depth) {
        if (x->root_seen || !is_tag(name, "modeling")) return problem(x, 4, "expected a single <modeling> root");
        x->root_seen = true; tag = MODELING;
    } else if (parent == MODELING && is_tag(name, "atominfo")) {
        if (x->metadata_started) return problem(x, 4, "duplicate atominfo");
        x->metadata_started = true; tag = ATOMINFO;
    } else if (parent == ATOMINFO && is_tag(name, "atoms")) tag = ATOM_COUNT;
    else if (parent == ATOMINFO && is_tag(name, "types")) tag = TYPE_COUNT;
    else if (parent == ATOMINFO && is_tag(name, "array")) {
        if (named(x->reader, "atoms")) {
            if (x->atoms_array || !x->candidate.atoms || !x->types) return problem(x, 4, "duplicate/out-of-order atoms array");
            x->atoms_array = true; tag = ATOM_ARRAY;
        } else if (named(x->reader, "atomtypes")) {
            if (x->types_array || !x->types) return problem(x, 4, "duplicate/out-of-order atomtypes array");
            x->types_array = true; tag = TYPE_ARRAY;
        }
    } else if (parent == ATOM_ARRAY && is_tag(name, "set")) tag = ATOM_SET;
    else if (parent == TYPE_ARRAY && is_tag(name, "set")) tag = TYPE_SET;
    else if ((parent == ATOM_SET || parent == TYPE_SET) && is_tag(name, "rc")) {
        x->cells = 0; x->row_element = 0; x->row_type = x->row_population = 0;
        tag = parent == ATOM_SET ? ATOM_ROW : TYPE_ROW;
    } else if ((parent == ATOM_ROW || parent == TYPE_ROW) && is_tag(name, "c")) {
        tag = parent == ATOM_ROW ? ATOM_CELL : TYPE_CELL;
    } else if (parent == MODELING && is_tag(name, "calculation")) {
        if (x->final_seen) return problem(x, 4, "calculation after finalpos");
        tag = CALCULATION;
    } else if ((parent == MODELING || parent == CALCULATION) && is_tag(name, "structure")) {
        /* Primitive-cell structures use a different atom mapping and are not
         * trajectory frames. Never feed them to the simulation-cell parser. */
        if (parent == MODELING && named(x->reader, "primitive_cell")) { x->stack[depth] = OTHER; return true; }
        if (!x->metadata_done) return problem(x, 4, "structure precedes complete atominfo");
        if (x->final_seen) return problem(x, 4, "structure after finalpos or duplicate finalpos");
        xmlChar *label = xmlTextReaderGetAttribute(x->reader, BAD_CAST "name");
        x->kind = INITIAL;
        bool valid = true;
        if (!label || !*label) x->kind = IONIC;
        else if (!xmlStrcmp(label, BAD_CAST "initialpos") && parent == MODELING) {
            if (x->initial_seen || x->frames) valid = false;
            x->initial_seen = true;
        } else if (!xmlStrcmp(label, BAD_CAST "finalpos") && parent == MODELING) x->kind = FINAL;
        else valid = false;
        xmlFree(label);
        if (!valid) return problem(x, 4, "unsupported, misplaced or duplicate structure name");
        x->basis_seen = x->positions_seen = false; x->basis_rows = x->position_rows = 0;
        memset(x->candidate.lattice, 0, sizeof x->candidate.lattice);
        tag = SNAPSHOT;
    } else if (parent == SNAPSHOT && is_tag(name, "crystal")) tag = CRYSTAL;
    else if (parent == CRYSTAL && is_tag(name, "varray") && named(x->reader, "basis")) {
        if (x->basis_seen) return problem(x, 4, "duplicate basis");
        x->basis_seen = true; tag = BASIS;
    } else if (parent == SNAPSHOT && is_tag(name, "varray") && named(x->reader, "positions")) {
        if (x->positions_seen) return problem(x, 4, "duplicate positions");
        x->positions_seen = true; tag = POSITIONS;
    } else if (parent == BASIS && is_tag(name, "v")) tag = BASIS_ROW;
    else if (parent == POSITIONS && is_tag(name, "v")) tag = POSITION_ROW;
    else if (parent == BASIS || parent == POSITIONS || parent == ATOM_SET || parent == TYPE_SET || parent == ATOM_ROW || parent == TYPE_ROW)
        return problem(x, 4, "unexpected geometry/metadata table child");
    x->stack[depth] = tag;
    if (captures(tag)) { x->capture_depth = depth; x->text_size = 0; x->text[0] = '\0'; }
    return true;
}

static bool finish(Xml *x, int depth, Structure *s)
{
    Tag tag = x->stack[depth];
    char *text = trimmed(x->text);
    if (captures(tag)) x->capture_depth = -1;
    if (tag == ATOM_COUNT) {
        size_t n;
        if (x->candidate.atoms || !integer(text, VR_MAX_ATOMS, &n) || !n) return problem(x, 4, "invalid/duplicate atom count");
        x->candidate.atoms = calloc(n, sizeof *x->candidate.atoms);
        if (!x->candidate.atoms) return problem(x, 6, "cannot allocate XML atoms");
        x->candidate.count = n;
    } else if (tag == TYPE_COUNT) {
        if (x->types || !integer(text, VR_MAX_SPECIES, &x->types) || !x->types) return problem(x, 4, "invalid/duplicate type count");
    } else if (tag == ATOM_CELL || tag == TYPE_CELL) {
        if (++x->cells > 16) return problem(x, 4, "too many metadata columns");
        if ((tag == ATOM_CELL && x->cells == 1) || (tag == TYPE_CELL && x->cells == 2)) {
            x->row_element = element_id(text);
            if (!x->row_element) return problem(x, 5, "unknown XML element symbol");
        } else if (tag == ATOM_CELL && x->cells == 2) {
            if (!integer(text, x->types, &x->row_type) || !x->row_type) return problem(x, 4, "invalid per-ion type index");
        } else if (tag == TYPE_CELL && x->cells == 1) {
            if (!integer(text, VR_MAX_ATOMS, &x->row_population)) return problem(x, 4, "invalid type population");
        }
    } else if (tag == ATOM_ROW) {
        if (x->cells != 2 || x->atom_rows >= x->candidate.count) return problem(x, 4, "atoms table row/column count mismatch");
        size_t t = x->row_type - 1;
        if (x->elements[t] && x->elements[t] != x->row_element) return problem(x, 5, "conflicting per-type element identities");
        x->elements[t] = x->row_element; ++x->populations[t];
        x->candidate.atoms[x->atom_rows++].element = x->row_element;
    } else if (tag == TYPE_ROW) {
        if (x->cells < 2 || x->type_rows >= x->types) return problem(x, 4, "atomtypes table row/column count mismatch");
        x->declared_elements[x->type_rows] = x->row_element;
        x->declared_populations[x->type_rows++] = x->row_population;
    } else if (tag == ATOMINFO) {
        if (!x->types || !x->atoms_array || x->atom_rows != x->candidate.count || !x->candidate.count ||
            (x->types_array && x->type_rows != x->types)) return problem(x, 4, "incomplete atominfo");
        if (x->config->species_count && x->config->species_count != x->types) return problem(x, 5, "explicit species count conflicts with XML types");
        for (size_t i = 0; i < x->types; ++i) {
            if (x->types_array && (x->populations[i] != x->declared_populations[i] ||
                (x->elements[i] && x->elements[i] != x->declared_elements[i]))) return problem(x, 5, "atoms/atomtypes metadata disagree");
            unsigned z = x->elements[i] ? x->elements[i] : x->declared_elements[i];
            if (!z || (x->config->species_count && x->config->species[i] != z)) return problem(x, 5, "missing/conflicting XML species identity");
        }
        x->metadata_done = true;
    } else if (tag == BASIS_ROW || tag == POSITION_ROW) {
        double v[3];
        if (!vector(text, v)) return problem(x, 4, "expected three finite geometry components");
        if (tag == BASIS_ROW) {
            if (x->basis_rows >= 3) return problem(x, 4, "basis must have exactly three vectors");
            memcpy(x->candidate.lattice[x->basis_rows++].x, v, sizeof v);
        } else {
            if (x->position_rows >= x->candidate.count) return problem(x, 4, "too many position rows");
            for (size_t i = 0; i < 3; ++i) if (fabs(v[i]) > 1e12) return problem(x, 5, "fractional coordinate outside supported range");
            memcpy(x->candidate.atoms[x->position_rows++].position.x, v, sizeof v);
        }
    } else if (tag == SNAPSHOT) {
        if (!x->basis_seen || !x->positions_seen || x->basis_rows != 3 || x->position_rows != x->candidate.count)
            return problem(x, 4, "incomplete structure basis/positions");
        if (!lattice_valid(x->candidate.lattice)) return problem(x, 5, "degenerate or unsupported lattice");
        for (size_t i = 0; i < x->candidate.count; ++i) {
            Vec3 p = x->candidate.atoms[i].position;
            for (size_t j = 0; j < 3; ++j) {
                double v = p.x[0]*x->candidate.lattice[0].x[j] + p.x[1]*x->candidate.lattice[1].x[j] + p.x[2]*x->candidate.lattice[2].x[j];
                if (!isfinite(v) || fabs(v) > 1e12) return problem(x, 5, "Cartesian coordinate outside supported range");
                x->candidate.atoms[i].position.x[j] = v;
            }
        }
        if (x->kind == IONIC) {
            if (x->frames == VR_MAX_FRAMES) return problem(x, 6, "ionic frame limit exceeded");
            if (x->config->visit) {
                for (int d=0;d<3;++d) x->candidate.cell_present[d]=x->candidate.pbc[d]=true;
                if (!structure_validate(&x->candidate,x->error) ||
                    !x->config->visit(&x->candidate,x->frames,x->config->visit_context,x->error)) return false;
            }
            if ((x->config->final_frame || x->frames == x->config->frame) && !save_snapshot(x, s, x->frames)) return false;
            ++x->frames;
        } else if (x->kind == FINAL) {
            x->final_seen = true;
            if ((x->config->final_frame || (!x->frames && !x->config->frame)) && !save_snapshot(x, s, x->frames ? x->frames-1 : 0)) return false;
        }
    } else if (tag == MODELING) x->root_closed = true;
    return true;
}

bool parse_xml(FILE *file, const InputOptions *c, Structure *s, Error *e)
{
    if (c->recover) return fail(e, 2, "--recover is only meaningful for OUTCAR; XML requires a complete document");
    Xml *x = calloc(1, sizeof *x);
    if (!x) return fail(e, 6, "%s: cannot allocate XML parser state", c->input);
    x->file = file; x->config = c; x->error = e; x->line = 1; x->capture_depth = -1;
    /* No NOENT, DTDLOAD, DTDATTR, XINCLUDE, HUGE or RECOVER. In particular,
     * NONET alone is not the protection against local-file entity loading. */
    x->reader = xmlReaderForIO(xml_input_read, NULL, x, NULL, NULL, XML_PARSE_NONET | XML_PARSE_COMPACT);
    bool ok = false;
    if (!x->reader) { if (!e->code) problem(x, 6, "cannot create streaming reader"); goto done; }
    xmlTextReaderSetErrorHandler(x->reader, reader_error, x);
    xmlTextReaderSetParserProp(x->reader, XML_PARSER_LOADDTD, 0);
    xmlTextReaderSetParserProp(x->reader, XML_PARSER_SUBST_ENTITIES, 0);
    xmlTextReaderSetParserProp(x->reader, XML_PARSER_VALIDATE, 0);
    int rc;
    while ((rc = xmlTextReaderRead(x->reader)) == 1) {
        if (!check_cancel(e)) goto done;
        if (e->code) goto done;
        if (++x->nodes > XML_NODE_LIMIT) { problem(x, 6, "XML node/work limit exceeded"); goto done; }
        int depth = xmlTextReaderDepth(x->reader), type = xmlTextReaderNodeType(x->reader);
        if (depth < 0 || depth > XML_DEPTH_LIMIT) { problem(x, 4, "XML nesting depth limit exceeded"); goto done; }
        if (type == XML_READER_TYPE_DOCUMENT_TYPE || type == XML_READER_TYPE_ENTITY_REFERENCE || type == XML_READER_TYPE_ENTITY) {
            problem(x, 4, "DTDs and entity references are unsupported"); goto done;
        }
        const xmlChar *uri = xmlTextReaderConstNamespaceUri(x->reader);
        if (uri && *uri) { problem(x, 4, "namespaced XML is unsupported"); goto done; }
        const xmlChar *value = xmlTextReaderConstValue(x->reader);
        if (value && xmlStrlen(value) > (int)VR_MAX_LINE) { problem(x, 4, "XML node text limit exceeded"); goto done; }
        if (type == XML_READER_TYPE_ELEMENT) {
            if (xmlTextReaderAttributeCount(x->reader) > 32) { problem(x, 4, "XML attribute count limit exceeded"); goto done; }
            if (xmlStrlen(xmlTextReaderConstName(x->reader)) > 256) { problem(x, 4, "XML name limit exceeded"); goto done; }
            if (xmlTextReaderMoveToFirstAttribute(x->reader) == 1) {
                do {
                    const xmlChar *attribute = xmlTextReaderConstValue(x->reader);
                    if (xmlStrlen(xmlTextReaderConstName(x->reader)) > 256 ||
                        (attribute && xmlStrlen(attribute) > (int)VR_MAX_LINE)) {
                        problem(x, 4, "XML attribute name/value limit exceeded"); goto done;
                    }
                } while (xmlTextReaderMoveToNextAttribute(x->reader) == 1);
                xmlTextReaderMoveToElement(x->reader);
            }
            if (!start(x, depth)) goto done;
            if (xmlTextReaderIsEmptyElement(x->reader) && !finish(x, depth, s)) goto done;
        } else if (type == XML_READER_TYPE_END_ELEMENT) {
            if (!finish(x, depth, s)) goto done;
        } else if (x->capture_depth >= 0 && value && (type == XML_READER_TYPE_TEXT || type == XML_READER_TYPE_CDATA ||
                   type == XML_READER_TYPE_WHITESPACE || type == XML_READER_TYPE_SIGNIFICANT_WHITESPACE)) {
            size_t n = (size_t)xmlStrlen(value);
            if (n > VR_MAX_LINE - x->text_size) { problem(x, 4, "geometry/metadata scalar limit exceeded"); goto done; }
            memcpy(x->text+x->text_size, value, n); x->text_size += n; x->text[x->text_size] = '\0';
        } else if (value && type == XML_READER_TYPE_TEXT) {
            Tag parent = depth ? x->stack[depth-1] : OTHER;
            if (parent == BASIS || parent == POSITIONS || parent == ATOM_SET || parent == TYPE_SET || parent == ATOM_ROW || parent == TYPE_ROW) {
                problem(x, 4, "unexpected text in geometry/metadata table"); goto done;
            }
        }
    }
    if (rc < 0 || e->code || !x->root_closed) { if (!e->code) problem(x, 4, "malformed or unfinished document"); goto done; }
    if (!x->metadata_done || !s->atoms) { problem(x, 4, "requested ionic frame not found or no complete geometry"); goto done; }
    s->source.format = FORMAT_XML; s->source.frame_count = x->frames ? x->frames : 1;
    ok = true;
done:
    if (x->reader) xmlFreeTextReader(x->reader);
    structure_free(&x->candidate); free(x);
    return ok;
}
