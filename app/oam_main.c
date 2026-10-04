/*
 * OpenMail: the main window (DESIGN.md 2), GadTools.
 *
 *   folders | messages
 *           | From / Subject / Date
 *           | the message, as text
 *   [View in browser] [Links...]          the status line
 *
 * The window never talks to a server: the worker (oam_worker.c) does, and
 * the window waits for its answers alongside the user's input. This is M3:
 * reading, with the account window (oam_accountwin.c). Writing and replying
 * come next (M4).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <graphics/gfxbase.h>
#include <utility/date.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oam_account.h"
#include "oam_mime.h"
#include "oam_text.h"
#include "oam_worker.h"
#include "oam_browser.h"
#include "oam_stack.h"
#include "oam_accountwin.h"

struct Library *GadToolsBase = NULL;      /* ours, not libnix's auto-open stub */

#define VERSION_TEXT "OpenMail 0.2 (4.10.2026)"
static const char version[] __attribute__((used)) = "$VER: " VERSION_TEXT;
#define ACCOUNT_FILE "ENV:OpenMail/Account"

enum { GID_GET = 1, GID_WRITE, GID_REPLY, GID_REPLYALL, GID_FORWARD, GID_DELETE,
       GID_FOLDERS, GID_MESSAGES, GID_FROM, GID_SUBJECT, GID_DATE, GID_BODY,
       GID_BROWSER, GID_LINKS, GID_STATUS, GID_COUNT };
enum { M_ABOUT = 1, M_QUIT, M_GET, M_BROWSER, M_LINKS, M_ACCOUNT };

#define PAD 4
#define MARGIN 6

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "Account...", "A", 0, 0, (APTR)M_ACCOUNT },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, "Mailbox", NULL, 0, 0, NULL },
    { NM_ITEM, "Get mail", "G", 0, 0, (APTR)M_GET },
    { NM_TITLE, "Message", NULL, 0, 0, NULL },
    { NM_ITEM, "View in browser", "B", 0, 0, (APTR)M_BROWSER },
    { NM_ITEM, "Links...", "L", 0, 0, (APTR)M_LINKS },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

/* ---- the state ------------------------------------------------------------------ */

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *glist, *gad[GID_COUNT];
static struct TextFont *font;
static struct TextAttr font_attr, fixed_attr;
static int fh, cw, line_h, btn_h;      /* the screen font's height; the fixed font's width and line */
static struct MsgPort *replies;
static int busy_jobs;
static BOOL quit_now;

static oam_account account;
static BOOL have_account, connected;
static void account_menu(void);
static char status[200] = "Starting...";

/* A list a listview shows: nodes over Latin-1 strings, both ours. */
struct rows {
    struct List list;
    struct Node *node;
    char **text;
    int count;
};
static struct rows folder_rows, message_rows, body_rows;

static oam_imap_folder *folders;
static int nfolders, folder_sel = -1;
static oam_imap_msg *msgs;             /* newest first */
static int nmsgs, msg_sel = -1;
static unsigned long showing_uid, wanted_uid;
static char from_text[160], subject_text[200], date_text[64];
static oam_view view;
static BOOL have_view;

/* ---- lists ------------------------------------------------------------------------ */

static void rows_free(struct rows *r)
{
    int i;
    for (i = 0; i < r->count; i++) free(r->text[i]);
    free(r->text);
    free(r->node);
    memset(r, 0, sizeof *r);
    NewList(&r->list);
}

/* Takes ownership of text (malloc'd strings). */
static void rows_set(struct rows *r, char **text, int count)
{
    int i;
    rows_free(r);
    r->text = text;
    r->count = count;
    r->node = calloc(count ? count : 1, sizeof *r->node);
    NewList(&r->list);
    for (i = 0; r->node && i < count; i++) {
        r->node[i].ln_Name = text[i];
        AddTail(&r->list, &r->node[i]);
    }
}

static void attach(int id, struct rows *r)
{
    if (win && gad[id]) {
        GT_SetGadgetAttrs(gad[id], win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
        GT_SetGadgetAttrs(gad[id], win, NULL, GTLV_Labels, (ULONG)&r->list, TAG_DONE);
    }
}

static void detach(int id)
{
    if (win && gad[id]) GT_SetGadgetAttrs(gad[id], win, NULL, GTLV_Labels, ~0UL, TAG_DONE);
}

static char *latin1(const char *utf8)
{
    char *s = oam_utf8_to_latin1(utf8 ? utf8 : "");
    return s ? s : strdup("");
}

/* ---- the status line, busy pointer ---------------------------------------------- */

static void set_status(const char *fmt, const char *arg)
{
    snprintf(status, sizeof status, fmt, arg ? arg : "");
    if (win && gad[GID_STATUS]) GT_SetGadgetAttrs(gad[GID_STATUS], win, NULL, GTTX_Text, (ULONG)status, TAG_DONE);
}

static void busy(int delta)
{
    busy_jobs += delta;
    if (win && ((struct Library *)IntuitionBase)->lib_Version >= 39) {
        if (busy_jobs > 0) SetWindowPointer(win, WA_BusyPointer, TRUE, WA_PointerDelay, TRUE, TAG_DONE);
        else SetWindowPointer(win, TAG_DONE);
    }
}

static void send(struct oam_job *j, const char *what, const char *arg)
{
    if (!j) { set_status("Out of memory", NULL); return; }
    set_status(what, arg);
    busy(1);
    oam_worker_put(j);
}

/* ---- what the lists show ----------------------------------------------------------- */

static void show_folders(void)
{
    char **t = calloc(nfolders ? nfolders : 1, sizeof *t);
    int i;
    for (i = 0; t && i < nfolders; i++) t[i] = latin1(folders[i].display ? folders[i].display : folders[i].name);
    detach(GID_FOLDERS);
    rows_set(&folder_rows, t, t ? nfolders : 0);
    attach(GID_FOLDERS, &folder_rows);
    if (win && gad[GID_FOLDERS] && folder_sel >= 0)
        GT_SetGadgetAttrs(gad[GID_FOLDERS], win, NULL, GTLV_Selected, folder_sel, TAG_DONE);
}

static void short_date(const char *raw, char *out, size_t size)
{
    long long when;
    int zone;
    struct ClockData cd;
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    out[0] = 0;
    if (!raw || !oam_hdr_date(raw, &when, &zone)) return;
    when += zone * 60LL - 252460800LL;                  /* the sender's local time, as Amiga seconds */
    if (when < 0) return;
    Amiga2Date((ULONG)when, &cd);
    snprintf(out, size, "%2u %s %02u:%02u", (unsigned)cd.mday, mon[(cd.month + 11) % 12], (unsigned)cd.hour, (unsigned)cd.min);
}

static int newest_first(const void *a, const void *b)
{
    const oam_imap_msg *x = a, *y = b;
    return x->uid < y->uid ? 1 : x->uid > y->uid ? -1 : 0;
}

static void show_messages(void)
{
    char **t;
    int i;
    qsort(msgs, nmsgs, sizeof *msgs, newest_first);
    t = calloc(nmsgs ? nmsgs : 1, sizeof *t);
    for (i = 0; t && i < nmsgs; i++) {
        const char *h = msgs[i].header ? msgs[i].header : "";
        size_t hl = strlen(h);
        char *from = oam_hdr_get(h, hl, "From"), *subj = oam_hdr_get(h, hl, "Subject"), *date = oam_hdr_get(h, hl, "Date");
        char *name = NULL, *addr = NULL, *dsubj = subj ? oam_hdr_decode(subj) : NULL, *dname, *lname, *lsubj, when[24];
        if (from) oam_hdr_address(from, &name, &addr);
        dname = name && name[0] ? oam_hdr_decode(name) : NULL;
        lname = latin1(dname ? dname : addr ? addr : from ? from : "");
        lsubj = latin1(dsubj ? dsubj : subj ? subj : "(no subject)");
        short_date(date ? date : msgs[i].internaldate, when, sizeof when);
        t[i] = malloc(160);
        if (t[i]) snprintf(t[i], 160, "%c%c %-22.22s %-46.46s %s", msgs[i].flags & OAM_F_SEEN ? ' ' : '*',
                           msgs[i].flags & OAM_F_FLAGGED ? '!' : ' ', lname, lsubj, when);
        free(from); free(subj); free(date); free(name); free(addr); free(dsubj); free(dname); free(lname); free(lsubj);
        if (!t[i]) t[i] = strdup("");
    }
    detach(GID_MESSAGES);
    rows_set(&message_rows, t, t ? nmsgs : 0);
    attach(GID_MESSAGES, &message_rows);
}

/* The message's text, wrapped to the body's width. */
static void show_body(void)
{
    const char *s;
    char **t = NULL;
    int n = 0, cap = 0, cols = 76;
    if (gad[GID_BODY] && cw) cols = (gad[GID_BODY]->Width - 24) / cw;
    if (cols < 20) cols = 20;
    for (s = have_view ? oam_buf_str(&view.text) : ""; *s; ) {
        size_t len = strcspn(s, "\n");
        char *line = malloc(len + 1), *l1, *p;
        if (!line) break;
        memcpy(line, s, len);
        line[len] = 0;
        s += len + (s[len] == '\n');
        l1 = latin1(line);
        free(line);
        for (p = l1; ; ) {                                /* wrap at the last space that fits */
            size_t pl = strlen(p), cut = pl;
            if ((int)pl > cols) {
                cut = cols;
                while (cut > 0 && p[cut] != ' ') cut--;
                if (cut == 0) cut = cols;
            }
            if (n == cap) {
                char **nt = realloc(t, (cap = cap ? cap * 2 : 64) * sizeof *t);
                if (!nt) break;
                t = nt;
            }
            t[n] = malloc(cut + 1);
            if (!t[n]) break;
            memcpy(t[n], p, cut);
            t[n++][cut] = 0;
            p += cut;
            while (*p == ' ') p++;
            if (!*p) break;
        }
        free(l1);
    }
    detach(GID_BODY);
    rows_set(&body_rows, t, n);
    attach(GID_BODY, &body_rows);
    if (win) {
        GT_SetGadgetAttrs(gad[GID_FROM], win, NULL, GTTX_Text, (ULONG)from_text, TAG_DONE);
        GT_SetGadgetAttrs(gad[GID_SUBJECT], win, NULL, GTTX_Text, (ULONG)subject_text, TAG_DONE);
        GT_SetGadgetAttrs(gad[GID_DATE], win, NULL, GTTX_Text, (ULONG)date_text, TAG_DONE);
        GT_SetGadgetAttrs(gad[GID_BROWSER], win, NULL, GA_Disabled, !(have_view && view.has_html), TAG_DONE);
        GT_SetGadgetAttrs(gad[GID_LINKS], win, NULL, GA_Disabled, !(have_view && view.links.len), TAG_DONE);
    }
}

static void read_message(const oam_buf *raw)
{
    const char *m = oam_buf_str(raw), *e = strstr(m, "\r\n\r\n");
    size_t hl = e ? (size_t)(e - m) + 2 : raw->len;
    char *from = oam_hdr_get(m, hl, "From"), *subj = oam_hdr_get(m, hl, "Subject"), *date = oam_hdr_get(m, hl, "Date");
    char *dsubj = subj ? oam_hdr_decode(subj) : NULL, *dfrom = from ? oam_hdr_decode(from) : NULL, *l;
    l = latin1(dfrom ? dfrom : "");
    snprintf(from_text, sizeof from_text, "%s", l);
    free(l);
    l = latin1(dsubj ? dsubj : "");
    snprintf(subject_text, sizeof subject_text, "%s", l);
    free(l);
    short_date(date, date_text, sizeof date_text);
    free(from); free(subj); free(date); free(dsubj); free(dfrom);
    if (have_view) oam_view_free(&view);
    oam_view_init(&view);
    have_view = oam_mime_view(m, raw->len, &view);
    show_body();
    if (have_view && view.attachments) {
        char n[16];
        snprintf(n, sizeof n, "%d", view.attachments);
        set_status("Attachments: %s (saving them comes with M5)", n);
    } else set_status("%s", "");
}

/* ---- jobs ------------------------------------------------------------------------- */

static void open_folder(int row)
{
    struct oam_job *j;
    if (row < 0 || row >= nfolders || (folders[row].attrs & OAM_FOLDER_NOSELECT)) return;
    folder_sel = row;
    j = oam_job_new(OAM_JOB_OPEN, replies);
    if (j) snprintf(j->folder, sizeof j->folder, "%s", folders[row].name);
    send(j, "Opening %s...", folder_rows.count > row ? folder_rows.text[row] : "");
}

static void connect_account(void)
{
    struct oam_job *j = oam_job_new(OAM_JOB_CONNECT, replies);
    if (j) j->account = account;
    send(j, "Connecting to %s...", account.imap.host);
}

static void answer(struct oam_job *j)
{
    busy(-1);
    if (!j->ok) {
        set_status("%s", j->error);
        if (j->kind == OAM_JOB_CONNECT) connected = FALSE;
        oam_job_free(j);
        return;
    }
    switch (j->kind) {
        case OAM_JOB_CONNECT: {
            struct oam_job *f = oam_job_new(OAM_JOB_FOLDERS, replies);
            connected = TRUE;
            send(f, "Signed in. Listing folders...", NULL);
            break;
        }
        case OAM_JOB_FOLDERS: {
            int i, inbox = 0;
            if (folders) oam_imap_free_folders(folders, nfolders);
            folders = j->folders;
            nfolders = j->nfolders;
            j->folders = NULL;
            for (i = 0; i < nfolders; i++)
                if (!strcasecmp(folders[i].name, "INBOX")) inbox = i;
            folder_sel = nfolders ? inbox : -1;
            show_folders();
            if (folder_sel >= 0) open_folder(folder_sel);
            break;
        }
        case OAM_JOB_OPEN: {
            char n[32];
            if (msgs) oam_imap_free_msgs(msgs, nmsgs);
            msgs = j->msgs;
            nmsgs = j->nmsgs;
            j->msgs = NULL;
            msg_sel = -1;
            show_messages();
            snprintf(n, sizeof n, "%lu", j->exists);
            set_status("%s messages", n);
            break;
        }
        case OAM_JOB_FETCH:
            if (j->uid == wanted_uid) {                       /* not one the user has moved on from */
                showing_uid = j->uid;
                read_message(&j->message);
            }
            break;
        case OAM_JOB_FLAG:
            set_status("Done", NULL);
            break;
    }
    oam_job_free(j);
}

/* ---- the browser hook ------------------------------------------------------------- */

static void view_in_browser(void)
{
    char path[64], how[200];
    BPTR fh;
    if (!have_view || !view.has_html) return;
    UnLock(CreateDir((STRPTR)"T:OpenMail"));
    snprintf(path, sizeof path, "T:OpenMail/%lu.html", showing_uid);
    if (!(fh = Open((STRPTR)path, MODE_NEWFILE))) { set_status("Could not write %s", path); return; }
    Write(fh, (APTR)"<meta charset=\"utf-8\">\n", 23);
    Write(fh, (APTR)oam_buf_str(&view.html), view.html.len);
    Close(fh);
    oam_browser_open(path, how, sizeof how);
    set_status("%s", how);
}

/* "Links...": a small window of the message's links; picking one opens it. */
static void links_window(void)
{
    struct Window *lw;
    struct Gadget *lg = NULL, *g;
    struct NewGadget ng;
    struct rows r;
    char **t;
    const char *s;
    int n = 0, i, w, done = 0;
    if (!have_view || !view.links.len) return;
    for (s = oam_buf_str(&view.links); *s; s++) n += *s == '\n';
    t = calloc(n ? n : 1, sizeof *t);
    for (s = oam_buf_str(&view.links), i = 0; t && *s && i < n; i++) {
        size_t len = strcspn(s, "\n");
        t[i] = malloc(len + 8);
        if (t[i]) { snprintf(t[i], len + 8, "[%d] ", i + 1); strncat(t[i], s, len); }
        s += len + (s[len] == '\n');
    }
    memset(&r, 0, sizeof r);
    NewList(&r.list);
    rows_set(&r, t, t ? n : 0);
    w = cw * 70 + 30;
    if (!(g = CreateContext(&lg))) { rows_free(&r); return; }
    memset(&ng, 0, sizeof ng);
    ng.ng_TextAttr = &fixed_attr;
    ng.ng_VisualInfo = vi;
    ng.ng_LeftEdge = scr->WBorLeft + PAD;
    ng.ng_TopEdge = scr->WBorTop + scr->Font->ta_YSize + 1 + PAD;
    ng.ng_Width = w;
    ng.ng_Height = line_h * 12;
    ng.ng_GadgetID = 1;
    g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&r.list, TAG_DONE);
    lw = g ? OpenWindowTags(NULL, WA_Title, (ULONG)"Links: pick one to open it", WA_PubScreen, (ULONG)scr,
                            WA_InnerWidth, w + 2 * PAD, WA_InnerHeight, line_h * 12 + 2 * PAD,
                            WA_Left, win->LeftEdge + 40, WA_Top, win->TopEdge + 30,
                            WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                            WA_Gadgets, (ULONG)lg, WA_IDCMP, IDCMP_CLOSEWINDOW | LISTVIEWIDCMP | IDCMP_REFRESHWINDOW,
                            TAG_DONE) : NULL;
    if (lw) {
        GT_RefreshWindow(lw, NULL);
        while (!done) {
            struct IntuiMessage *im;
            WaitPort(lw->UserPort);
            while ((im = GT_GetIMsg(lw->UserPort))) {
                ULONG class = im->Class;
                UWORD code = im->Code;
                GT_ReplyIMsg(im);
                if (class == IDCMP_CLOSEWINDOW) done = 1;
                else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(lw); GT_EndRefresh(lw, TRUE); }
                else if (class == IDCMP_GADGETUP && code < n) {
                    char how[200], url[600];
                    const char *u = strchr(t[code], ' ');
                    snprintf(url, sizeof url, "%s", u ? u + 1 : t[code]);
                    oam_browser_open(url, how, sizeof how);
                    set_status("%s", how);
                }
            }
        }
        CloseWindow(lw);
    }
    FreeGadgets(lg);
    rows_free(&r);
}

/* ---- the window ------------------------------------------------------------------- */

struct Zone { int x, y, w, h; };

static struct Gadget *button(struct Gadget *g, struct NewGadget *ng, int id, const char *label, int x, int y, int w, BOOL off)
{
    ng->ng_LeftEdge = x; ng->ng_TopEdge = y; ng->ng_Width = w; ng->ng_Height = btn_h;
    ng->ng_GadgetText = (UBYTE *)label; ng->ng_GadgetID = id; ng->ng_Flags = PLACETEXT_IN;
    ng->ng_TextAttr = &font_attr;
    g = CreateGadget(BUTTON_KIND, g, ng, GT_Underscore, '_', GA_Disabled, off, TAG_DONE);
    gad[id] = g;
    return g;
}

static struct Gadget *text(struct Gadget *g, struct NewGadget *ng, int id, const char *label, const char *value, int x, int y, int w)
{
    ng->ng_LeftEdge = x; ng->ng_TopEdge = y; ng->ng_Width = w; ng->ng_Height = btn_h;
    ng->ng_GadgetText = (UBYTE *)label; ng->ng_GadgetID = id; ng->ng_Flags = PLACETEXT_LEFT;
    ng->ng_TextAttr = &font_attr;
    g = CreateGadget(TEXT_KIND, g, ng, GTTX_Text, (ULONG)value, GTTX_Border, TRUE, GTTX_CopyText, TRUE, TAG_DONE);
    gad[id] = g;
    return g;
}

static struct Gadget *listview(struct Gadget *g, struct NewGadget *ng, int id, struct rows *r, int x, int y, int w, int h, BOOL ro)
{
    ng->ng_LeftEdge = x; ng->ng_TopEdge = y; ng->ng_Width = w; ng->ng_Height = h;
    ng->ng_GadgetText = NULL; ng->ng_GadgetID = id; ng->ng_Flags = 0;
    ng->ng_TextAttr = &fixed_attr;
    if (ro) g = CreateGadget(LISTVIEW_KIND, g, ng, GTLV_Labels, (ULONG)&r->list, GTLV_ReadOnly, TRUE, TAG_DONE);
    else g = CreateGadget(LISTVIEW_KIND, g, ng, GTLV_Labels, (ULONG)&r->list, GTLV_ShowSelected, 0UL, TAG_DONE);   /* NULL: highlight the row */
    gad[id] = g;
    return g;
}

static int label_w(const char *s)
{
    struct RastPort rp;
    InitRastPort(&rp);
    SetFont(&rp, font);
    return TextLength(&rp, (STRPTR)s, strlen(s));
}

static void make_gadgets(void)
{
    struct NewGadget ng;
    struct Gadget *g;
    struct Zone a;
    int x, y, bw, left_w, right_x, right_w, list_h, body_y, lw, i;
    static const struct { int id; const char *label; BOOL off; } bar[] = {
        { GID_GET, "_Get mail", FALSE }, { GID_WRITE, "_Write", TRUE }, { GID_REPLY, "_Reply", TRUE },
        { GID_REPLYALL, "Reply _all", TRUE }, { GID_FORWARD, "_Forward", TRUE }, { GID_DELETE, "_Delete", FALSE } };

    memset(gad, 0, sizeof gad);
    glist = NULL;
    if (!(g = CreateContext(&glist))) return;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    a.x = win->BorderLeft + MARGIN;
    a.y = win->BorderTop + MARGIN;
    a.w = win->Width - win->BorderLeft - win->BorderRight - 2 * MARGIN;
    a.h = win->Height - win->BorderTop - win->BorderBottom - 2 * MARGIN;

    bw = 0;                                                   /* the toolbar: equal buttons */
    for (i = 0; i < 6; i++) if (label_w(bar[i].label) + 16 > bw) bw = label_w(bar[i].label) + 16;
    for (i = 0, x = a.x; i < 6; i++, x += bw + PAD) g = button(g, &ng, bar[i].id, bar[i].label, x, a.y, bw, bar[i].off);

    y = a.y + btn_h + PAD;
    left_w = a.w / 4 < cw * 14 ? cw * 14 : a.w / 4;
    right_x = a.x + left_w + PAD;
    right_w = a.w - left_w - PAD;
    list_h = (a.h - btn_h - PAD) * 2 / 5;
    g = listview(g, &ng, GID_FOLDERS, &folder_rows, a.x, y, left_w, a.h - 2 * (btn_h + PAD), FALSE);
    g = listview(g, &ng, GID_MESSAGES, &message_rows, right_x, y, right_w, list_h, FALSE);

    lw = label_w("Subject") + 8;
    body_y = y + list_h + PAD;
    g = text(g, &ng, GID_FROM, "From", from_text, right_x + lw, body_y, right_w * 3 / 5 - lw);
    g = text(g, &ng, GID_DATE, "Date", date_text, right_x + right_w * 3 / 5 + label_w("Date") + 12, body_y,
             right_w - right_w * 3 / 5 - label_w("Date") - 12);
    g = text(g, &ng, GID_SUBJECT, "Subject", subject_text, right_x + lw, body_y + btn_h + 2, right_w - lw);
    body_y += 2 * btn_h + 2 + PAD;
    g = listview(g, &ng, GID_BODY, &body_rows, right_x, body_y, right_w, a.y + a.h - btn_h - PAD - body_y, TRUE);

    y = a.y + a.h - btn_h;                                    /* the bottom row */
    g = button(g, &ng, GID_BROWSER, "View in _browser", a.x, y, label_w("View in browser") + 16, !(have_view && view.has_html));
    x = a.x + label_w("View in browser") + 16 + PAD;
    g = button(g, &ng, GID_LINKS, "_Links...", x, y, label_w("Links...") + 16, !(have_view && view.links.len));
    x += label_w("Links...") + 16 + PAD;
    g = text(g, &ng, GID_STATUS, NULL, status, x, y, a.x + a.w - x);
}

static void redo(void)
{
    if (glist) {
        RemoveGList(win, glist, -1);
        FreeGadgets(glist);
        glist = NULL;
    }
    EraseRect(win->RPort, win->BorderLeft, win->BorderTop, win->Width - win->BorderRight - 1, win->Height - win->BorderBottom - 1);
    make_gadgets();
    if (!glist) return;
    AddGList(win, glist, ~0, -1, NULL);
    RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
    if (folder_sel >= 0 && gad[GID_FOLDERS]) GT_SetGadgetAttrs(gad[GID_FOLDERS], win, NULL, GTLV_Selected, folder_sel, TAG_DONE);
    if (msg_sel >= 0 && gad[GID_MESSAGES]) GT_SetGadgetAttrs(gad[GID_MESSAGES], win, NULL, GTLV_Selected, msg_sel, TAG_DONE);
}

static BOOL open_window(void)
{
    struct TextFont *fixed = ((struct GfxBase *)GfxBase)->DefaultFont;
    int w, h;
    if (!(scr = LockPubScreen(NULL))) return FALSE;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return FALSE;
    font_attr = *scr->Font;
    if (!(font = OpenFont(&font_attr))) return FALSE;
    fixed_attr.ta_Name = fixed->tf_Message.mn_Node.ln_Name;   /* the system's fixed-width text font */
    fixed_attr.ta_YSize = fixed->tf_YSize;
    fixed_attr.ta_Style = fixed->tf_Style;
    fixed_attr.ta_Flags = fixed->tf_Flags;
    fh = font->tf_YSize;
    cw = fixed->tf_XSize;
    line_h = fixed->tf_YSize + 1;
    btn_h = fh + 6;
    if ((menu = CreateMenus(menus, TAG_DONE))) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    w = scr->Width * 9 / 10;
    h = scr->Height * 8 / 10;
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenMail", WA_ScreenTitle, (ULONG)VERSION_TEXT,
                         WA_PubScreen, (ULONG)scr, WA_Width, w, WA_Height, h,
                         WA_Left, (scr->Width - w) / 2, WA_Top, (scr->Height - h) / 2,
                         WA_MinWidth, cw * 70, WA_MinHeight, (fh + 6) * 14, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_SizeBBottom, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                                   IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | LISTVIEWIDCMP | BUTTONIDCMP | TEXTIDCMP,
                         TAG_DONE);
    if (!win) return FALSE;
    if (menu) SetMenuStrip(win, menu);
    redo();
    return TRUE;
}

static void close_window(void)
{
    if (win) { ClearMenuStrip(win); CloseWindow(win); win = NULL; }
    if (glist) { FreeGadgets(glist); glist = NULL; }
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static void about(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"OpenMail",
        (UBYTE *)VERSION_TEXT "\n\nMail for the Amiga. MIT licence,\nCopyright (c) 2026 Dalsin Limited.", (UBYTE *)"OK" };
    EasyRequestArgs(win, &es, NULL, NULL);
}

static void pick_message(int row)
{
    struct oam_job *j;
    if (row < 0 || row >= nmsgs) return;
    msg_sel = row;
    wanted_uid = msgs[row].uid;
    j = oam_job_new(OAM_JOB_FETCH, replies);
    if (j) j->uid = wanted_uid;
    send(j, "Fetching the message...", NULL);
    if (!(msgs[row].flags & OAM_F_SEEN)) {                   /* fetching marks it read on the server */
        msgs[row].flags |= OAM_F_SEEN;
        if (message_rows.text && message_rows.text[row]) message_rows.text[row][0] = ' ';
        attach(GID_MESSAGES, &message_rows);
        GT_SetGadgetAttrs(gad[GID_MESSAGES], win, NULL, GTLV_Selected, row, TAG_DONE);
    }
}

static void do_command(int id)
{
    struct oam_job *j;
    switch (id) {
        case GID_GET:
            if (!connected) connect_account();
            else if (folder_sel >= 0) open_folder(folder_sel);
            break;
        case GID_DELETE:
            if (msg_sel < 0) break;
            j = oam_job_new(OAM_JOB_FLAG, replies);
            if (j) { j->uid = msgs[msg_sel].uid; j->flags = OAM_F_DELETED; j->add = 1; }
            msgs[msg_sel].flags |= OAM_F_DELETED;
            if (message_rows.text[msg_sel]) message_rows.text[msg_sel][1] = 'x';
            attach(GID_MESSAGES, &message_rows);
            send(j, "Marking it deleted...", NULL);
            break;
        case GID_BROWSER: view_in_browser(); break;
        case GID_LINKS: links_window(); break;
        default:
            set_status("Writing and replying come in the next version (M4)", NULL);
    }
}

static void window_events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class;
        UWORD code = im->Code;
        struct Gadget *g = (struct Gadget *)im->IAddress;
        GT_ReplyIMsg(im);
        switch (class) {
            case IDCMP_CLOSEWINDOW: quit_now = TRUE; break;
            case IDCMP_NEWSIZE: redo(); show_body(); break;
            case IDCMP_REFRESHWINDOW: GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); break;
            case IDCMP_GADGETUP:
                if (g->GadgetID == GID_FOLDERS) open_folder(code);
                else if (g->GadgetID == GID_MESSAGES) pick_message(code);
                else do_command(g->GadgetID);
                break;
            case IDCMP_VANILLAKEY: {
                static const struct { char key; int id; } keys[] = {
                    { 'g', GID_GET }, { 'd', GID_DELETE }, { 'b', GID_BROWSER }, { 'l', GID_LINKS } };
                unsigned i;
                for (i = 0; i < sizeof keys / sizeof keys[0]; i++)
                    if ((code | 0x20) == keys[i].key) do_command(keys[i].id);
                break;
            }
            case IDCMP_MENUPICK:
                while (code != MENUNULL && win) {
                    struct MenuItem *item = ItemAddress(menu, code);
                    if (!item) break;
                    switch ((ULONG)GTMENUITEM_USERDATA(item)) {
                        case M_ABOUT: about(); break;
                        case M_QUIT: quit_now = TRUE; break;
                        case M_GET: do_command(GID_GET); break;
                        case M_BROWSER: do_command(GID_BROWSER); break;
                        case M_LINKS: do_command(GID_LINKS); break;
                        case M_ACCOUNT: account_menu(); break;
                    }
                    code = item->NextSelect;
                }
                break;
        }
    }
}

/* Project > Account...: change the account, then sign in with it. */
static void account_menu(void)
{
    if (!oam_account_window(scr, vi, &font_attr, &account)) return;
    have_account = TRUE;
    connect_account();
}

static int load_account(char *err, size_t errlen)
{
    static char buf[4096];
    BPTR fh = Open((STRPTR)ACCOUNT_FILE, MODE_OLDFILE);
    LONG n;
    if (!fh) {
        snprintf(err, errlen, "No account yet: choose Account... in the Project menu");
        return -1;
    }
    n = Read(fh, buf, sizeof buf - 1);
    Close(fh);
    buf[n > 0 ? n : 0] = 0;
    return oam_account_parse(buf, &account, err, errlen);
}

static int mail_main(int argc, char **argv)
{
    char err[200];
    (void)argc;
    (void)argv;
    NewList(&folder_rows.list);
    NewList(&message_rows.list);
    NewList(&body_rows.list);
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39))) {
        PutStr((STRPTR)"OpenMail needs AmigaOS 3.0 or later (gadtools.library 39)\n");
        return 20;
    }
    if (!(replies = CreateMsgPort())) { CloseLibrary(GadToolsBase); return 20; }
    if (!open_window()) {
        PutStr((STRPTR)"OpenMail: the window could not open\n");
        close_window();
        DeleteMsgPort(replies);
        CloseLibrary(GadToolsBase);
        return 20;
    }
    if (!oam_worker_start(err, sizeof err)) {
        set_status("No network: %s", err);
    } else {
        int got = load_account(err, sizeof err);
        if (got < 0 && oam_account_window(scr, vi, &font_attr, &account)) got = 1;   /* first run: ask */
        have_account = got > 0;
        if (have_account) connect_account();
        else set_status("%s", err);
    }

    while (!quit_now) {
        ULONG wsig = win ? 1UL << win->UserPort->mp_SigBit : 0, rsig = 1UL << replies->mp_SigBit;
        ULONG got = Wait(wsig | rsig | SIGBREAKF_CTRL_C);
        struct oam_job *j;
        if (got & SIGBREAKF_CTRL_C) quit_now = TRUE;
        while ((j = (struct oam_job *)GetMsg(replies))) answer(j);
        if (got & wsig) window_events();
    }

    oam_worker_stop(replies);
    detach(GID_FOLDERS);
    detach(GID_MESSAGES);
    detach(GID_BODY);
    close_window();
    rows_free(&folder_rows);
    rows_free(&message_rows);
    rows_free(&body_rows);
    if (folders) oam_imap_free_folders(folders, nfolders);
    if (msgs) oam_imap_free_msgs(msgs, nmsgs);
    if (have_view) oam_view_free(&view);
    memset(account.secret, 0, sizeof account.secret);
    DeleteMsgPort(replies);
    CloseLibrary(GadToolsBase);
    return 0;
}

int main(int argc, char **argv)
{
    return oam_run_with_stack(65536, mail_main, argc, argv);
}
