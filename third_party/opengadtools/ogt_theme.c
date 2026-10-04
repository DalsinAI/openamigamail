/* ogt_theme: reads OpenGadTools theme files (THEME_SPEC.md, version 1).
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include "ogt_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int ogt_parse_colour(const char *s, ogt_rgb *out)
{
    int i, v[6];
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '#') return 0;
    for (i = 0; i < 6; i++)
        if ((v[i] = hexval((unsigned char)s[1 + i])) < 0) return 0;
    if (s[7] && s[7] != ' ' && s[7] != '\t' && s[7] != ',') return 0;
    out->r = (unsigned char)(v[0] * 16 + v[1]);
    out->g = (unsigned char)(v[2] * 16 + v[3]);
    out->b = (unsigned char)(v[4] * 16 + v[5]);
    return 1;
}

int ogt_parse_grad(const char *s, ogt_grad *g)
{
    const char *p = s;
    memset(g, 0, sizeof *g);
    while (*p == ' ' || *p == '\t') p++;
    if (!strncmp(p, "vertical", 8)) { g->kind = OGT_VERTICAL; p += 8; }
    else if (!strncmp(p, "horizontal", 10)) { g->kind = OGT_HORIZONTAL; p += 10; }
    else if (!strncmp(p, "radial", 6)) { g->kind = OGT_RADIAL; p += 6; }
    while (*p == ' ' || *p == '\t') p++;
    for (;;) {
        ogt_rgb c;
        char *end;
        int pos;
        if (!ogt_parse_colour(p, &c)) return 0;
        p += 7;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p && g->n == 0) {                         /* a single colour: a flat fill */
            g->n = 2;
            g->pos[0] = 0; g->c[0] = c;
            g->pos[1] = 1000; g->c[1] = c;
            return 1;
        }
        pos = 0;
        end = (char *)p;
        while (*end >= '0' && *end <= '9') pos = pos * 10 + (*end++ - '0');
        pos *= 10;
        if (*end == '.') {                                  /* one decimal is kept */
            end++;
            if (*end >= '0' && *end <= '9') pos += *end - '0';
            while (*end >= '0' && *end <= '9') end++;
        }
        if (end == p || pos > 1000) return 0;
        if (g->n && pos < g->pos[g->n - 1]) return 0;
        if (g->n == OGT_MAX_STOPS) return 0;
        g->pos[g->n] = pos;
        g->c[g->n] = c;
        g->n++;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p != ',') return 0;
        p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    return g->n >= 2 && g->pos[0] == 0 && g->pos[g->n - 1] == 1000;
}

/* One channel between two stops, rounded, in integers. */
static unsigned char blend(int a, int b, int d, int span)
{
    long v = (long)a * span + (long)(b - a) * d;
    return (unsigned char)((v + span / 2) / span);
}

ogt_rgb ogt_grad_at10(const ogt_grad *g, int pos)
{
    int i;
    for (i = 0; i < g->n - 1; i++) {
        if (pos <= g->pos[i + 1]) {
            int span = g->pos[i + 1] - g->pos[i], d = pos - g->pos[i];
            ogt_rgb a = g->c[i], b = g->c[i + 1], o;
            if (span <= 0 || d <= 0) return a;
            o.r = blend(a.r, b.r, d, span);
            o.g = blend(a.g, b.g, d, span);
            o.b = blend(a.b, b.b, d, span);
            return o;
        }
    }
    return g->c[g->n - 1];
}

ogt_rgb ogt_grad_at(const ogt_grad *g, int percent)
{
    return ogt_grad_at10(g, percent * 10);
}

static char *trim(char *s)
{
    char *e;
    while (*s == ' ' || *s == '\t') s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) *--e = 0;
    return s;
}

static int add(ogt_section *sec, const char *key, const char *value, char *err, size_t errlen, int line)
{
    int i;
    for (i = 0; i < sec->count; i++)
        if (!strcmp(sec->key[i], key)) {
            snprintf(err, errlen, "line %d: %s is set twice", line, key);
            return 0;
        }
    if (sec->count == OGT_MAX_KEYS) {
        snprintf(err, errlen, "line %d: too many settings", line);
        return 0;
    }
    sec->key[sec->count] = key;
    sec->value[sec->count] = value;
    sec->count++;
    return 1;
}

static const char *section_get(const ogt_section *sec, const char *key)
{
    int i;
    for (i = 0; i < sec->count; i++)
        if (!strcmp(sec->key[i], key)) return sec->value[i];
    return NULL;
}

static void unquote(const char *v, char *out, size_t size)
{
    size_t n;
    if (*v == '"') {
        const char *e = strchr(v + 1, '"');
        n = e ? (size_t)(e - v - 1) : strlen(v + 1);
        v++;
    } else n = strlen(v);
    if (n >= size) n = size - 1;
    memcpy(out, v, n);
    out[n] = 0;
}

int ogt_theme_parse(ogt_theme *t, const char *text, char *err, size_t errlen)
{
    char *p, *line;
    ogt_section *sec;
    int n = 0;
    const char *v;
    memset(t, 0, sizeof *t);
    if (!(t->text = malloc(strlen(text) + 1))) {
        snprintf(err, errlen, "out of memory");
        return 0;
    }
    strcpy(t->text, text);
    sec = &t->header;
    for (p = t->text; p && *p; ) {
        char *nl = strchr(p, '\n'), *semi, *key, *val;
        line = p;
        p = nl ? nl + 1 : NULL;
        if (nl) *nl = 0;
        n++;
        if ((semi = strchr(line, ';'))) *semi = 0;
        line = trim(line);
        if (!*line) continue;
        if (*line == '[') {
            if (!strcmp(line, "[light]")) { sec = &t->light; t->has_light = 1; }
            else if (!strcmp(line, "[dark]")) { sec = &t->dark; t->has_dark = 1; }
            else if (!strcmp(line, "[four]")) sec = &t->four;
            else {
                snprintf(err, errlen, "line %d: unknown section %s", n, line);
                ogt_theme_free(t);
                return 0;
            }
            continue;
        }
        key = line;
        val = key + strcspn(key, " \t");
        if (*val) *val++ = 0;
        val = trim(val);
        if (!strncmp(key, "x-", 2)) continue;                /* extensions (section 8) */
        if (!add(sec, key, val, err, errlen, n)) {
            ogt_theme_free(t);
            return 0;
        }
    }
    if ((v = section_get(&t->header, "name"))) unquote(v, t->name, sizeof t->name);
    if ((v = section_get(&t->header, "description"))) unquote(v, t->description, sizeof t->description);
    t->version = (v = section_get(&t->header, "version")) ? atoi(v) : 0;
    t->align = OGT_ALIGN_LEFT;
    if ((v = section_get(&t->header, "title.align"))) {
        if (!strcmp(v, "centre")) t->align = OGT_ALIGN_CENTRE;
        else if (!strcmp(v, "right")) t->align = OGT_ALIGN_RIGHT;
    }
    t->passthrough = (v = section_get(&t->header, "passthrough")) && !strcmp(v, "yes");
    if ((v = section_get(&t->header, "font")) && strcmp(v, "system")) {
        const char *q = v;
        unquote(v, t->font, sizeof t->font);
        if (*q == '"' && (q = strchr(q + 1, '"'))) t->font_size = atoi(q + 1);
    }
    if (!t->name[0] || t->version < 1) {
        snprintf(err, errlen, "not a theme: it needs a name and a version");
        ogt_theme_free(t);
        return 0;
    }
    if (!t->passthrough && !t->has_light) {
        snprintf(err, errlen, "a theme that draws needs a [light] section");
        ogt_theme_free(t);
        return 0;
    }
    return 1;
}

void ogt_theme_free(ogt_theme *t)
{
    free(t->text);
    memset(t, 0, sizeof *t);
}

const char *ogt_theme_get(const ogt_theme *t, int mode, const char *key)
{
    const char *v = NULL;
    if (mode == OGT_DARK && t->has_dark) v = section_get(&t->dark, key);
    if (!v && t->has_light) v = section_get(&t->light, key);
    return v;
}

int ogt_theme_grad(const ogt_theme *t, int mode, const char *key, ogt_grad *g)
{
    const char *v = ogt_theme_get(t, mode, key);
    return v && strcmp(v, "none") && ogt_parse_grad(v, g);
}

int ogt_theme_colour(const ogt_theme *t, int mode, const char *key, ogt_rgb *out)
{
    ogt_grad g;
    if (!ogt_theme_grad(t, mode, key, &g)) return 0;
    *out = ogt_grad_at(&g, 50);
    return 1;
}

int ogt_theme_number(const ogt_theme *t, int mode, const char *key, int def)
{
    const char *v = ogt_theme_get(t, mode, key);
    char *end;
    long n;
    if (!v) return def;
    n = strtol(v, &end, 10);
    return end == v ? def : (int)n;
}

int ogt_theme_four(const ogt_theme *t, ogt_rgb pens[4], int *title_fill, int *title_text, int *fill_text)
{
    const char *v = section_get(&t->four, "pens");
    int i;
    if (!v) return 0;
    for (i = 0; i < 4; i++) {
        while (*v == ' ' || *v == '\t') v++;
        if (!ogt_parse_colour(v, &pens[i])) return 0;
        v += 7;
    }
    *title_fill = (v = section_get(&t->four, "title.active.fill")) ? atoi(v) & 3 : 3;
    *title_text = (v = section_get(&t->four, "title.active.text")) ? atoi(v) & 3 : 1;
    *fill_text = (v = section_get(&t->four, "fill.text")) ? atoi(v) & 3 : 1;
    return 1;
}

/* Which pen each key takes on a four-colour screen (section 6): 0 the
 * background, 1 text and shadow edges, 2 shine edges, 3 the fill. */
int ogt_four_role(const char *key, int title_fill, int title_text, int fill_text)
{
    static const struct { const char *key; int role; } roles[] = {
        { "desktop", 0 }, { "window", 0 }, { "panel", 0 }, { "list", 0 }, { "list.alternate", 0 },
        { "list.header", 0 }, { "tab", 0 }, { "track", 0 }, { "string", 0 }, { "button", 0 },
        { "title.inactive", 0 }, { "gadget.active", -1 }, { "gadget.inactive", -1 },
        { "screenbar", 2 }, { "menu", 2 }, { "string.active", 2 }, { "frame.highlight", 2 },
        { "group.highlight", 2 }, { "string.shine", 2 }, { "button.highlight", 2 },
        { "text", 1 }, { "label", 1 }, { "muted", 1 }, { "screenbar.text", 1 }, { "screenbar.line", 1 },
        { "title.inactive.text", 1 }, { "menu.text", 1 }, { "menu.line", 1 }, { "icon.text", 1 },
        { "button.text", 1 }, { "button.border", 1 }, { "string.shadow", 1 }, { "group.line", 1 },
        { "frame.active", 1 }, { "frame.inactive", 1 }, { "tab.text", 1 }, { "gadget.inactive.glyph", 1 },
        { "gadget.inactive.border", 1 }, { "selection.inactive", 2 },
        { "accent", 3 }, { "fill", 3 }, { "accent.mark", 1 },
    };
    unsigned i;
    if (!strcmp(key, "title.active")) return title_fill;
    if (!strcmp(key, "title.active.text") || !strcmp(key, "gadget.active.glyph") || !strcmp(key, "gadget.active.border"))
        return title_text;
    if (!strcmp(key, "accent.text")) return fill_text;
    if (!strcmp(key, "fill.text")) return fill_text;
    for (i = 0; i < sizeof roles / sizeof roles[0]; i++)
        if (!strcmp(roles[i].key, key)) return roles[i].role;
    return 1;
}
