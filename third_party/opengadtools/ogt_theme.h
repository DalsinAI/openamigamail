/* ogt_theme: reads OpenGadTools theme files (THEME_SPEC.md, version 1).
 *
 * Plain C with no Amiga calls, so the same code is tested on the host
 * (tests/host/test_theme.c) and runs on the Amiga. A theme is read once;
 * its values are looked up by key and mode, and turned into colours,
 * gradients and numbers on demand.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_THEME_H
#define OGT_THEME_H

#include <stddef.h>

enum { OGT_LIGHT = 0, OGT_DARK = 1 };
enum { OGT_ALIGN_LEFT = 0, OGT_ALIGN_CENTRE = 1, OGT_ALIGN_RIGHT = 2 };
enum { OGT_VERTICAL = 0, OGT_HORIZONTAL = 1, OGT_RADIAL = 2 };

#define OGT_MAX_KEYS 96
#define OGT_MAX_STOPS 8

typedef struct ogt_rgb { unsigned char r, g, b; } ogt_rgb;

typedef struct ogt_grad {
    int kind;                       /* OGT_VERTICAL, OGT_HORIZONTAL, OGT_RADIAL */
    int n;                          /* stops, 2 to OGT_MAX_STOPS */
    int pos[OGT_MAX_STOPS];         /* 0 to 1000: tenths of a percent (integers only: no FPU needed) */
    ogt_rgb c[OGT_MAX_STOPS];
} ogt_grad;

typedef struct ogt_section {
    int count;
    const char *key[OGT_MAX_KEYS];
    const char *value[OGT_MAX_KEYS];
} ogt_section;

typedef struct ogt_theme {
    char name[48];
    char description[128];
    int version;
    char font[64];                  /* "" for the screen's own font */
    int font_size;
    int align;                      /* OGT_ALIGN_* */
    int passthrough;                /* Classic: draw what the OS draws */
    int has_light, has_dark;
    ogt_section header, light, dark, four;
    char *text;                     /* the file's text, owned; keys and values point into it */
} ogt_theme;

/* Reads a theme from its text. 1, or 0 with the reason in err. */
int ogt_theme_parse(ogt_theme *t, const char *text, char *err, size_t errlen);
void ogt_theme_free(ogt_theme *t);

/* The raw value of a key in a mode; dark falls back to light. NULL when unset. */
const char *ogt_theme_get(const ogt_theme *t, int mode, const char *key);

/* A key's value as a gradient (a single colour is a flat two-stop one). */
int ogt_theme_grad(const ogt_theme *t, int mode, const char *key, ogt_grad *g);
/* A key's value as one colour; for a gradient, its middle. */
int ogt_theme_colour(const ogt_theme *t, int mode, const char *key, ogt_rgb *out);
/* A key's whole-number value (radii); def when unset or bad. */
int ogt_theme_number(const ogt_theme *t, int mode, const char *key, int def);

/* The [four] section: the four pens' colours, and the pen numbers of the
 * active title's fill and text and of text on the fill. */
int ogt_theme_four(const ogt_theme *t, ogt_rgb pens[4], int *title_fill, int *title_text, int *fill_text);

/* For drawing on a four-colour screen (and the Classic theme): which of
 * the four pens a mode key stands for. */
int ogt_four_role(const char *key, int title_fill, int title_text, int fill_text);

int ogt_parse_colour(const char *s, ogt_rgb *out);
int ogt_parse_grad(const char *s, ogt_grad *g);
ogt_rgb ogt_grad_at(const ogt_grad *g, int percent);   /* 0 to 100 */
ogt_rgb ogt_grad_at10(const ogt_grad *g, int tenths); /* 0 to 1000 */

#endif
