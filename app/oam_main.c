/*
 * OpenMail: the desk (DESIGN.md 2), drawn in the OpenGadTools theme.
 *
 *   [New mail] | [Reply][Reply all][Forward] | [Delete] ... [Get mail]   [search]
 *   folders     | Inbox                        | Subject
 *   (side panel)| Today                        | (SR) Sender <address>   date
 *               |  Sender           09:42      |      [Reply][Reply all][Forward]...
 *               |  Subject                     | ----------------------------------
 *               |  a line of the message       | the message
 *   status
 *
 * The window never talks to a server: the worker (oam_worker.c) does, and
 * the window waits for its answers alongside the user's input. This is
 * M3b: reading on the desk. Writing comes next (M4).
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
#include <dos/var.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

#include <ctype.h>
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

#include "ogt_theme.h"
#include "ogt_draw.h"
#include "ogt_icons.h"
#include "ogt_toolbar.h"
#include "ogt_list.h"

struct Library *GadToolsBase = NULL;      /* ours, not libnix's auto-open stub */

#define VERSION_TEXT "OpenMail 0.3.2 (10.10.2026)"
static const char version[] __attribute__((used)) = "$VER: " VERSION_TEXT;
#define ACCOUNT_FILE "ENV:OpenMail/Account"
#define THEME_ENV "ENV:OpenGadTools/Theme"
#define THEME_ENVARC "ENVARC:OpenGadTools/Theme"

/* the commands, from the toolbars, menus and keys */
enum { C_NEW = 1, C_REPLY, C_REPLYALL, C_FORWARD, C_DELETE, C_ARCHIVE, C_JUNK, C_MOVE, C_FLAG, C_UNREAD, C_GET,
       C_BROWSER, C_LINKS };
enum { GID_SEARCH = 1, GID_FOLDERS = 10, GID_MESSAGES = 11, GID_BODY = 12, GID_TOOLBAR = 100, GID_READBAR = 150 };
enum { M_ABOUT = 1, M_QUIT, M_GET, M_BROWSER, M_LINKS, M_ACCOUNT, M_FLAG, M_UNREAD, M_DELETE,
       M_TH_OPEN, M_TH_OPEN_DARK, M_TH_GRAPHITE, M_TH_GRAPHITE_DARK, M_TH_CLASSIC,
       M_TB_BOTH, M_TB_ICONS, M_TB_TEXT };

static struct NewMenu menus[] = {
    { NM_TITLE, "Project", NULL, 0, 0, NULL },
    { NM_ITEM, "Account...", "A", 0, 0, (APTR)M_ACCOUNT },
    { NM_ITEM, "About...", NULL, 0, 0, (APTR)M_ABOUT },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "Quit", "Q", 0, 0, (APTR)M_QUIT },
    { NM_TITLE, "Mailbox", NULL, 0, 0, NULL },
    { NM_ITEM, "Get mail", "G", 0, 0, (APTR)M_GET },
    { NM_TITLE, "Message", NULL, 0, 0, NULL },
    { NM_ITEM, "Flag", "F", 0, 0, (APTR)M_FLAG },
    { NM_ITEM, "Mark unread", "U", 0, 0, (APTR)M_UNREAD },
    { NM_ITEM, "Delete", NULL, 0, 0, (APTR)M_DELETE },
    { NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL },
    { NM_ITEM, "View in browser", "B", 0, 0, (APTR)M_BROWSER },
    { NM_ITEM, "Links...", "L", 0, 0, (APTR)M_LINKS },
    { NM_TITLE, "Settings", NULL, 0, 0, NULL },
    { NM_ITEM, "Theme", NULL, 0, 0, NULL },
    { NM_SUB, "Open (light)", NULL, CHECKIT, ~1 & 31, (APTR)M_TH_OPEN },
    { NM_SUB, "Open (dark)", NULL, CHECKIT, ~2 & 31, (APTR)M_TH_OPEN_DARK },
    { NM_SUB, "Graphite (light)", NULL, CHECKIT, ~4 & 31, (APTR)M_TH_GRAPHITE },
    { NM_SUB, "Graphite (dark)", NULL, CHECKIT, ~8 & 31, (APTR)M_TH_GRAPHITE_DARK },
    { NM_SUB, "Classic", NULL, CHECKIT, ~16 & 31, (APTR)M_TH_CLASSIC },
    { NM_ITEM, "Toolbar", NULL, 0, 0, NULL },
    { NM_SUB, "Icons and text", NULL, CHECKIT, ~1 & 7, (APTR)M_TB_BOTH },
    { NM_SUB, "Icons only", NULL, CHECKIT | CHECKED, ~2 & 7, (APTR)M_TB_ICONS },
    { NM_SUB, "Text only", NULL, CHECKIT, ~4 & 7, (APTR)M_TB_TEXT },
    { NM_END, NULL, NULL, 0, 0, NULL }
};

static const ogt_tool main_tools[] = {
    { C_NEW, "New mail", OGT_ICON_NEWMAIL, 0, 1, 0 },
    { C_REPLY, "Reply", OGT_ICON_REPLY, 1, 1, 0 },
    { C_REPLYALL, "Reply all", OGT_ICON_REPLYALL, 0, 1, 0 },
    { C_FORWARD, "Forward", OGT_ICON_FORWARD, 0, 1, 0 },
    { C_DELETE, "Delete", OGT_ICON_DELETE, 1, 0, 0 },
    { C_ARCHIVE, "Archive", OGT_ICON_ARCHIVE, 0, 1, 0 },
    { C_JUNK, "Junk", OGT_ICON_JUNK, 0, 1, 0 },
    { C_MOVE, "Move to", OGT_ICON_MOVE, 0, 1, 0 },
    { C_FLAG, "Flag", OGT_ICON_FLAG, 1, 0, 0 },
    { C_UNREAD, "Unread", OGT_ICON_UNREAD, 0, 0, 0 },
    { C_GET, "Get mail", OGT_ICON_GETMAIL, 1, 0, 0 },
};
static const ogt_tool read_tools[] = {
    { C_REPLY, "Reply", OGT_ICON_REPLY, 0, 1, 0 },
    { C_REPLYALL, "Reply all", OGT_ICON_REPLYALL, 0, 1, 0 },
    { C_FORWARD, "Forward", OGT_ICON_FORWARD, 0, 1, 0 },
    { C_BROWSER, "View in browser", OGT_ICON_SEARCH, 1, 1, 0 },
    { C_LINKS, "Links...", OGT_ICON_ATTACH, 0, 1, 0 },
};

/* ---- the state ------------------------------------------------------------------ */

static struct Screen *scr;
static APTR vi;
static struct Window *win;
static struct Menu *menu;
static struct Gadget *glist, *gt_last, *search_gad;
static struct TextFont *font;
static struct TextAttr font_attr;
static int fh;
static struct MsgPort *replies;
static int busy_jobs;
static BOOL quit_now;

static ogt_theme theme;
static ogt_ctx ctx;
static char theme_name[48] = "Open";
static int theme_mode = OGT_LIGHT;
static int tb_style = OGT_TB_ICONS;     /* icons only to start (the Team's rule, 10 October 2026) */
static ogt_toolbar tb, rb;
static ogt_list *folder_list, *msg_list, *body_list;

static oam_account account;
static BOOL have_account, connected;
static void account_menu(void);
static char status[sizeof ((struct oam_job *)0)->error + 64] = "Starting...";   /* a job's whole error and its words */
static char filter[64];

static oam_imap_folder *folders;
static int nfolders, folder_sel = -1;
static int *folder_unread;                 /* per folder, -1 unknown */
static oam_imap_msg *msgs;                 /* newest first */
static char **previews;                    /* per message, once read */
static int nmsgs, msg_sel = -1;
static unsigned long exists_count;
static unsigned long showing_uid, wanted_uid;
static char from_name[120], from_addr[120], subject_text[200], date_text[64], to_text[160], initials[4];
static oam_view view;
static BOOL have_view;

/* what each list shows */
typedef struct frow { int kind, folder, depth, icon; char name[128]; } frow;     /* kind 0 heading, 1 folder; name: an address */      /* kind 0 heading, 1 folder */
typedef struct mrow { int kind, msg; char label[32]; } mrow;                     /* kind 0 heading, 1 message */
static frow *frows;
static int nfrows;
static mrow *mrows;
static int nmrows;
static char **lines;
static int nlines;

/* the layout */
static struct { int x, y, w, h; } area, toolbar_box, panel, list_head, list_box, reading, read_head, body_box, status_box;

/* ---- small helpers ---------------------------------------------------------------- */

static char *latin1(const char *utf8)
{
    char *s = oam_utf8_to_latin1(utf8 ? utf8 : "");
    return s ? s : strdup("");
}

static int contains_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (!n) return 1;
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n)) return 1;
    return 0;
}

static void draw_status(void);

static void set_status(const char *fmt, const char *arg)
{
    snprintf(status, sizeof status, fmt, arg ? arg : "");
    if (win) draw_status();
}

static void busy(int delta)
{
    busy_jobs += delta;
    if (win && ((struct Library *)IntuitionBase)->lib_Version >= 39) {
        if (busy_jobs > 0) SetWindowPointer(win, WA_BusyPointer, TRUE, WA_PointerDelay, TRUE, TAG_DONE);
        else SetWindowPointer(win, TAG_DONE);
    }
    if (win) draw_status();
}

static void send(struct oam_job *j, const char *what, const char *arg)
{
    if (!j) { set_status("Out of memory", NULL); return; }
    set_status(what, arg);
    busy(1);
    oam_worker_put(j);
}

/* The sender's name and address, and a message's subject and date, from its header. */
static void header_bits(const char *h, char *name, size_t nsize, char *addr, size_t asize, char *subj, size_t ssize, long long *when)
{
    size_t hl = strlen(h);
    char *from = oam_hdr_get(h, hl, "From"), *s = oam_hdr_get(h, hl, "Subject"), *date = oam_hdr_get(h, hl, "Date");
    char *n = NULL, *a = NULL, *dn = NULL, *ds = NULL, *l;
    int zone = 0;
    if (from) oam_hdr_address(from, &n, &a);
    dn = n && n[0] ? oam_hdr_decode(n) : NULL;
    ds = s ? oam_hdr_decode(s) : NULL;
    l = latin1(dn ? dn : a ? a : from ? from : "");
    snprintf(name, nsize, "%s", l);
    free(l);
    l = latin1(a ? a : "");
    snprintf(addr, asize, "%s", l);
    free(l);
    l = latin1(ds ? ds : s ? s : "(no subject)");
    snprintf(subj, ssize, "%s", l);
    free(l);
    *when = 0;
    if (date && oam_hdr_date(date, when, &zone)) *when += zone * 60LL - 252460800LL;   /* sender's local time, Amiga seconds */
    else *when = -1;
    free(from); free(s); free(date); free(n); free(a); free(dn); free(ds);
}

static long today_days(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days;
}

static void short_time(long long when, char *out, size_t size)
{
    static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    static const char *wday[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct ClockData cd;
    long days, diff;
    out[0] = 0;
    if (when < 0) return;
    Amiga2Date((ULONG)when, &cd);
    days = (long)(when / 86400);
    diff = today_days() - days;
    if (diff <= 0) snprintf(out, size, "%02u:%02u", (unsigned)cd.hour, (unsigned)cd.min);
    else if (diff < 7) snprintf(out, size, "%s", wday[cd.wday % 7]);
    else snprintf(out, size, "%u %s", (unsigned)cd.mday, mon[(cd.month + 11) % 12]);
}

static const char *group_of(long long when)
{
    long diff = when < 0 ? 9999 : today_days() - (long)(when / 86400);
    if (diff <= 0) return "Today";
    if (diff == 1) return "Yesterday";
    if (diff < 7) return "Earlier this week";
    if (diff < 14) return "Last week";
    return "Older";
}

static int folder_icon(const char *name)
{
    const char *leaf = strrchr(name, '/');
    leaf = leaf ? leaf + 1 : name;
    if (!strcasecmp(leaf, "INBOX")) return OGT_ICON_INBOX;
    if (contains_ci(leaf, "sent")) return OGT_ICON_SENT;
    if (contains_ci(leaf, "draft")) return OGT_ICON_DRAFT;
    if (contains_ci(leaf, "trash") || contains_ci(leaf, "bin") || contains_ci(leaf, "deleted")) return OGT_ICON_TRASH;
    if (contains_ci(leaf, "junk") || contains_ci(leaf, "spam")) return OGT_ICON_JUNK;
    if (contains_ci(leaf, "archive") || contains_ci(leaf, "all mail")) return OGT_ICON_ARCHIVE;
    if (contains_ci(leaf, "starred") || contains_ci(leaf, "flagged") || contains_ci(leaf, "important")) return OGT_ICON_FLAG;
    return OGT_ICON_FOLDER;
}

/* ---- the theme -------------------------------------------------------------------- */

static const char classic_theme[] =
    "name Classic\nversion 1\nfont system\ntitle.align left\npassthrough yes\n[four]\n"
    "pens #aaaaaa #000000 #ffffff #6688bb\ntitle.active.fill 3\ntitle.active.text 1\nfill.text 1\n";

static char *read_file(const char *path)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    char *buf;
    LONG n, size = 32768;
    if (!fh) return NULL;
    if (!(buf = malloc(size))) { Close(fh); return NULL; }
    n = Read(fh, buf, size - 1);
    Close(fh);
    if (n < 0) { free(buf); return NULL; }
    buf[n] = 0;
    return buf;
}

static void load_theme(void)
{
    char path[128], err[120];
    char *text = NULL;
    snprintf(path, sizeof path, "PROGDIR:Themes/%s.theme", theme_name);
    if (!(text = read_file(path))) {
        snprintf(path, sizeof path, "SYS:Prefs/Presets/Themes/%s.theme", theme_name);
        text = read_file(path);
    }
    if (theme.text) ogt_theme_free(&theme);
    if (!text || !ogt_theme_parse(&theme, text, err, sizeof err)) {
        ogt_theme_parse(&theme, classic_theme, err, sizeof err);
        strcpy(theme_name, "Classic");
    }
    free(text);
}

/* ENV:OpenGadTools/Theme: the theme's name, then light or dark. */
static void read_theme_choice(void)
{
    char *s = read_file(THEME_ENV);
    if (s) {
        char *nl = strchr(s, '\n');
        if (nl) *nl = 0;
        if (s[0] && strlen(s) < sizeof theme_name) strcpy(theme_name, s);
        theme_mode = nl && !strncmp(nl + 1, "dark", 4) ? OGT_DARK : OGT_LIGHT;
        free(s);
    }
}

static void save_theme_choice(void)
{
    char line[80];
    const char *paths[] = { THEME_ENV, THEME_ENVARC };
    unsigned i;
    snprintf(line, sizeof line, "%s\n%s\n", theme_name, theme_mode == OGT_DARK ? "dark" : "light");
    for (i = 0; i < 2; i++) {
        BPTR fh;
        UnLock(CreateDir((STRPTR)(i ? "ENVARC:OpenGadTools" : "ENV:OpenGadTools")));
        if ((fh = Open((STRPTR)paths[i], MODE_NEWFILE))) {
            Write(fh, line, strlen(line));
            Close(fh);
        }
    }
}

/* ---- the folder side panel -------------------------------------------------------- */

static void build_folder_rows(void)
{
    int i, n = 0;
    free(frows);
    frows = calloc(nfolders + 2, sizeof *frows);
    if (!frows) { nfrows = 0; return; }
    frows[n].kind = 0;
    snprintf(frows[n].name, sizeof frows[n].name, "%s", account.name[0] ? account.name : account.address[0] ? account.address : "Mail");
    n++;
    for (i = 0; i < nfolders; i++) {
        const char *disp = folders[i].display ? folders[i].display : folders[i].name, *p;
        char *l;
        int depth = 0;
        if (folders[i].attrs & OAM_FOLDER_NOSELECT) continue;
        if (folders[i].delimiter)
            for (p = folders[i].name; *p; p++) depth += *p == folders[i].delimiter;
        p = folders[i].delimiter ? strrchr(disp, folders[i].delimiter) : NULL;
        l = latin1(p ? p + 1 : disp);
        frows[n].kind = 1;
        frows[n].folder = i;
        frows[n].depth = depth > 3 ? 3 : depth;
        frows[n].icon = folder_icon(folders[i].name);
        snprintf(frows[n].name, sizeof frows[n].name, "%s", !strcasecmp(folders[i].name, "INBOX") ? "Inbox" : l);
        free(l);
        n++;
    }
    nfrows = n;
}

static int frow_height(void *u, int i) { (void)u; return frows[i].kind ? fh + 8 : fh + 12; }
static int frow_pickable(void *u, int i) { (void)u; return frows[i].kind == 1; }

static void frow_draw(void *u, int i, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    frow *f = &frows[i];
    (void)u;
    if (!f->kind) {
        ogt_bold(rp, 1);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + 8, y + (h - fh) / 2 + 1, f->name, w - 12);
        ogt_bold(rp, 0);
        return;
    }
    if (selected) {
        ogt_box(rp, ogt_pen(&ctx, "selection.inactive"), x, y, w, h);
        ogt_box(rp, ogt_pen(&ctx, "accent"), x, y, 3, h);
    }
    {
        int ix = x + 10 + f->depth * 14, unread = f->folder < nfolders && folder_unread ? folder_unread[f->folder] : -1;
        int cw = 0;
        char count[12];
        ogt_icon_draw(&ctx, rp, f->icon, ix, y + (h - (fh + 2)) / 2, fh + 2, 0);
        count[0] = 0;
        if (unread > 0) {
            snprintf(count, sizeof count, "%d", unread);
            ogt_bold(rp, f->icon == OGT_ICON_INBOX);
            cw = ogt_text_width(rp, count);
            ogt_text(rp, f->icon == OGT_ICON_INBOX ? ogt_pen(&ctx, "accent") : ogt_pen(&ctx, "muted"), x + w - cw - 8, y + (h - fh) / 2, count, 0);
            ogt_bold(rp, 0);
        }
        ogt_bold(rp, selected);
        ogt_text(rp, ogt_pen(&ctx, "text"), ix + fh + 8, y + (h - fh) / 2, f->name, x + w - (ix + fh + 8) - cw - 14);
        ogt_bold(rp, 0);
    }
}

static int frow_of_folder(int folder)
{
    int i;
    for (i = 0; i < nfrows; i++) if (frows[i].kind && frows[i].folder == folder) return i;
    return -1;
}

static void show_folders(void)
{
    build_folder_rows();
    if (!folder_list) return;
    ogt_list_set_count(folder_list, nfrows);
    folder_list->selected = frow_of_folder(folder_sel);
    ogt_list_draw(folder_list);
}

/* ---- the message list ------------------------------------------------------------- */

static int newest_first(const void *a, const void *b)
{
    const oam_imap_msg *x = a, *y = b;
    return x->uid < y->uid ? 1 : x->uid > y->uid ? -1 : 0;
}

static void build_msg_rows(void)
{
    int i, n = 0;
    const char *last = NULL;
    free(mrows);
    mrows = calloc(2 * nmsgs + 1, sizeof *mrows);
    if (!mrows) { nmrows = 0; return; }
    for (i = 0; i < nmsgs; i++) {
        char name[120], addr[120], subj[200];
        long long when;
        const char *g;
        header_bits(msgs[i].header ? msgs[i].header : "", name, sizeof name, addr, sizeof addr, subj, sizeof subj, &when);
        if (filter[0] && !contains_ci(name, filter) && !contains_ci(subj, filter) && !contains_ci(addr, filter)) continue;
        g = group_of(when);
        if (g != last) {
            mrows[n].kind = 0;
            snprintf(mrows[n].label, sizeof mrows[n].label, "%s", g);
            n++;
            last = g;
        }
        mrows[n].kind = 1;
        mrows[n].msg = i;
        n++;
    }
    nmrows = n;
}

static int mrow_height(void *u, int i) { (void)u; return mrows[i].kind ? 3 * fh + 14 : fh + 8; }
static int mrow_pickable(void *u, int i) { (void)u; return mrows[i].kind == 1; }

static void mrow_draw(void *u, int i, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    mrow *r = &mrows[i];
    oam_imap_msg *m;
    char name[120], addr[120], subj[200], when[24];
    long long t;
    int unread, tx = x + 12, tw;
    (void)u;
    if (!r->kind) {
        ogt_fill(&ctx, rp, "list.header", x, y, w, h);
        ogt_bold(rp, 1);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + 12, y + (h - fh) / 2, r->label, w - 16);
        ogt_bold(rp, 0);
        return;
    }
    m = &msgs[r->msg];
    unread = !(m->flags & OAM_F_SEEN);
    header_bits(m->header ? m->header : "", name, sizeof name, addr, sizeof addr, subj, sizeof subj, &t);
    short_time(t, when, sizeof when);
    if (selected) ogt_box(rp, ogt_pen(&ctx, "selection.inactive"), x, y, w, h);
    ogt_hline(rp, ogt_pen(&ctx, "list.alternate"), x, y + h - 1, w);
    if (unread) ogt_box(rp, ogt_pen(&ctx, "accent"), x + 3, y + 5, 3, h - 10);
    tw = ogt_text_width(rp, when);
    ogt_text(rp, ogt_pen(&ctx, selected ? "label" : "muted"), x + w - tw - 8, y + 5, when, 0);
    {
        int right = x + w - tw - 14;
        if (m->flags & OAM_F_FLAGGED) { right -= fh + 2; ogt_icon_draw(&ctx, rp, OGT_ICON_FLAG, right, y + 5, fh, 0); right -= 4; }
        if (m->flags & OAM_F_DELETED) { ogt_text(rp, ogt_pen(&ctx, "muted"), tx, y + 5 + 2 * (fh + 2), "(deleted)", 0); }
        ogt_bold(rp, unread);
        ogt_text(rp, ogt_pen(&ctx, "text"), tx, y + 5, name, right - tx - 4);
        ogt_text(rp, unread ? ogt_pen(&ctx, "accent") : ogt_pen(&ctx, "text"), tx, y + 5 + fh + 2, subj, x + w - tx - 8);
        ogt_bold(rp, 0);
    }
    if (!(m->flags & OAM_F_DELETED))
        ogt_text(rp, ogt_pen(&ctx, selected ? "label" : "muted"), tx, y + 5 + 2 * (fh + 2),
                 previews && previews[r->msg] ? previews[r->msg] : addr, x + w - tx - 8);
}

static int mrow_of_msg(int msg)
{
    int i;
    for (i = 0; i < nmrows; i++) if (mrows[i].kind && mrows[i].msg == msg) return i;
    return -1;
}

static void draw_list_head(void);

static void show_messages(void)
{
    build_msg_rows();
    if (!msg_list) return;
    ogt_list_set_count(msg_list, nmrows);
    msg_list->selected = mrow_of_msg(msg_sel);
    ogt_list_draw(msg_list);
    draw_list_head();
}

/* ---- the reading pane --------------------------------------------------------------- */

static int lrow_height(void *u, int i) { (void)u; (void)i; return fh + 2; }
static int lrow_pickable(void *u, int i) { (void)u; (void)i; return 0; }

static void lrow_draw(void *u, int i, struct RastPort *rp, int x, int y, int w, int h, int selected)
{
    (void)u; (void)h; (void)selected;
    if (lines && lines[i]) ogt_text(rp, ogt_pen(&ctx, "text"), x + 10, y + 1, lines[i], w - 14);
}

static void free_lines(void)
{
    int i;
    for (i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    lines = NULL;
    nlines = 0;
}

/* The message's text, wrapped to the pane's width in the window's font. */
static void wrap_body(void)
{
    const char *s;
    int cap = 0, width = body_box.w - 34;
    struct TextExtent te;
    free_lines();
    if (!win || width < 40) return;
    for (s = have_view ? oam_buf_str(&view.text) : ""; *s; ) {
        size_t len = strcspn(s, "\n");
        char *line = malloc(len + 1), *l1, *p;
        if (!line) break;
        memcpy(line, s, len);
        line[len] = 0;
        s += len + (s[len] == '\n');
        l1 = latin1(line);
        free(line);
        for (p = l1; ; ) {
            int pl = strlen(p), fit = pl ? TextFit(win->RPort, (STRPTR)p, pl, &te, NULL, 1, width, 32767) : 0, cut = fit;
            if (fit < pl) {
                while (cut > 0 && p[cut] != ' ') cut--;
                if (cut == 0) cut = fit ? fit : 1;
            }
            if (nlines == cap) {
                char **nl = realloc(lines, (cap = cap ? cap * 2 : 64) * sizeof *lines);
                if (!nl) break;
                lines = nl;
            }
            if (!(lines[nlines] = malloc(cut + 1))) break;
            memcpy(lines[nlines], p, cut);
            lines[nlines++][cut] = 0;
            p += cut;
            while (*p == ' ') p++;
            if (!*p) break;
        }
        free(l1);
    }
}

static void draw_reading_head(void)
{
    struct RastPort *rp;
    int x = read_head.x, y = read_head.y, w = read_head.w, av = 2 * fh + 6, tx;
    if (!win) return;
    rp = win->RPort;
    ogt_fill(&ctx, rp, "list", x, y, w, read_head.h);
    if (!showing_uid) {
        ogt_text(rp, ogt_pen(&ctx, "muted"), x + 14, y + 14, nmsgs ? "Pick a message to read it." : "", w - 20);
        return;
    }
    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "text"), x + 14, y + 10, subject_text, w - 24);
    ogt_bold(rp, 0);
    y += fh + 18;
    ogt_fill_circle(rp, ogt_pen(&ctx, "accent"), x + 14 + av / 2, y + av / 2, av / 2);
    ogt_bold(rp, 1);
    ogt_text(rp, ogt_pen(&ctx, "accent.text"), x + 14 + (av - ogt_text_width(rp, initials)) / 2, y + (av - fh) / 2, initials, 0);
    tx = x + 14 + av + 10;
    tx += ogt_text(rp, ogt_pen(&ctx, "text"), tx, y, from_name, w - (tx - x) - 20);
    ogt_bold(rp, 0);
    if (from_addr[0] && strcmp(from_addr, from_name)) {
        char a[130];
        snprintf(a, sizeof a, " <%s>", from_addr);
        ogt_text(rp, ogt_pen(&ctx, "muted"), tx, y, a, x + w - tx - 10);
    }
    ogt_text(rp, ogt_pen(&ctx, "muted"), x + 14 + av + 10, y + fh + 4, to_text[0] ? to_text : date_text, w - av - 40);
    if (to_text[0]) {
        int dw = ogt_text_width(rp, date_text);
        ogt_text(rp, ogt_pen(&ctx, "muted"), x + w - dw - 14, y + fh + 4, date_text, 0);
    }
    if (rb.n) ogt_toolbar_draw(&rb, &ctx, rp, "list");
    if (have_view && view.attachments) {
        char a[64];
        int ay = rb.y + rb.h + 6;
        snprintf(a, sizeof a, "%d attachment%s (saving them comes with M5)", view.attachments, view.attachments == 1 ? "" : "s");
        ogt_icon_draw(&ctx, rp, OGT_ICON_ATTACH, x + 14, ay, fh + 2, 0);
        ogt_text(rp, ogt_pen(&ctx, "label"), x + 14 + fh + 8, ay + 1, a, w - fh - 40);
    }
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), x + 10, read_head.y + read_head.h - 2, w - 20);
}

static void show_body(void)
{
    wrap_body();
    if (body_list) {
        ogt_list_set_count(body_list, nlines);
        body_list->top = 0;
        ogt_list_set_count(body_list, nlines);
        ogt_list_draw(body_list);
    }
    ogt_toolbar_enable(&rb, C_BROWSER, have_view && view.has_html);
    ogt_toolbar_enable(&rb, C_LINKS, have_view && view.links.len);
    draw_reading_head();
}

static void read_message(const oam_buf *raw)
{
    const char *m = oam_buf_str(raw), *e = strstr(m, "\r\n\r\n");
    size_t hl = e ? (size_t)(e - m) + 2 : raw->len;
    char *hdr = malloc(hl + 1), *to = oam_hdr_get(m, hl, "To");
    long long when;
    if (hdr) {
        memcpy(hdr, m, hl);
        hdr[hl] = 0;
        header_bits(hdr, from_name, sizeof from_name, from_addr, sizeof from_addr, subject_text, sizeof subject_text, &when);
        free(hdr);
        if (when >= 0) {
            struct ClockData cd;
            static const char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
            Amiga2Date((ULONG)when, &cd);
            snprintf(date_text, sizeof date_text, "%u %s %u, %02u:%02u", (unsigned)cd.mday, mon[(cd.month + 11) % 12], (unsigned)cd.year, (unsigned)cd.hour, (unsigned)cd.min);
        } else date_text[0] = 0;
    }
    to_text[0] = 0;
    if (to) {
        char *dto = oam_hdr_decode(to), *l = latin1(dto ? dto : to);
        snprintf(to_text, sizeof to_text, "To: %s", l);
        free(l); free(dto); free(to);
    }
    {
        const char *p = from_name;
        int k = 0;
        initials[0] = 0;
        while (*p && k < 2) {
            while (*p && !isalnum((unsigned char)*p)) p++;
            if (*p) { initials[k++] = toupper((unsigned char)*p); }
            while (*p && *p != ' ') p++;
        }
        initials[k] = 0;
    }
    if (have_view) oam_view_free(&view);
    oam_view_init(&view);
    have_view = oam_mime_view(m, raw->len, &view);
    if (have_view && previews && msg_sel >= 0 && msg_sel < nmsgs && !previews[msg_sel]) {
        char *l = latin1(oam_buf_str(&view.text)), *p, *q;
        for (p = q = l; *p && q - l < 140; p++) {               /* one line, spaces collapsed */
            char c = (*p == '\n' || *p == '\r' || *p == '\t') ? ' ' : *p;
            if (c == ' ' && (q == l || q[-1] == ' ')) continue;
            *q++ = c;
        }
        *q = 0;
        previews[msg_sel] = l;
        if (msg_list) ogt_list_draw_row(msg_list, mrow_of_msg(msg_sel));
    }
    show_body();
    set_status("%s", "");
}

/* ---- jobs ------------------------------------------------------------------------- */

static void open_folder(int folder)
{
    struct oam_job *j;
    if (folder < 0 || folder >= nfolders || (folders[folder].attrs & OAM_FOLDER_NOSELECT)) return;
    folder_sel = folder;
    j = oam_job_new(OAM_JOB_OPEN, replies);
    if (j) snprintf(j->folder, sizeof j->folder, "%s", folders[folder].name);
    send(j, "Opening %s...", folders[folder].display ? folders[folder].display : folders[folder].name);
}

static void connect_account(void)
{
    struct oam_job *j = oam_job_new(OAM_JOB_CONNECT, replies);
    if (j) j->account = account;
    send(j, "Connecting to %s...", account.imap.host);
}

static void free_previews(void)
{
    int i;
    if (previews) for (i = 0; i < nmsgs; i++) free(previews[i]);
    free(previews);
    previews = NULL;
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
            free(folder_unread);
            folder_unread = malloc((nfolders ? nfolders : 1) * sizeof *folder_unread);
            for (i = 0; i < nfolders; i++) {
                if (folder_unread) folder_unread[i] = -1;
                if (!strcasecmp(folders[i].name, "INBOX")) inbox = i;
            }
            folder_sel = nfolders ? inbox : -1;
            show_folders();
            if (folder_sel >= 0) open_folder(folder_sel);
            break;
        }
        case OAM_JOB_OPEN: {
            char n[32];
            int i, unread = 0;
            free_previews();
            if (msgs) oam_imap_free_msgs(msgs, nmsgs);
            msgs = j->msgs;
            nmsgs = j->nmsgs;
            j->msgs = NULL;
            previews = calloc(nmsgs ? nmsgs : 1, sizeof *previews);
            exists_count = j->exists;
            qsort(msgs, nmsgs, sizeof *msgs, newest_first);
            for (i = 0; i < nmsgs; i++) unread += !(msgs[i].flags & OAM_F_SEEN);
            if (folder_unread && folder_sel >= 0) folder_unread[folder_sel] = unread;
            msg_sel = -1;
            showing_uid = 0;
            show_messages();
            show_folders();
            show_body();
            snprintf(n, sizeof n, "%lu", j->exists);
            set_status("%s messages", n);
            break;
        }
        case OAM_JOB_FETCH:
            if (j->uid == wanted_uid) {
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

/* "Links...": the message's links in a small window; picking one opens it. */
static void links_window(void)
{
    struct Window *lw;
    struct Gadget *lg = NULL, *g;
    struct NewGadget ng;
    struct List list;
    struct Node *nodes;
    char **t;
    const char *s;
    int n = 0, i, w, done = 0, lh = fh + 1;
    if (!have_view || !view.links.len) return;
    for (s = oam_buf_str(&view.links); *s; s++) n += *s == '\n';
    t = calloc(n ? n : 1, sizeof *t);
    nodes = calloc(n ? n : 1, sizeof *nodes);
    NewList(&list);
    for (s = oam_buf_str(&view.links), i = 0; t && nodes && *s && i < n; i++) {
        size_t len = strcspn(s, "\n");
        t[i] = malloc(len + 8);
        if (t[i]) { snprintf(t[i], len + 8, "[%d] ", i + 1); strncat(t[i], s, len); }
        nodes[i].ln_Name = t[i] ? t[i] : (char *)"";
        AddTail(&list, &nodes[i]);
        s += len + (s[len] == '\n');
    }
    w = scr->Width / 2;
    if ((g = CreateContext(&lg))) {
        memset(&ng, 0, sizeof ng);
        ng.ng_TextAttr = &font_attr;
        ng.ng_VisualInfo = vi;
        ng.ng_LeftEdge = scr->WBorLeft + 4;
        ng.ng_TopEdge = scr->WBorTop + scr->Font->ta_YSize + 1 + 4;
        ng.ng_Width = w;
        ng.ng_Height = lh * 12;
        ng.ng_GadgetID = 1;
        g = CreateGadget(LISTVIEW_KIND, g, &ng, GTLV_Labels, (ULONG)&list, TAG_DONE);
    }
    lw = g ? OpenWindowTags(NULL, WA_Title, (ULONG)"Links: pick one to open it", WA_PubScreen, (ULONG)scr,
                            WA_InnerWidth, w + 8, WA_InnerHeight, lh * 12 + 8,
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
                else if (class == IDCMP_GADGETUP && code < n && t[code]) {
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
    for (i = 0; t && i < n; i++) free(t[i]);
    free(t);
    free(nodes);
}

/* ---- drawing the desk ------------------------------------------------------------- */

static void draw_status(void)
{
    struct RastPort *rp;
    char right[40];
    int rw;
    if (!win || !status_box.w) return;
    rp = win->RPort;
    ogt_fill(&ctx, rp, "window", status_box.x, status_box.y, status_box.w, status_box.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), status_box.x, status_box.y, status_box.w);
    right[0] = 0;
    if (folder_sel >= 0 && folder_unread && folder_unread[folder_sel] >= 0)
        snprintf(right, sizeof right, "%d unread", folder_unread[folder_sel]);
    rw = ogt_text_width(rp, right);
    ogt_text(rp, ogt_pen(&ctx, "label"), status_box.x + 8, status_box.y + (status_box.h - fh) / 2,
             busy_jobs > 0 && !status[0] ? "Working..." : status, status_box.w - rw - 30);
    ogt_text(rp, ogt_pen(&ctx, "label"), status_box.x + status_box.w - rw - 10, status_box.y + (status_box.h - fh) / 2, right, 0);
}

static void draw_list_head(void)
{
    struct RastPort *rp;
    const char *name = "";
    char count[40];
    int tx;
    if (!win || !list_head.w) return;
    rp = win->RPort;
    ogt_fill(&ctx, rp, "list.header", list_head.x, list_head.y, list_head.w, list_head.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), list_head.x, list_head.y + list_head.h - 1, list_head.w);
    if (folder_sel >= 0 && folder_sel < nfolders) {
        int r = frow_of_folder(folder_sel);
        name = r >= 0 ? frows[r].name : folders[folder_sel].name;
    }
    ogt_bold(rp, 1);
    tx = list_head.x + 10;
    tx += ogt_text(rp, ogt_pen(&ctx, "text"), tx, list_head.y + (list_head.h - fh) / 2, name, list_head.w / 2);
    ogt_bold(rp, 0);
    count[0] = 0;
    if (filter[0]) snprintf(count, sizeof count, "  matching \"%.20s\"", filter);
    else if (exists_count) snprintf(count, sizeof count, "  %lu messages", exists_count);
    ogt_text(rp, ogt_pen(&ctx, "muted"), tx, list_head.y + (list_head.h - fh) / 2, count, list_head.x + list_head.w - tx - 6);
}

static void draw_all(void)
{
    struct RastPort *rp;
    if (!win) return;
    rp = win->RPort;
    ogt_fill(&ctx, rp, "window", area.x, area.y, area.w, area.h);
    ogt_fill(&ctx, rp, "window", toolbar_box.x, toolbar_box.y, toolbar_box.w, toolbar_box.h);
    ogt_hline(rp, ogt_pen(&ctx, "group.line"), area.x, toolbar_box.y + toolbar_box.h - 1, area.w);
    ogt_toolbar_draw(&tb, &ctx, rp, "window");
    ogt_fill(&ctx, rp, "list.alternate", panel.x, panel.y, panel.w, panel.h);
    if (folder_list) ogt_list_draw(folder_list);
    draw_list_head();
    if (msg_list) ogt_list_draw(msg_list);
    ogt_fill(&ctx, rp, "list", reading.x, reading.y, reading.w, reading.h);
    draw_reading_head();
    if (body_list) ogt_list_draw(body_list);
    draw_status();
    if (glist) RefreshGList(glist, win, NULL, -1);
    GT_RefreshWindow(win, NULL);
}

/* ---- the layout ------------------------------------------------------------------- */

static void free_gadgets(void)
{
    if (glist) {
        RemoveGList(win, glist, -1);
        if (gt_last) gt_last->NextGadget = NULL;              /* the toolbars' gadgets are ours, not GadTools' */
        FreeGadgets(glist);
        glist = NULL;
        gt_last = NULL;
        search_gad = NULL;
    }
}

static void layout(void)
{
    struct RastPort *rp = win->RPort;
    struct NewGadget ng;
    struct Gadget *g, *chain, *last;
    int sw, bodyw, pw, lw, tbh, sh, rh;
    free_gadgets();
    area.x = win->BorderLeft;
    area.y = win->BorderTop;
    area.w = win->Width - win->BorderLeft - win->BorderRight;
    area.h = win->Height - win->BorderTop - win->BorderBottom;
    SetFont(rp, font);

    ogt_toolbar_set(&tb, main_tools, sizeof main_tools / sizeof main_tools[0], tb_style);
    if (msg_sel >= 0) {
        tb.tool[4].disabled = 0;
        tb.tool[8].disabled = 0;
        tb.tool[9].disabled = 0;
    } else {
        tb.tool[4].disabled = 1;
        tb.tool[8].disabled = 1;
        tb.tool[9].disabled = 1;
    }
    sw = area.w / 5 < 120 ? 120 : area.w / 5 > 260 ? 260 : area.w / 5;
    tbh = ogt_toolbar_layout(&tb, rp, area.x + 6, area.y + 4, area.w - sw - 24);
    toolbar_box.x = area.x; toolbar_box.y = area.y; toolbar_box.w = area.w; toolbar_box.h = tbh + 9;

    sh = fh + 8;
    status_box.x = area.x; status_box.w = area.w; status_box.h = sh; status_box.y = area.y + area.h - sh;
    bodyw = area.w;
    pw = bodyw / 5 < 150 ? 150 : bodyw / 5 > 240 ? 240 : bodyw / 5;
    lw = (bodyw - pw) * 2 / 5 < 220 ? 220 : (bodyw - pw) * 2 / 5;
    panel.x = area.x; panel.y = toolbar_box.y + toolbar_box.h; panel.w = pw; panel.h = status_box.y - panel.y;
    list_head.x = panel.x + pw; list_head.y = panel.y; list_head.w = lw; list_head.h = fh + 12;
    list_box.x = list_head.x; list_box.y = list_head.y + list_head.h; list_box.w = lw; list_box.h = status_box.y - list_box.y;
    reading.x = list_box.x + lw; reading.y = panel.y; reading.w = area.x + area.w - reading.x; reading.h = panel.h;

    ogt_toolbar_set(&rb, read_tools, sizeof read_tools / sizeof read_tools[0], OGT_TB_INLINE);
    rb.tool[3].disabled = !(have_view && view.has_html);
    rb.tool[4].disabled = !(have_view && view.links.len);
    rh = ogt_toolbar_layout(&rb, rp, reading.x + 14, reading.y + fh + 18 + 2 * fh + 12, reading.w - 28);
    read_head.x = reading.x; read_head.y = reading.y; read_head.w = reading.w;
    read_head.h = (rb.y + rh) - reading.y + 10 + fh + 10;
    body_box.x = reading.x + 6; body_box.y = read_head.y + read_head.h; body_box.w = reading.w - 10; body_box.h = reading.y + reading.h - body_box.y - 4;

    /* GadTools: the search field */
    if (!(g = CreateContext(&glist))) return;
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = &font_attr;
    ng.ng_LeftEdge = area.x + area.w - sw - 10;
    ng.ng_TopEdge = area.y + 4 + (tbh - (fh + 6)) / 2;
    ng.ng_Width = sw;
    ng.ng_Height = fh + 6;
    ng.ng_GadgetID = GID_SEARCH;
    g = CreateGadget(STRING_KIND, g, &ng, GTST_String, (ULONG)filter, GTST_MaxChars, sizeof filter - 1, TAG_DONE);
    search_gad = g;
    gt_last = g;
    if (!g) return;
    /* the toolbars' buttons, after GadTools' own */
    chain = ogt_toolbar_gadgets(&tb, GID_TOOLBAR);
    last = gt_last;
    if (chain) {
        last->NextGadget = chain;
        for (last = chain; last->NextGadget; last = last->NextGadget) ;
    }
    chain = ogt_toolbar_gadgets(&rb, GID_READBAR);
    if (chain) last->NextGadget = chain;
    AddGList(win, glist, ~0, -1, NULL);

    if (!folder_list) folder_list = ogt_list_new(win, &ctx, GID_FOLDERS, frow_height, frow_draw, frow_pickable, NULL, "list.alternate");
    if (!msg_list) msg_list = ogt_list_new(win, &ctx, GID_MESSAGES, mrow_height, mrow_draw, mrow_pickable, NULL, "list");
    if (!body_list) body_list = ogt_list_new(win, &ctx, GID_BODY, lrow_height, lrow_draw, lrow_pickable, NULL, "list");
    if (folder_list) { ogt_list_layout(folder_list, panel.x + 2, panel.y + 4, panel.w - 6, panel.h - 8); ogt_list_set_count(folder_list, nfrows); }
    if (msg_list) { ogt_list_layout(msg_list, list_box.x, list_box.y, list_box.w, list_box.h); ogt_list_set_count(msg_list, nmrows); }
    wrap_body();
    if (body_list) { ogt_list_layout(body_list, body_box.x, body_box.y, body_box.w, body_box.h); ogt_list_set_count(body_list, nlines); }
}

static void relayout(void)
{
    layout();
    draw_all();
}

static void apply_theme(void)
{
    ogt_ctx_free(&ctx);
    load_theme();
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    if (win) relayout();
}

/* The part of the screen a full-size window may have: below the title bar,
 * less the strip OpenDock takes along an edge (OpenFiles' free_area(), copied
 * here). OpenDock's window is the one whose screen title starts "OpenDock";
 * an ENV:OpenDock/Free of "left top width height" wins when the dock
 * publishes one. Without a dock it is the screen less its title bar. */
static void free_area(int *l, int *t, int *w, int *h)
{
    char buf[48];
    struct Window *dw;
    ULONG lock;
    LONG got;
    int top = scr->BarHeight + 1, bottom = scr->Height, left = 0, right = scr->Width, a, b, c, d;
    got = GetVar((STRPTR)"OpenDock/Free", (STRPTR)buf, sizeof buf, GVF_GLOBAL_ONLY);
    if (got > 0 && sscanf(buf, "%d %d %d %d", &a, &b, &c, &d) == 4 && c >= 400 && d >= 200 && a >= 0 && b >= 0 &&
        a + c <= scr->Width && b + d <= scr->Height) {
        *l = a;
        *t = b < top ? top : b;
        *w = c;
        *h = b + d - *t;
        return;
    }
    lock = LockIBase(0);
    for (dw = scr->FirstWindow; dw; dw = dw->NextWindow) {
        if (!dw->ScreenTitle || strncmp((const char *)dw->ScreenTitle, "OpenDock", 8) != 0)
            continue;
        if (dw->Width >= dw->Height) {              /* along the top or the bottom */
            if (dw->TopEdge + dw->Height / 2 > scr->Height / 2) {
                if (dw->TopEdge < bottom)
                    bottom = dw->TopEdge;
            } else if (dw->TopEdge + dw->Height > top)
                top = dw->TopEdge + dw->Height;
        } else {                                    /* down the left or the right */
            if (dw->LeftEdge + dw->Width / 2 > scr->Width / 2) {
                if (dw->LeftEdge < right)
                    right = dw->LeftEdge;
            } else if (dw->LeftEdge + dw->Width > left)
                left = dw->LeftEdge + dw->Width;
        }
    }
    UnlockIBase(lock);
    if (right - left < 400 || bottom - top < 200) { /* a dock that big: use the whole screen */
        left = 0;
        right = scr->Width;
        top = scr->BarHeight + 1;
        bottom = scr->Height;
    }
    *l = left;
    *t = top;
    *w = right - left;
    *h = bottom - top;
}

/* The first size (the user, 10 October 2026, as in OpenFiles 0.2.3): 800 x
 * 600, centred in the free area, and never bigger than it, so on a screen
 * smaller than 800 x 600 it is the free area itself. A size the user gives
 * the window is kept and given back by OpenWindows. */
#define START_W 800
#define START_H 600
static void start_box(int *l, int *t, int *w, int *h)
{
    int al, at, aw, ah;
    free_area(&al, &at, &aw, &ah);
    *w = aw < START_W ? aw : START_W;
    *h = ah < START_H ? ah : START_H;
    *l = al + (aw - *w) / 2;
    *t = at + (ah - *h) / 2;
}

static BOOL open_window(void)
{
    int l, t, w, h, mh;
    if (!(scr = LockPubScreen(NULL))) return FALSE;
    if (!(vi = GetVisualInfo(scr, TAG_DONE))) return FALSE;
    font_attr = *scr->Font;
    if (!(font = OpenFont(&font_attr))) return FALSE;
    fh = font->tf_YSize;
    load_theme();
    ogt_ctx_init(&ctx, scr, &theme, theme_mode);
    if ((menu = CreateMenus(menus, TAG_DONE))) LayoutMenus(menu, vi, GTMN_NewLookMenus, TRUE, TAG_DONE);
    start_box(&l, &t, &w, &h);     /* 800 x 600 in the free area, or the free area when smaller */
    mh = (fh + 6) * 18;
    win = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenMail", WA_ScreenTitle, (ULONG)VERSION_TEXT,
                         WA_PubScreen, (ULONG)scr, WA_Width, w, WA_Height, h,
                         WA_Left, l, WA_Top, t,
                         WA_MinWidth, w < 560 ? w : 560, WA_MinHeight, h < mh ? h : mh, WA_MaxWidth, ~0, WA_MaxHeight, ~0,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_SizeBBottom, TRUE, WA_Activate, TRUE, WA_SmartRefresh, TRUE, WA_NewLookMenus, TRUE,
                         WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_GADGETDOWN | IDCMP_MENUPICK | IDCMP_NEWSIZE |
                                   IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                                   IDCMP_IDCMPUPDATE | STRINGIDCMP,
                         TAG_DONE);
    if (!win) return FALSE;
    if (menu) {
        /* tick the theme and toolbar style in use */
        struct MenuItem *item;
        UWORD num;
        int which = !strcmp(theme_name, "Classic") ? 4 : (!strcmp(theme_name, "Graphite") ? 2 : 0) + (theme_mode == OGT_DARK);
        num = FULLMENUNUM(3, 0, which);
        if ((item = ItemAddress(menu, num))) item->Flags |= CHECKED;
        SetMenuStrip(win, menu);
    }
    relayout();
    return TRUE;
}

static void close_window(void)
{
    if (win) {
        ClearMenuStrip(win);
        free_gadgets();
        ogt_list_free(folder_list); folder_list = NULL;
        ogt_list_free(msg_list); msg_list = NULL;
        ogt_list_free(body_list); body_list = NULL;
        CloseWindow(win);
        win = NULL;
    }
    ogt_ctx_free(&ctx);
    if (menu) { FreeMenus(menu); menu = NULL; }
    if (font) { CloseFont(font); font = NULL; }
    if (vi) { FreeVisualInfo(vi); vi = NULL; }
    if (scr) { UnlockPubScreen(NULL, scr); scr = NULL; }
}

static void about(void)
{
    struct EasyStruct es = { sizeof(struct EasyStruct), 0, (UBYTE *)"OpenMail",
        (UBYTE *)VERSION_TEXT "\n\nMail for the Amiga, drawn with OpenGadTools.\nMIT licence, Copyright (c) 2026 Dalsin Limited.", (UBYTE *)"OK" };
    EasyRequestArgs(win, &es, NULL, NULL);
}

/* ---- commands --------------------------------------------------------------------- */

static void pick_message(int row)
{
    struct oam_job *j;
    int m;
    if (row < 0 || row >= nmrows || !mrows[row].kind) return;
    m = mrows[row].msg;
    if (msg_sel < 0) { msg_sel = m; relayout(); }                 /* Delete, Flag and Unread come alive */
    msg_sel = m;
    wanted_uid = msgs[m].uid;
    j = oam_job_new(OAM_JOB_FETCH, replies);
    if (j) j->uid = wanted_uid;
    send(j, "Fetching the message...", NULL);
    if (!(msgs[m].flags & OAM_F_SEEN)) {                       /* fetching marks it read on the server */
        msgs[m].flags |= OAM_F_SEEN;
        if (folder_unread && folder_sel >= 0 && folder_unread[folder_sel] > 0) {
            folder_unread[folder_sel]--;
            if (folder_list) ogt_list_draw_row(folder_list, frow_of_folder(folder_sel));
            draw_status();
        }
    }
    if (msg_list) ogt_list_select(msg_list, row);
}

static void flag_job(unsigned flag, int add, const char *what)
{
    struct oam_job *j;
    if (msg_sel < 0) return;
    j = oam_job_new(OAM_JOB_FLAG, replies);
    if (j) { j->uid = msgs[msg_sel].uid; j->flags = flag; j->add = add; }
    if (add) msgs[msg_sel].flags |= flag;
    else msgs[msg_sel].flags &= ~flag;
    if (msg_list) ogt_list_draw_row(msg_list, mrow_of_msg(msg_sel));
    send(j, what, NULL);
}

static void do_command(int id)
{
    switch (id) {
        case C_GET:
            if (!connected) connect_account();
            else if (folder_sel >= 0) open_folder(folder_sel);
            break;
        case C_DELETE: flag_job(OAM_F_DELETED, 1, "Marking it deleted..."); break;
        case C_FLAG:
            if (msg_sel >= 0) flag_job(OAM_F_FLAGGED, !(msgs[msg_sel].flags & OAM_F_FLAGGED), "Changing the flag...");
            break;
        case C_UNREAD:
            if (msg_sel >= 0) {
                flag_job(OAM_F_SEEN, 0, "Marking it unread...");
                if (folder_unread && folder_sel >= 0) { folder_unread[folder_sel]++; if (folder_list) ogt_list_draw_row(folder_list, frow_of_folder(folder_sel)); }
            }
            break;
        case C_BROWSER: view_in_browser(); break;
        case C_LINKS: links_window(); break;
        default:
            set_status("Writing, replying and moving come in the next version (M4)", NULL);
    }
}

static void set_theme(const char *name, int mode)
{
    snprintf(theme_name, sizeof theme_name, "%s", name);
    theme_mode = mode;
    save_theme_choice();
    apply_theme();
}

static void toolbar_event(ogt_toolbar *t, ULONG class, struct Gadget *g)
{
    int i = ogt_toolbar_index(t, g->GadgetID);
    if (i < 0) return;
    if (class == IDCMP_GADGETDOWN) {
        t->pressed = i;
        ogt_toolbar_draw_one(t, &ctx, win->RPort, i, 1, t == &tb ? "window" : "list");
    } else {
        t->pressed = -1;
        ogt_toolbar_draw_one(t, &ctx, win->RPort, i, 0, t == &tb ? "window" : "list");
        if (!t->tool[i].disabled) do_command(t->tool[i].id);
    }
}

static void window_events(void)
{
    struct IntuiMessage *im;
    while (win && (im = GT_GetIMsg(win->UserPort))) {
        ULONG class = im->Class, secs = im->Seconds, micros = im->Micros;
        UWORD code = im->Code;
        APTR ia = im->IAddress;
        int mx = im->MouseX, my = im->MouseY, row = -1, r;
        struct Gadget *g = (struct Gadget *)ia;
        GT_ReplyIMsg(im);
        switch (class) {
            case IDCMP_CLOSEWINDOW: quit_now = TRUE; break;
            case IDCMP_NEWSIZE: relayout(); break;
            case IDCMP_REFRESHWINDOW: GT_BeginRefresh(win); GT_EndRefresh(win, TRUE); break;
            case IDCMP_GADGETDOWN:
            case IDCMP_GADGETUP:
                if (g->GadgetID >= GID_READBAR) toolbar_event(&rb, class, g);
                else if (g->GadgetID >= GID_TOOLBAR) toolbar_event(&tb, class, g);
                else if (g->GadgetID == GID_SEARCH && class == IDCMP_GADGETUP) {
                    snprintf(filter, sizeof filter, "%s", ((struct StringInfo *)g->SpecialInfo)->Buffer);
                    show_messages();
                } else {
                    if (folder_list) ogt_list_event(folder_list, class, code, ia, mx, my, secs, micros, &row);
                    if (msg_list) ogt_list_event(msg_list, class, code, ia, mx, my, secs, micros, &row);
                    if (body_list) ogt_list_event(body_list, class, code, ia, mx, my, secs, micros, &row);
                }
                break;
            case IDCMP_IDCMPUPDATE:
                if (folder_list) ogt_list_event(folder_list, class, code, ia, mx, my, secs, micros, &row);
                if (msg_list) ogt_list_event(msg_list, class, code, ia, mx, my, secs, micros, &row);
                if (body_list) ogt_list_event(body_list, class, code, ia, mx, my, secs, micros, &row);
                break;
            case IDCMP_MOUSEBUTTONS:
                if (code == SELECTUP) {
                    if (tb.pressed >= 0) { ogt_toolbar_draw_one(&tb, &ctx, win->RPort, tb.pressed, 0, "window"); tb.pressed = -1; }
                    if (rb.pressed >= 0) { ogt_toolbar_draw_one(&rb, &ctx, win->RPort, rb.pressed, 0, "list"); rb.pressed = -1; }
                    break;
                }
                if (folder_list && (r = ogt_list_event(folder_list, class, code, ia, mx, my, secs, micros, &row)) >= OGT_LIST_PICKED && r <= OGT_LIST_OPENED) {
                    if (row >= 0 && frows[row].kind) open_folder(frows[row].folder);
                    draw_list_head();
                } else if (msg_list && (r = ogt_list_event(msg_list, class, code, ia, mx, my, secs, micros, &row)) == OGT_LIST_PICKED) {
                    pick_message(row);
                }
                break;
            case IDCMP_RAWKEY:
                if (msg_list && (code == 0x4C || code == 0x4D)) {
                    int i = ogt_list_step(msg_list, code == 0x4C ? -1 : 1);
                    if (i >= 0) pick_message(i);
                }
                break;
            case IDCMP_VANILLAKEY: {
                static const struct { char key; int id; } keys[] = {
                    { 'g', C_GET }, { 'b', C_BROWSER }, { 'l', C_LINKS }, { 'f', C_FLAG }, { 'u', C_UNREAD } };
                unsigned i;
                if (code == 127) { do_command(C_DELETE); break; }   /* Del */
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
                        case M_GET: do_command(C_GET); break;
                        case M_BROWSER: do_command(C_BROWSER); break;
                        case M_LINKS: do_command(C_LINKS); break;
                        case M_FLAG: do_command(C_FLAG); break;
                        case M_UNREAD: do_command(C_UNREAD); break;
                        case M_DELETE: do_command(C_DELETE); break;
                        case M_ACCOUNT: account_menu(); break;
                        case M_TH_OPEN: set_theme("Open", OGT_LIGHT); break;
                        case M_TH_OPEN_DARK: set_theme("Open", OGT_DARK); break;
                        case M_TH_GRAPHITE: set_theme("Graphite", OGT_LIGHT); break;
                        case M_TH_GRAPHITE_DARK: set_theme("Graphite", OGT_DARK); break;
                        case M_TH_CLASSIC: set_theme("Classic", OGT_LIGHT); break;
                        case M_TB_BOTH: tb_style = OGT_TB_ICONS_TEXT; relayout(); break;
                        case M_TB_ICONS: tb_style = OGT_TB_ICONS; relayout(); break;
                        case M_TB_TEXT: tb_style = OGT_TB_TEXT; relayout(); break;
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
    show_folders();
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
    if (!(GadToolsBase = OpenLibrary((STRPTR)"gadtools.library", 39))) {
        PutStr((STRPTR)"OpenMail needs AmigaOS 3.0 or later (gadtools.library 39)\n");
        return 20;
    }
    if (!(replies = CreateMsgPort())) { CloseLibrary(GadToolsBase); return 20; }
    read_theme_choice();
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
        show_folders();
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
    close_window();
    free_lines();
    free(frows);
    free(mrows);
    free(folder_unread);
    free_previews();
    if (folders) oam_imap_free_folders(folders, nfolders);
    if (msgs) oam_imap_free_msgs(msgs, nmsgs);
    if (have_view) oam_view_free(&view);
    if (theme.text) ogt_theme_free(&theme);
    memset(account.secret, 0, sizeof account.secret);
    DeleteMsgPort(replies);
    CloseLibrary(GadToolsBase);
    return 0;
}

int main(int argc, char **argv)
{
    return oam_run_with_stack(65536, mail_main, argc, argv);
}
