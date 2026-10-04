/* ogt_toolbar: a row of icon buttons (DESIGN.md section 0: icons with their
 * names under them by default; icons only; text only; or, for small rows,
 * the icon beside its name). Each button is a plain Intuition boolean
 * gadget that draws nothing itself; the toolbar draws them in the theme.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_TOOLBAR_H
#define OGT_TOOLBAR_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

enum { OGT_TB_ICONS_TEXT = 0, OGT_TB_ICONS = 1, OGT_TB_TEXT = 2, OGT_TB_INLINE = 3 };

#define OGT_TB_MAX 24

typedef struct ogt_tool {
    int id;             /* the program's command number */
    const char *label;
    int icon;           /* OGT_ICON_* */
    int sep_before;     /* a separator line before it */
    int disabled;
    int primary;        /* drawn in the accent colour (Send) */
} ogt_tool;

typedef struct ogt_toolbar {
    int n, style, icon_size;
    ogt_tool tool[OGT_TB_MAX];
    struct { int x, y, w, h; } box[OGT_TB_MAX];
    struct Gadget gad[OGT_TB_MAX];
    int x, y, w, h;     /* the row */
    int pressed;        /* the button held down, or -1 */
    int first_gid;
} ogt_toolbar;

/* Sets the buttons (copied). */
void ogt_toolbar_set(ogt_toolbar *tb, const ogt_tool *tools, int n, int style);
/* Places the buttons in a row starting at x,y (font from rp); returns the row's height. */
int ogt_toolbar_layout(ogt_toolbar *tb, struct RastPort *rp, int x, int y, int maxw);
/* The buttons' gadgets, linked in a chain, with ids from first_gid. Add them to the window. */
struct Gadget *ogt_toolbar_gadgets(ogt_toolbar *tb, int first_gid);
/* The tool for a gadget id, or -1. */
int ogt_toolbar_index(const ogt_toolbar *tb, int gadget_id);
void ogt_toolbar_draw(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, const char *bg_key);
void ogt_toolbar_draw_one(ogt_toolbar *tb, ogt_ctx *c, struct RastPort *rp, int i, int down, const char *bg_key);
void ogt_toolbar_enable(ogt_toolbar *tb, int id, int enabled);

#endif
