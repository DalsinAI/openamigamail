/* ogt_list: OpenGadTools' list (DESIGN.md section 1): drawn in the theme,
 * rows of any height (headings, three-line messages), a scroller, the
 * mouse and the cursor keys. The program draws each row; the list does the
 * rest. It sits in an ordinary GadTools window.
 *
 * The window needs IDCMP_MOUSEBUTTONS, IDCMP_GADGETUP, IDCMP_GADGETDOWN,
 * IDCMP_MOUSEMOVE and IDCMP_IDCMPUPDATE; pass its messages to
 * ogt_list_event().
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_LIST_H
#define OGT_LIST_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "ogt_draw.h"

/* A row's height; and drawing it in the box given (the list clips). */
typedef int (*ogt_list_height_fn)(void *user, int index);
typedef void (*ogt_list_draw_fn)(void *user, int index, struct RastPort *rp, int x, int y, int w, int h, int selected);
/* Whether a row can be selected (headings can't). NULL: all can. */
typedef int (*ogt_list_pickable_fn)(void *user, int index);

enum { OGT_LIST_NONE = 0, OGT_LIST_PICKED = 1, OGT_LIST_OPENED = 2, OGT_LIST_SCROLLED = 3 };

typedef struct ogt_list {
    struct Window *win;
    ogt_ctx *ctx;
    int gid;
    int x, y, w, h;             /* the whole list, scroller included */
    int sw;                     /* the scroller's width */
    int count, selected, top;   /* top: pixels scrolled */
    int total;                  /* every row's height added */
    int *offs;                  /* each row's y within the list (count + 1) */
    int scale;                  /* the scroller counts total >> scale */
    ogt_list_height_fn height;
    ogt_list_draw_fn draw;
    ogt_list_pickable_fn pickable;
    void *user;
    const char *bg_key;         /* the theme key behind the rows */
    struct Gadget *prop;
    ULONG last_secs, last_micros;
    int last_click;
} ogt_list;

ogt_list *ogt_list_new(struct Window *win, ogt_ctx *ctx, int gid, ogt_list_height_fn height,
                       ogt_list_draw_fn draw, ogt_list_pickable_fn pickable, void *user, const char *bg_key);
void ogt_list_free(ogt_list *l);
/* Places the list (and its scroller) in the window. */
void ogt_list_layout(ogt_list *l, int x, int y, int w, int h);
/* A new number of rows (heights are asked again); keeps the selection if it still fits. */
void ogt_list_set_count(ogt_list *l, int count);
void ogt_list_select(ogt_list *l, int index);
void ogt_list_show(ogt_list *l, int index);
void ogt_list_draw(ogt_list *l);
void ogt_list_draw_row(ogt_list *l, int index);
/* One IntuiMessage's worth of work. Returns OGT_LIST_*; *index gets the row. */
int ogt_list_event(ogt_list *l, ULONG class, UWORD code, APTR iaddress, int mx, int my, ULONG secs, ULONG micros, int *index);
/* Cursor up (-1) or down (+1): moves the selection. Returns the new row or -1. */
int ogt_list_step(ogt_list *l, int delta);

#endif
