/* ogt_icons: icons drawn from shapes on a 48 x 48 grid.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "ogt_icons.h"

/* The icon colours: the GlowIcons-style fills and their outlines. */
static const ogt_rgb colours[] = {
    {0, 0, 0},                                                   /* 0: none */
    {0x1d, 0x24, 0x33}, {0xf4, 0xd3, 0x6b}, {0x5b, 0x45, 0x10},  /* 1 ink, 2 envelope, 3 its edge */
    {0x3f, 0x9a, 0x3a}, {0x1d, 0x4d, 0x1b}, {0x6f, 0x9a, 0xd8},  /* 4 green, 5 its edge, 6 blue */
    {0x1d, 0x3b, 0x6b}, {0x7c, 0xc0, 0x6d}, {0x21, 0x51, 0x1a},  /* 7 navy, 8 light green, 9 its edge */
    {0xd9, 0xdd, 0xe3}, {0x3a, 0x3f, 0x47}, {0xe8, 0x57, 0x4a},  /* 10 light grey, 11 slate, 12 red */
    {0x6b, 0x1d, 0x16}, {0xc7, 0x9a, 0x5a}, {0x4e, 0x35, 0x10},  /* 13 dark red, 14 tan, 15 brown */
    {0xe3, 0xbd, 0x82}, {0xf2, 0xa1, 0x3a}, {0x6a, 0x3c, 0x06},  /* 16 light tan, 17 orange, 18 its edge */
    {0xf0, 0xc2, 0x5e}, {0x5a, 0x41, 0x10}, {0xff, 0xff, 0xff},  /* 19 folder, 20 its edge, 21 white */
    {0x8a, 0x92, 0x9e}, {0x9c, 0xc0, 0xef}, {0x36, 0x5f, 0xa3},  /* 22 grey, 23 pale blue, 24 accent blue */
    {0x14, 0x26, 0x4a}, {0xa9, 0xb0, 0xba}, {0x2f, 0x7a, 0x2a},  /* 25 deep blue, 26 grey, 27 refresh green */
};

enum { S_END, S_POLY, S_LINE, S_RECT, S_CIRCLE, S_RING };

typedef struct shape {
    unsigned char kind, fill, line, thick, n;   /* n: points for POLY and LINE */
    signed char p[24];                          /* x,y pairs on the 48 grid; RECT x0,y0,x1,y1; CIRCLE cx,cy,r */
} shape;

#define END { S_END, 0, 0, 0, 0, {0} }

static const shape icons[OGT_ICON_COUNT][6] = {
    [OGT_ICON_NEWMAIL] = {
        { S_RECT, 2, 3, 1, 0, {5, 13, 39, 37} },
        { S_LINE, 0, 3, 1, 3, {5, 15, 22, 27, 39, 15} },
        { S_CIRCLE, 4, 5, 1, 0, {38, 12, 9} },
        { S_LINE, 0, 21, 2, 2, {38, 7, 38, 17} },
        { S_LINE, 0, 21, 2, 2, {33, 12, 43, 12} }, END },
    [OGT_ICON_REPLY] = {
        { S_POLY, 6, 7, 1, 10, {20, 10, 6, 23, 20, 36, 20, 28, 30, 28, 36, 31, 42, 38, 40, 26, 33, 19, 20, 18} }, END },
    [OGT_ICON_REPLYALL] = {
        { S_LINE, 0, 7, 2, 3, {16, 12, 4, 23, 16, 34} },
        { S_POLY, 6, 7, 1, 10, {26, 10, 14, 23, 26, 36, 26, 28, 34, 28, 39, 31, 44, 38, 42, 26, 36, 19, 26, 18} }, END },
    [OGT_ICON_FORWARD] = {
        { S_POLY, 8, 9, 1, 10, {28, 10, 42, 23, 28, 36, 28, 28, 18, 28, 12, 31, 6, 38, 8, 26, 15, 19, 28, 18} }, END },
    [OGT_ICON_DELETE] = {
        { S_RECT, 10, 11, 1, 0, {11, 15, 37, 41} },
        { S_RECT, 12, 13, 1, 0, {7, 9, 41, 15} },
        { S_LINE, 0, 11, 1, 2, {19, 21, 19, 35} },
        { S_LINE, 0, 11, 1, 2, {29, 21, 29, 35} }, END },
    [OGT_ICON_ARCHIVE] = {
        { S_RECT, 16, 15, 1, 0, {9, 19, 39, 39} },
        { S_RECT, 14, 15, 1, 0, {7, 11, 41, 19} },
        { S_RECT, 15, 15, 1, 0, {19, 24, 29, 28} }, END },
    [OGT_ICON_JUNK] = {
        { S_CIRCLE, 17, 18, 1, 0, {24, 24, 17} },
        { S_LINE, 0, 18, 3, 2, {13, 13, 35, 35} }, END },
    [OGT_ICON_MOVE] = {
        { S_POLY, 19, 20, 1, 6, {7, 15, 19, 15, 23, 19, 41, 19, 41, 37, 7, 37} },
        { S_LINE, 0, 20, 2, 2, {18, 28, 32, 28} },
        { S_LINE, 0, 20, 2, 3, {27, 23, 32, 28, 27, 33} }, END },
    [OGT_ICON_FLAG] = {
        { S_LINE, 0, 11, 2, 2, {13, 6, 13, 43} },
        { S_POLY, 12, 13, 1, 5, {14, 9, 38, 9, 32, 17, 38, 25, 14, 25} }, END },
    [OGT_ICON_UNREAD] = {
        { S_POLY, 2, 3, 1, 5, {7, 20, 24, 9, 41, 20, 41, 39, 7, 39} },
        { S_LINE, 0, 3, 1, 3, {7, 20, 24, 30, 41, 20} },
        { S_CIRCLE, 24, 25, 1, 0, {38, 12, 7} }, END },
    [OGT_ICON_GETMAIL] = {
        { S_LINE, 0, 27, 3, 10, {38, 24, 36, 32, 31, 37, 24, 39, 17, 37, 12, 32, 10, 24, 12, 16, 17, 11, 25, 9} },
        { S_POLY, 27, 27, 1, 3, {26, 3, 38, 10, 28, 17} }, END },
    [OGT_ICON_INBOX] = {
        { S_POLY, 23, 7, 1, 6, {5, 26, 11, 8, 37, 8, 43, 26, 43, 40, 5, 40} },
        { S_LINE, 0, 7, 1, 6, {5, 26, 15, 26, 18, 31, 30, 31, 33, 26, 43, 26} }, END },
    [OGT_ICON_SENT] = {
        { S_POLY, 23, 7, 1, 4, {4, 22, 44, 4, 33, 44, 23, 29} },
        { S_LINE, 0, 7, 1, 2, {23, 29, 44, 4} }, END },
    [OGT_ICON_DRAFT] = {
        { S_RECT, 21, 11, 1, 0, {8, 4, 36, 43} },
        { S_LINE, 0, 22, 1, 2, {14, 14, 30, 14} },
        { S_LINE, 0, 22, 1, 2, {14, 21, 30, 21} },
        { S_LINE, 0, 22, 1, 2, {14, 28, 24, 28} },
        { S_POLY, 2, 3, 1, 5, {29, 40, 43, 26, 46, 29, 32, 43, 28, 44} }, END },
    [OGT_ICON_TRASH] = {
        { S_RECT, 10, 11, 1, 0, {11, 13, 37, 44} },
        { S_RECT, 26, 11, 1, 0, {6, 7, 42, 13} }, END },
    [OGT_ICON_FOLDER] = {
        { S_POLY, 19, 20, 1, 6, {4, 10, 18, 10, 22, 15, 44, 15, 44, 40, 4, 40} }, END },
    [OGT_ICON_ATTACH] = {
        { S_LINE, 0, 11, 2, 12, {33, 16, 19, 30, 19, 34, 23, 37, 39, 21, 39, 14, 34, 10, 28, 11, 12, 27, 13, 36, 20, 42, 42, 32} }, END },
    [OGT_ICON_SEARCH] = {
        { S_RING, 0, 11, 3, 0, {20, 20, 12} },
        { S_LINE, 0, 11, 4, 2, {29, 29, 42, 42} }, END },
    [OGT_ICON_CONTACTS] = {
        { S_CIRCLE, 23, 7, 1, 0, {24, 16, 9} },
        { S_POLY, 23, 7, 1, 6, {7, 42, 10, 31, 18, 27, 30, 27, 38, 31, 41, 42} }, END },
    [OGT_ICON_MAIL] = {
        { S_RECT, 2, 3, 1, 0, {5, 11, 43, 37} },
        { S_LINE, 0, 3, 1, 3, {5, 13, 24, 26, 43, 13} }, END },
    [OGT_ICON_SEND] = {
        { S_POLY, 21, 25, 1, 4, {4, 22, 44, 4, 33, 44, 23, 29} },
        { S_LINE, 0, 25, 1, 2, {23, 29, 44, 4} }, END },
    [OGT_ICON_SETTINGS] = {
        { S_CIRCLE, 22, 11, 1, 0, {24, 24, 15} },
        { S_CIRCLE, 21, 11, 1, 0, {24, 24, 6} }, END },
};

static int sc(int v, int size) { return (v * size + 24) / 48; }

static void line(struct RastPort *rp, LONG pen, const int *xy, int n, int thick)
{
    int t, i;
    ogt_set_apen(rp, pen);
    for (t = 0; t < thick; t++) {
        int ox = t & 1 ? 1 : 0, oy = t & 2 ? 1 : 0;
        Move(rp, xy[0] + ox, xy[1] + oy);
        for (i = 1; i < n; i++) Draw(rp, xy[2 * i] + ox, xy[2 * i + 1] + oy);
    }
}

void ogt_icon_draw(ogt_ctx *c, struct RastPort *rp, int icon, int x, int y, int size, int disabled)
{
    const shape *s;
    LONG faded_fill = 0, faded_line = 0;
    if (icon <= OGT_ICON_NONE || icon >= OGT_ICON_COUNT) return;
    if (disabled) {
        faded_fill = ogt_pen(c, "track");
        faded_line = ogt_pen(c, "muted");
    }
    for (s = icons[icon]; s->kind != S_END; s++) {
        LONG fill = disabled ? faded_fill : (s->fill ? ogt_pen_rgb(c, colours[s->fill]) : -1);
        LONG edge = disabled ? faded_line : ogt_pen_rgb(c, colours[s->line]);
        int pts[24], i, thick = s->thick * size / 24;
        if (thick < 1) thick = 1;
        if (thick > 3) thick = 3;
        switch (s->kind) {
        case S_POLY:
            for (i = 0; i < s->n; i++) { pts[2 * i] = x + sc(s->p[2 * i], size); pts[2 * i + 1] = y + sc(s->p[2 * i + 1], size); }
            if (fill >= 0) ogt_fill_poly(rp, fill, pts, s->n);
            ogt_poly_outline(rp, edge, pts, s->n);
            break;
        case S_LINE:
            for (i = 0; i < s->n; i++) { pts[2 * i] = x + sc(s->p[2 * i], size); pts[2 * i + 1] = y + sc(s->p[2 * i + 1], size); }
            line(rp, edge, pts, s->n, thick);
            break;
        case S_RECT: {
            int x0 = x + sc(s->p[0], size), y0 = y + sc(s->p[1], size), x1 = x + sc(s->p[2], size), y1 = y + sc(s->p[3], size);
            if (fill >= 0) ogt_box(rp, fill, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
            ogt_frame(rp, edge, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
            break;
        }
        case S_CIRCLE: {
            int cx = x + sc(s->p[0], size), cy = y + sc(s->p[1], size), r = sc(s->p[2], size);
            ogt_fill_circle(rp, edge, cx, cy, r);
            if (fill >= 0 && r > 1) ogt_fill_circle(rp, fill, cx, cy, r - 1);
            break;
        }
        case S_RING: {
            int cx = x + sc(s->p[0], size), cy = y + sc(s->p[1], size), r = sc(s->p[2], size), k;
            int ring[2 * 12];
            for (k = 0; k < 12; k++) {
                static const signed char cosx[12] = { 100, 87, 50, 0, -50, -87, -100, -87, -50, 0, 50, 87 };
                static const signed char siny[12] = { 0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50 };
                ring[2 * k] = cx + cosx[k] * r / 100;
                ring[2 * k + 1] = cy + siny[k] * r / 100;
            }
            for (k = 0; k < thick; k++) {
                int j;
                for (j = 0; j < 12; j++) { ring[2 * j] += (k & 1); ring[2 * j + 1] += (k & 2) ? 1 : 0; }
                ogt_poly_outline(rp, edge, ring, 12);
                for (j = 0; j < 12; j++) { ring[2 * j] -= (k & 1); ring[2 * j + 1] -= (k & 2) ? 1 : 0; }
            }
            break;
        }
        }
    }
}
