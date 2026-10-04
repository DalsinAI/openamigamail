/* ogt_list: OpenGadTools' list, drawn in the theme.
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <graphics/regions.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layers.h>
#include <proto/utility.h>

#include <stdlib.h>
#include <string.h>

#include "ogt_list.h"

#define SCROLLER_W 14

ogt_list *ogt_list_new(struct Window *win, ogt_ctx *ctx, int gid, ogt_list_height_fn height,
                       ogt_list_draw_fn draw, ogt_list_pickable_fn pickable, void *user, const char *bg_key)
{
    ogt_list *l = calloc(1, sizeof *l);
    if (!l) return NULL;
    l->win = win;
    l->ctx = ctx;
    l->gid = gid;
    l->height = height;
    l->draw = draw;
    l->pickable = pickable;
    l->user = user;
    l->bg_key = bg_key;
    l->selected = -1;
    l->last_click = -1;
    l->sw = SCROLLER_W;
    return l;
}

static void remove_prop(ogt_list *l)
{
    if (l->prop) {
        RemoveGadget(l->win, l->prop);
        DisposeObject(l->prop);
        l->prop = NULL;
    }
}

void ogt_list_free(ogt_list *l)
{
    if (!l) return;
    remove_prop(l);
    free(l->offs);
    free(l);
}

static int visible_h(ogt_list *l) { return l->h > 2 ? l->h - 2 : 0; }

static void clamp_top(ogt_list *l)
{
    int max = l->total - visible_h(l);
    if (l->top > max) l->top = max;
    if (l->top < 0) l->top = 0;
}

static void update_prop(ogt_list *l)
{
    if (!l->prop) return;
    SetGadgetAttrs(l->prop, l->win, NULL,
                   PGA_Total, (ULONG)(l->total >> l->scale), PGA_Visible, (ULONG)(visible_h(l) >> l->scale),
                   PGA_Top, (ULONG)(l->top >> l->scale), TAG_DONE);
}

void ogt_list_layout(ogt_list *l, int x, int y, int w, int h)
{
    l->x = x; l->y = y; l->w = w; l->h = h;
    remove_prop(l);
    clamp_top(l);
    l->prop = (struct Gadget *)NewObject(NULL, (UBYTE *)"propgclass",
                                         GA_Left, x + w - l->sw, GA_Top, y + 1, GA_Width, l->sw - 1, GA_Height, h - 2,
                                         GA_ID, l->gid, GA_RelVerify, TRUE, GA_Immediate, TRUE,
                                         PGA_Freedom, FREEVERT, PGA_NewLook, TRUE, PGA_Borderless, TRUE,
                                         PGA_Total, (ULONG)(l->total >> l->scale), PGA_Visible, (ULONG)(visible_h(l) >> l->scale),
                                         PGA_Top, (ULONG)(l->top >> l->scale),
                                         ICA_TARGET, ICTARGET_IDCMP, TAG_DONE);
    if (l->prop) AddGadget(l->win, l->prop, (UWORD)~0);
}

void ogt_list_set_count(ogt_list *l, int count)
{
    int i;
    free(l->offs);
    l->offs = malloc((count + 1) * sizeof *l->offs);
    l->count = l->offs ? count : 0;
    l->total = 0;
    for (i = 0; l->offs && i < l->count; i++) {
        l->offs[i] = l->total;
        l->total += l->height(l->user, i);
    }
    if (l->offs) l->offs[l->count] = l->total;
    for (l->scale = 0; (l->total >> l->scale) > 32000; l->scale++) ;
    if (l->selected >= l->count) l->selected = -1;
    clamp_top(l);
    update_prop(l);
}

static void draw_rows(ogt_list *l, int only)
{
    struct RastPort *rp = l->win->RPort;
    struct Region *clip, *old;
    struct Rectangle r;
    int i, inner_w = l->w - l->sw - 1, vy = l->y + 1, vh = visible_h(l);
    if (inner_w <= 0 || vh <= 0) return;
    r.MinX = l->x + 1; r.MinY = vy; r.MaxX = l->x + inner_w; r.MaxY = vy + vh - 1;
    if (!(clip = NewRegion())) return;
    OrRectRegion(clip, &r);
    old = InstallClipRegion(l->win->WLayer, clip);
    if (only < 0) {
        int end = l->offs ? l->total - l->top : 0;
        if (end < vh) ogt_fill(l->ctx, rp, l->bg_key, l->x + 1, vy + (end > 0 ? end : 0), inner_w, vh - (end > 0 ? end : 0));
    }
    for (i = 0; i < l->count; i++) {
        int ry = vy + l->offs[i] - l->top, rh = l->offs[i + 1] - l->offs[i];
        if (ry + rh <= vy || ry >= vy + vh) continue;
        if (only >= 0 && i != only) continue;
        ogt_fill(l->ctx, rp, l->bg_key, l->x + 1, ry, inner_w, rh);
        l->draw(l->user, i, rp, l->x + 1, ry, inner_w, rh, i == l->selected);
    }
    InstallClipRegion(l->win->WLayer, old);
    DisposeRegion(clip);
}

void ogt_list_draw(ogt_list *l)
{
    struct RastPort *rp = l->win->RPort;
    ogt_bevel(rp, ogt_pen(l->ctx, "string.shadow"), ogt_pen(l->ctx, "string.shine"), l->x, l->y, l->w, l->h);
    ogt_box(rp, ogt_pen(l->ctx, "track"), l->x + l->w - l->sw, l->y + 1, l->sw - 1, l->h - 2);
    draw_rows(l, -1);
    if (l->prop) RefreshGList(l->prop, l->win, NULL, 1);
}

void ogt_list_draw_row(ogt_list *l, int index)
{
    if (index >= 0 && index < l->count) draw_rows(l, index);
}

void ogt_list_show(ogt_list *l, int index)
{
    int vh = visible_h(l), old = l->top;
    if (index < 0 || index >= l->count) return;
    if (l->offs[index] < l->top) l->top = l->offs[index];
    else if (l->offs[index + 1] > l->top + vh) l->top = l->offs[index + 1] - vh;
    clamp_top(l);
    if (l->top != old) {
        update_prop(l);
        draw_rows(l, -1);
    }
}

void ogt_list_select(ogt_list *l, int index)
{
    int old = l->selected;
    if (index >= l->count) index = -1;
    l->selected = index;
    if (old >= 0 && old != index) draw_rows(l, old);
    if (index >= 0) {
        ogt_list_show(l, index);
        draw_rows(l, index);
    }
}

static int pickable(ogt_list *l, int i)
{
    return i >= 0 && i < l->count && (!l->pickable || l->pickable(l->user, i));
}

int ogt_list_step(ogt_list *l, int delta)
{
    int i = l->selected < 0 ? (delta > 0 ? -1 : l->count) : l->selected;
    do i += delta; while (i >= 0 && i < l->count && !pickable(l, i));
    if (i < 0 || i >= l->count) return -1;
    ogt_list_select(l, i);
    return i;
}

static int row_at(ogt_list *l, int my)
{
    int y = my - (l->y + 1) + l->top, lo = 0, hi = l->count - 1;
    if (y < 0 || y >= l->total) return -1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (l->offs[mid] <= y) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

static void read_prop(ogt_list *l)
{
    ULONG top = 0;
    if (!l->prop) return;
    GetAttr(PGA_Top, l->prop, &top);
    if ((int)(top << l->scale) != l->top) {
        l->top = (int)(top << l->scale);
        clamp_top(l);
        draw_rows(l, -1);
    }
}

int ogt_list_event(ogt_list *l, ULONG class, UWORD code, APTR iaddress, int mx, int my, ULONG secs, ULONG micros, int *index)
{
    if (class == IDCMP_IDCMPUPDATE) {
        struct TagItem *tags = (struct TagItem *)iaddress;
        if (tags && GetTagData(GA_ID, 0, tags) == (ULONG)l->gid) {
            read_prop(l);
            return OGT_LIST_SCROLLED;
        }
        return OGT_LIST_NONE;
    }
    if ((class == IDCMP_GADGETUP || class == IDCMP_GADGETDOWN) && iaddress == (APTR)l->prop) {
        read_prop(l);
        return OGT_LIST_SCROLLED;
    }
    if (class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN &&
        mx > l->x && mx < l->x + l->w - l->sw && my > l->y && my < l->y + l->h - 1) {
        int i = row_at(l, my);
        if (!pickable(l, i)) return OGT_LIST_NONE;
        if (i == l->last_click && DoubleClick(l->last_secs, l->last_micros, secs, micros)) {
            l->last_click = -1;
            *index = i;
            return OGT_LIST_OPENED;
        }
        l->last_click = i;
        l->last_secs = secs;
        l->last_micros = micros;
        ogt_list_select(l, i);
        *index = i;
        return OGT_LIST_PICKED;
    }
    return OGT_LIST_NONE;
}
