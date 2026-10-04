/* ogt_draw: drawing a theme on an Amiga screen (DESIGN.md 2d, THEME_SPEC.md 6).
 *
 * A context obtains the theme's colours as pens on one screen, the OS way
 * (ObtainBestPen, shared, released when done):
 *   - RTG screens (more than 8 bits) get smooth gradients, OGT_RTG_BANDS
 *     bands each;
 *   - palette screens (AGA) get OGT_AGA_BANDS bands, as the spec says;
 *   - the Classic theme, and palette screens of fewer than 64 colours, draw with four
 *     pens: Classic with the screen's own (DrawInfo) pens, the others with
 *     the theme's [four] colours.
 *
 *   - on true-colour RTG screens with cybergraphics.library (Picasso96
 *     has it), fills are drawn in exact colours (FillPixelArray) with no
 *     pens at all; only text and lines use pens, matched as closely as the
 *     screen's 256-entry table allows.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_DRAW_H
#define OGT_DRAW_H

#include <exec/types.h>
#include "ogt_theme.h"

struct Screen;
struct RastPort;
struct ColorMap;

#define OGT_RTG_BANDS 12
#define OGT_AGA_BANDS 4
#define OGT_MAX_PENS 160
/* A colour drawn directly (RTG), not a pen: the flag plus 0xRRGGBB. */
#define OGT_DIRECT 0x40000000L

typedef struct ogt_ctx {
    struct Screen *scr;
    struct ColorMap *cm;
    const ogt_theme *theme;
    int mode;               /* OGT_LIGHT or OGT_DARK */
    int bands;              /* per gradient */
    int four;               /* draw with four pens */
    int direct;             /* fills in exact colours (RTG, cybergraphics) */
    int tn;                 /* pens for text and lines on a direct screen */
    ULONG trgb[32];
    LONG tpen[32];
    LONG fourpen[4];
    int fourown[4];         /* 1 when we obtained it and must release it */
    int tf, tt, ft;         /* [four]: title fill, title text, text on fill */
    int n;
    ULONG rgb[OGT_MAX_PENS];
    LONG pen[OGT_MAX_PENS];
} ogt_ctx;

/* Sets up drawing a theme on a screen. 1, or 0 when the theme can't draw there. */
int ogt_ctx_init(ogt_ctx *c, struct Screen *scr, const ogt_theme *t, int mode);
void ogt_ctx_free(ogt_ctx *c);

/* A pen for a colour (shared, obtained once). */
LONG ogt_pen_rgb(ogt_ctx *c, ogt_rgb rgb);
/* The pen for a theme key: its colour, or a gradient's middle. */
LONG ogt_pen(ogt_ctx *c, const char *key);
/* A theme colour, for drawing code that mixes its own colours. */
ogt_rgb ogt_colour(ogt_ctx *c, const char *key);

/* Fills a box with a key's gradient (or its flat colour). */
void ogt_fill(ogt_ctx *c, struct RastPort *rp, const char *key, int x, int y, int w, int h);
void ogt_fill_grad(ogt_ctx *c, struct RastPort *rp, const ogt_grad *g, int x, int y, int w, int h);

void ogt_box(struct RastPort *rp, LONG pen, int x, int y, int w, int h);
void ogt_frame(struct RastPort *rp, LONG pen, int x, int y, int w, int h);
/* A bevel: light on the top and left, dark on the bottom and right. */
void ogt_bevel(struct RastPort *rp, LONG light, LONG dark, int x, int y, int w, int h);
void ogt_hline(struct RastPort *rp, LONG pen, int x, int y, int w);
void ogt_vline(struct RastPort *rp, LONG pen, int x, int y, int h);

/* Text at a top-left corner, cut with "..." to fit maxw (0: no limit).
 * Returns the width drawn. */
int ogt_text(struct RastPort *rp, LONG pen, int x, int y, const char *s, int maxw);
int ogt_text_width(struct RastPort *rp, const char *s);
void ogt_bold(struct RastPort *rp, int on);
/* SetAPen for a pen or a direct colour (text and lines need a real pen). */
void ogt_set_apen(struct RastPort *rp, LONG pen);

/* Filled shapes, drawn as spans (no TmpRas needed). Points are x,y pairs. */
void ogt_fill_poly(struct RastPort *rp, LONG pen, const int *xy, int n);
void ogt_poly_outline(struct RastPort *rp, LONG pen, const int *xy, int n);
void ogt_fill_circle(struct RastPort *rp, LONG pen, int cx, int cy, int r);

#endif
