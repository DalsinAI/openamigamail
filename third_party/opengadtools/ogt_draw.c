/* ogt_draw: drawing a theme on an Amiga screen.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <inline/macros.h>
#include <string.h>

#include "ogt_draw.h"

/* cybergraphics.library (Picasso96 and CyberGraphX provide it), two calls. */
static struct Library *CyberGfxBase;
#define CGX_ISCYBERGFX 0x80000008UL
#define CGX_FILL(rp, x, y, w, h, argb) LP6(0x96, ULONG, FillPixelArray, struct RastPort *, (rp), a1, UWORD, (x), d0, UWORD, (y), d1, UWORD, (w), d2, UWORD, (h), d3, ULONG, (argb), d4, , CyberGfxBase)
#define CGX_MAPATTR(bm, a) LP2(0x60, ULONG, GetCyberMapAttr, struct BitMap *, (bm), a0, ULONG, (a), d0, , CyberGfxBase)

static ogt_ctx *current;             /* the context direct colours are matched in */

static ULONG pack(ogt_rgb c) { return ((ULONG)c.r << 16) | ((ULONG)c.g << 8) | c.b; }
static ULONG x32(int v) { return (ULONG)v * 0x01010101UL; }

static int screen_depth(struct Screen *scr)
{
    if (((struct Library *)GfxBase)->lib_Version >= 39)
        return (int)GetBitMapAttr(scr->RastPort.BitMap, BMA_DEPTH);
    return scr->RastPort.BitMap->Depth;
}

int ogt_ctx_init(ogt_ctx *c, struct Screen *scr, const ogt_theme *t, int mode)
{
    int depth, i;
    ogt_rgb pens[4];
    memset(c, 0, sizeof *c);
    c->scr = scr;
    c->cm = scr->ViewPort.ColorMap;
    c->theme = t;
    c->mode = mode == OGT_DARK && t->has_dark ? OGT_DARK : OGT_LIGHT;
    depth = screen_depth(scr);
    c->bands = depth > 8 ? OGT_RTG_BANDS : OGT_AGA_BANDS;
    c->tf = 3; c->tt = 1; c->ft = 1;
    current = c;
    if (depth > 8 && !t->passthrough) {
        if (!CyberGfxBase) CyberGfxBase = OpenLibrary((STRPTR)"cybergraphics.library", 40);
        if (CyberGfxBase && CGX_MAPATTR(scr->RastPort.BitMap, CGX_ISCYBERGFX)) c->direct = 1;
    }
    /* Classic, and palette screens of fewer than 64 colours (THEME_SPEC.md 6): four pens */
    if (t->passthrough || ((struct Library *)GfxBase)->lib_Version < 39 || depth < 6) {
        struct DrawInfo *di = GetScreenDrawInfo(scr);
        c->four = 1;
        if (!t->passthrough && ogt_theme_four(t, pens, &c->tf, &c->tt, &c->ft) && ((struct Library *)GfxBase)->lib_Version >= 39 && depth > 3) {
            for (i = 0; i < 4; i++) {
                c->fourpen[i] = ObtainBestPen(c->cm, x32(pens[i].r), x32(pens[i].g), x32(pens[i].b), OBP_Precision, PRECISION_GUI, TAG_DONE);
                c->fourown[i] = c->fourpen[i] >= 0;
            }
        } else if (di) {                                    /* the screen's own pens: what the OS draws */
            c->fourpen[0] = di->dri_Pens[BACKGROUNDPEN];
            c->fourpen[1] = di->dri_Pens[TEXTPEN];
            c->fourpen[2] = di->dri_Pens[SHINEPEN];
            c->fourpen[3] = di->dri_Pens[FILLPEN];
            ogt_theme_four(t, pens, &c->tf, &c->tt, &c->ft);
            if (t->passthrough) { c->tf = 3; c->tt = 1; c->ft = 1; }
        }
        for (i = 0; i < 4; i++) if (c->fourpen[i] < 0) c->fourpen[i] = i == 0 ? 0 : 1;
        if (di) FreeScreenDrawInfo(scr, di);
    }
    return 1;
}

void ogt_ctx_free(ogt_ctx *c)
{
    int i;
    if (!c->cm) return;
    for (i = 0; i < c->n; i++) if (c->pen[i] >= 0) ReleasePen(c->cm, c->pen[i]);
    for (i = 0; i < 4; i++) if (c->fourown[i]) ReleasePen(c->cm, c->fourpen[i]);
    for (i = 0; i < c->tn; i++) if (c->tpen[i] >= 0) ReleasePen(c->cm, c->tpen[i]);
    if (current == c) current = NULL;
    memset(c, 0, sizeof *c);
}

LONG ogt_pen_rgb(ogt_ctx *c, ogt_rgb rgb)
{
    ULONG want = pack(rgb);
    int i, best = -1;
    long bestd = 0x7fffffffL;
    LONG p;
    if (c->direct) return OGT_DIRECT | (LONG)want;
    for (i = 0; i < c->n; i++) if (c->rgb[i] == want) return c->pen[i];
    if (c->n < OGT_MAX_PENS && ((struct Library *)GfxBase)->lib_Version >= 39) {
        p = ObtainBestPen(c->cm, x32(rgb.r), x32(rgb.g), x32(rgb.b), OBP_Precision, PRECISION_GUI, TAG_DONE);
        if (p >= 0) {
            c->rgb[c->n] = want;
            c->pen[c->n] = p;
            c->n++;
            return p;
        }
    }
    for (i = 0; i < c->n; i++) {                            /* out of pens: the nearest we have */
        long dr = (long)((c->rgb[i] >> 16) & 255) - rgb.r, dg = (long)((c->rgb[i] >> 8) & 255) - rgb.g, db = (long)(c->rgb[i] & 255) - rgb.b;
        long d = dr * dr + dg * dg + db * db;
        if (d < bestd) { bestd = d; best = i; }
    }
    return best >= 0 ? c->pen[best] : 1;
}

ogt_rgb ogt_colour(ogt_ctx *c, const char *key)
{
    ogt_rgb out = { 0, 0, 0 };
    if (!ogt_theme_colour(c->theme, c->mode, key, &out)) {
        ogt_rgb pens[4];
        int tf, tt, ft;
        if (ogt_theme_four(c->theme, pens, &tf, &tt, &ft)) out = pens[ogt_four_role(key, tf, tt, ft) & 3];
    }
    return out;
}

LONG ogt_pen(ogt_ctx *c, const char *key)
{
    ogt_rgb rgb;
    if (c->four) {
        int role = ogt_four_role(key, c->tf, c->tt, c->ft);
        return c->fourpen[role < 0 ? 0 : role & 3];
    }
    if (!ogt_theme_colour(c->theme, c->mode, key, &rgb)) return c->fourpen[1] ? c->fourpen[1] : 1;
    return ogt_pen_rgb(c, rgb);
}

/* A real pen for a direct colour: text and lines can't use FillPixelArray. */
static LONG real_pen(LONG h)
{
    ogt_ctx *c = current;
    ULONG rgb;
    LONG p;
    int i;
    if (!(h & OGT_DIRECT)) return h;
    rgb = (ULONG)h & 0xffffffUL;
    if (!c) return 1;
    for (i = 0; i < c->tn; i++) if (c->trgb[i] == rgb) return c->tpen[i];
    p = ObtainBestPen(c->cm, x32((rgb >> 16) & 255), x32((rgb >> 8) & 255), x32(rgb & 255), OBP_Precision, PRECISION_GUI, TAG_DONE);
    if (p < 0) return 1;
    if (c->tn < 32) { c->trgb[c->tn] = rgb; c->tpen[c->tn] = p; c->tn++; }
    else ReleasePen(c->cm, p);                              /* out of slots: use it this once */
    return p;
}

void ogt_set_apen(struct RastPort *rp, LONG pen)
{
    SetAPen(rp, real_pen(pen));
}

void ogt_box(struct RastPort *rp, LONG pen, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    if ((pen & OGT_DIRECT) && CyberGfxBase) {
        CGX_FILL(rp, x, y, w, h, (ULONG)pen & 0xffffffUL);
        return;
    }
    SetAPen(rp, real_pen(pen));
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

void ogt_hline(struct RastPort *rp, LONG pen, int x, int y, int w) { ogt_box(rp, pen, x, y, w, 1); }
void ogt_vline(struct RastPort *rp, LONG pen, int x, int y, int h) { ogt_box(rp, pen, x, y, 1, h); }

void ogt_frame(struct RastPort *rp, LONG pen, int x, int y, int w, int h)
{
    ogt_hline(rp, pen, x, y, w);
    ogt_hline(rp, pen, x, y + h - 1, w);
    ogt_vline(rp, pen, x, y, h);
    ogt_vline(rp, pen, x + w - 1, y, h);
}

void ogt_bevel(struct RastPort *rp, LONG light, LONG dark, int x, int y, int w, int h)
{
    ogt_hline(rp, light, x, y, w - 1);
    ogt_vline(rp, light, x, y, h - 1);
    ogt_hline(rp, dark, x + 1, y + h - 1, w - 1);
    ogt_vline(rp, dark, x + w - 1, y + 1, h - 1);
}

void ogt_fill_grad(ogt_ctx *c, struct RastPort *rp, const ogt_grad *g, int x, int y, int w, int h)
{
    int bands, i, along;
    if (w <= 0 || h <= 0) return;
    if (c->four) { ogt_box(rp, c->fourpen[0], x, y, w, h); return; }
    along = g->kind == OGT_HORIZONTAL ? w : h;
    bands = c->bands;
    if (bands > along) bands = along;
    if (bands < 1) bands = 1;
    if (g->n == 2 && g->c[0].r == g->c[1].r && g->c[0].g == g->c[1].g && g->c[0].b == g->c[1].b) bands = 1;
    for (i = 0; i < bands; i++) {
        int a = along * i / bands, b = along * (i + 1) / bands;
        int mid = (2 * i + 1) * 1000 / (2 * bands);
        LONG pen = ogt_pen_rgb(c, ogt_grad_at10(g, mid));
        /* a radial gradient is drawn as rings from the edge inwards */
        if (g->kind == OGT_RADIAL) {
            int ox = w * i / (2 * bands), oy = h * i / (2 * bands);
            ogt_box(rp, ogt_pen_rgb(c, ogt_grad_at10(g, 1000 - mid)), x + ox, y + oy, w - 2 * ox, h - 2 * oy);
            continue;
        }
        if (g->kind == OGT_HORIZONTAL) ogt_box(rp, pen, x + a, y, b - a, h);
        else ogt_box(rp, pen, x, y + a, w, b - a);
    }
}

void ogt_fill(ogt_ctx *c, struct RastPort *rp, const char *key, int x, int y, int w, int h)
{
    ogt_grad g;
    if (c->four || !ogt_theme_grad(c->theme, c->mode, key, &g)) {
        ogt_box(rp, ogt_pen(c, key), x, y, w, h);
        return;
    }
    ogt_fill_grad(c, rp, &g, x, y, w, h);
}

int ogt_text_width(struct RastPort *rp, const char *s)
{
    return TextLength(rp, (STRPTR)s, strlen(s));
}

void ogt_bold(struct RastPort *rp, int on)
{
    SetSoftStyle(rp, on ? FSF_BOLD : 0, FSF_BOLD);
}

int ogt_text(struct RastPort *rp, LONG pen, int x, int y, const char *s, int maxw)
{
    int len = strlen(s), w = TextLength(rp, (STRPTR)s, len);
    SetAPen(rp, real_pen(pen));
    SetDrMd(rp, JAM1);
    Move(rp, x, y + rp->TxBaseline);
    if (maxw <= 0 || w <= maxw) {
        Text(rp, (STRPTR)s, len);
        return w;
    } else {
        struct TextExtent te;
        int dots = TextLength(rp, (STRPTR)"...", 3), fit;
        if (maxw <= dots) return 0;
        fit = TextFit(rp, (STRPTR)s, len, &te, NULL, 1, maxw - dots, 32767);
        Text(rp, (STRPTR)s, fit);
        Text(rp, (STRPTR)"...", 3);
        return TextLength(rp, (STRPTR)s, fit) + dots;
    }
}

/* Even-odd scanline fill of a polygon, one RectFill per span. */
void ogt_fill_poly(struct RastPort *rp, LONG pen, const int *xy, int n)
{
    int ymin = 32767, ymax = -32768, i, y;
    int xs[32];
    if (n < 3) return;
    for (i = 0; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    if (!(pen & OGT_DIRECT)) SetAPen(rp, pen);
    for (y = ymin; y <= ymax; y++) {
        int k = 0, a, b, sy2 = 2 * y + 1;                  /* the scanline's centre, doubled */
        for (i = 0; i < n && k < 32; i++) {
            int j = (i + 1) % n;
            int y0 = xy[2 * i + 1], y1 = xy[2 * j + 1];
            if ((2 * y0 <= sy2 && 2 * y1 > sy2) || (2 * y1 <= sy2 && 2 * y0 > sy2)) {
                long x0 = xy[2 * i], x1 = xy[2 * j], num, den, q;
                if (y0 > y1) { long t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = (int)t; }   /* top to bottom */
                num = (long)(sy2 - 2 * y0) * (x1 - x0);
                den = 2L * (y1 - y0);
                q = num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den);
                xs[k++] = (int)(x0 + q);
            }
        }
        for (a = 1; a < k; a++)                              /* sort the crossings */
            for (b = a; b > 0 && xs[b - 1] > xs[b]; b--) { int t = xs[b]; xs[b] = xs[b - 1]; xs[b - 1] = t; }
        for (a = 0; a + 1 < k; a += 2)
            if (xs[a + 1] > xs[a]) {
                if (pen & OGT_DIRECT) ogt_box(rp, pen, xs[a], y, xs[a + 1] - xs[a], 1);
                else RectFill(rp, xs[a], y, xs[a + 1] - 1, y);
            }
    }
}

void ogt_poly_outline(struct RastPort *rp, LONG pen, const int *xy, int n)
{
    int i;
    if (n < 2) return;
    SetAPen(rp, real_pen(pen));
    Move(rp, xy[0], xy[1]);
    for (i = 1; i < n; i++) Draw(rp, xy[2 * i], xy[2 * i + 1]);
    Draw(rp, xy[0], xy[1]);
}

void ogt_fill_circle(struct RastPort *rp, LONG pen, int cx, int cy, int r)
{
    int dy;
    if (r <= 0) return;
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        ogt_box(rp, pen, cx - dx, cy + dy, 2 * dx + 1, 1);
    }
}
