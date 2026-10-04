/*
 * OpenMail's account window (M4's first part): the user's name, address and
 * password; the provider comes from the address (PROGDIR:Providers), so
 * nobody types a server name. Saved to ENVARC:OpenMail/Account and
 * ENV:OpenMail/Account, the password obscured (not encrypted: the Amiga has
 * no key store, and the window says so). The password gadget shows '*': an
 * edit hook keeps the real characters, appends at the end only, and treats
 * any other edit as starting again.
 *
 * MIT, Copyright (c) 2026 Dalsin Limited.
 */
#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/sghooks.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <utility/hooks.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/gadtools.h>

#include <stdio.h>
#include <string.h>

#include "oam_accountwin.h"
#include "oam_provider.h"
#include "oam_buf.h"

enum { G_NAME = 1, G_ADDRESS, G_PASSWORD, G_PROVIDER, G_NOTE1, G_NOTE2, G_SAVE, G_CANCEL, G_COUNT };

static char pw[256];
static int pw_len;

/* The password gadget's edit hook: the real text lives in pw; the gadget
 * shows a '*' for each character, and its cursor stays at the end. */
static ULONG pw_entry(register struct Hook *h __asm("a0"), register struct SGWork *sgw __asm("a2"),
                      register ULONG *msg __asm("a1"))
{
    (void)h;
    if (*msg == SGH_KEY) {
        switch (sgw->EditOp) {
        case EO_INSERTCHAR: case EO_REPLACECHAR:
            if (pw_len < (int)sizeof pw - 1) { pw[pw_len++] = (char)sgw->Code; pw[pw_len] = 0; }
            else sgw->Actions &= ~SGA_USE;
            break;
        case EO_DELBACKWARD:
            if (pw_len) pw[--pw_len] = 0;
            break;
        case EO_ENTER: case EO_NOOP:
            break;
        case EO_MOVECURSOR: case EO_DELFORWARD:
            sgw->Actions &= ~SGA_USE;
            break;
        default:                                   /* clear, undo, big change: start again */
            pw_len = 0; pw[0] = 0; sgw->NumChars = 0;
            break;
        }
    } else if (*msg != SGH_CLICK) {
        return 0;
    }
    for (int i = 0; i < sgw->NumChars; i++) sgw->WorkBuffer[i] = '*';
    sgw->WorkBuffer[sgw->NumChars] = 0;
    sgw->BufferPos = sgw->NumChars;
    return ~0UL;
}
static struct Hook pw_hook = { { NULL, NULL }, (ULONG (*)())(void *)pw_entry, NULL, NULL };

/* The provider whose domains include the address's, from PROGDIR:Providers. */
static int find_provider(const char *address, oam_provider *out)
{
    static char text[4096];
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR dir = Lock((STRPTR)"PROGDIR:Providers", ACCESS_READ), old;
    int found = 0;
    char err[80];
    if (!fib || !dir) { if (dir) UnLock(dir); if (fib) FreeDosObject(DOS_FIB, fib); return 0; }
    old = CurrentDir(dir);
    if (Examine(dir, fib)) {
        while (!found && ExNext(dir, fib)) {
            size_t n = strlen(fib->fib_FileName);
            if (fib->fib_DirEntryType > 0 || n < 9 || strcmp(fib->fib_FileName + n - 9, ".provider")) continue;
            BPTR fh = Open((STRPTR)fib->fib_FileName, MODE_OLDFILE);
            if (!fh) continue;
            LONG got = Read(fh, text, sizeof text - 1);
            Close(fh);
            text[got > 0 ? got : 0] = 0;
            if (oam_provider_parse(text, out, err, sizeof err) && oam_provider_matches(out, address)) found = 1;
        }
    }
    CurrentDir(old);
    UnLock(dir);
    FreeDosObject(DOS_FIB, fib);
    return found;
}

static const char *gadget_text(struct Gadget *g)
{
    return g ? (const char *)((struct StringInfo *)g->SpecialInfo)->Buffer : "";
}

static int write_file(const char *path, const char *text)
{
    BPTR fh = Open((STRPTR)path, MODE_NEWFILE);
    LONG n = (LONG)strlen(text);
    if (!fh) return 0;
    LONG w = Write(fh, (APTR)text, n);
    Close(fh);
    return w == n;
}

static void make_dir(const char *path)
{
    BPTR l = Lock((STRPTR)path, ACCESS_READ);
    if (!l) l = CreateDir((STRPTR)path);
    if (l) UnLock(l);
}

static int save(const oam_account *a)
{
    oam_buf b;
    int ok;
    oam_buf_init(&b);
    if (!oam_account_format(a, &b)) { oam_buf_free(&b); return 0; }
    make_dir("ENVARC:OpenMail");
    make_dir("ENV:OpenMail");
    ok = write_file("ENVARC:OpenMail/Account", oam_buf_str(&b));
    ok = write_file("ENV:OpenMail/Account", oam_buf_str(&b)) && ok;
    memset(b.data ? b.data : (char *)"", 0, b.len);
    oam_buf_free(&b);
    return ok;
}

/* What the window says about the address's provider; 1 when it can be saved. */
static int describe(const char *address, oam_provider *p, int *found, char *line1, char *line2, size_t size)
{
    *found = strchr(address, '@') && find_provider(address, p);
    line2[0] = 0;
    if (!strchr(address, '@')) {
        snprintf(line1, size, "Type your mail address, and OpenMail finds its servers.");
        return 0;
    }
    if (!*found) {
        snprintf(line1, size, "OpenMail doesn't know this provider yet.");
        snprintf(line2, size, "Gmail, Yahoo, iCloud and Fastmail work today.");
        return 0;
    }
    if (!oam_provider_has_auth(p, "password")) {
        snprintf(line1, size, "%s needs Microsoft's own sign-in (OAuth), not a password.", p->name);
        snprintf(line2, size, "That comes once OpenMail is registered. Gmail works today.");
        return 0;
    }
    if (!strcmp(p->name, "Gmail")) {
        snprintf(line1, size, "Use a Google app password (Security, App passwords).");
        snprintf(line2, size, "It is kept obscured on this Amiga, not encrypted.");
    } else {
        snprintf(line1, size, "Some providers want an app password, not your usual one.");
        snprintf(line2, size, "It is kept obscured on this Amiga, not encrypted.");
    }
    return 1;
}

int oam_account_window(struct Screen *scr, APTR vi, struct TextAttr *ta, oam_account *a)
{
    struct TextFont *font = OpenFont(ta);
    struct Gadget *glist = NULL, *g, *gad[G_COUNT];
    struct NewGadget ng;
    struct Window *w = NULL;
    oam_provider prov;
    char line1[100], line2[100], ptext[180];
    int fh, cw, lw, x, y, ww, row, found = 0, can_save, result = 0, done = 0;

    if (!font) return 0;
    fh = font->tf_YSize;
    cw = font->tf_XSize ? font->tf_XSize : 8;
    {
        struct RastPort rp;
        InitRastPort(&rp);
        SetFont(&rp, font);
        lw = TextLength(&rp, (STRPTR)"Password", 8) + 12;
    }
    row = fh + 8;
    {
        static const char *longest[] = {
            "Outlook.com needs Microsoft's own sign-in (OAuth), not a password.",
            "That comes once OpenMail is registered. Gmail works today.",
            "Some providers want an app password, not your usual one.",
            "Type your mail address, and OpenMail finds its servers." };
        struct RastPort rp;
        InitRastPort(&rp);
        SetFont(&rp, font);
        ww = cw * 40;
        for (int i = 0; i < 4; i++) {
            int t = lw + TextLength(&rp, (STRPTR)longest[i], strlen(longest[i])) + 16;
            if (t > ww) ww = t;
        }
        if (ww > scr->Width - scr->WBorLeft - scr->WBorRight - 40) ww = scr->Width - scr->WBorLeft - scr->WBorRight - 40;
    }
    memset(gad, 0, sizeof gad);
    snprintf(pw, sizeof pw, "%s", a->secret);
    pw_len = (int)strlen(pw);
    char stars[256];
    memset(stars, '*', pw_len);
    stars[pw_len] = 0;
    can_save = describe(a->address, &prov, &found, line1, line2, sizeof line1);
    snprintf(ptext, sizeof ptext, "%s", found ? prov.name : "");

    if (!(g = CreateContext(&glist))) { CloseFont(font); return 0; }
    memset(&ng, 0, sizeof ng);
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr = ta;
    x = scr->WBorLeft + 8 + lw;
    y = scr->WBorTop + scr->Font->ta_YSize + 1 + 8;
#define FIELD(id, label, kind, ...) \
    ng.ng_LeftEdge = x; ng.ng_TopEdge = y; ng.ng_Width = ww - lw; ng.ng_Height = fh + 6; \
    ng.ng_GadgetText = (UBYTE *)(label); ng.ng_GadgetID = id; ng.ng_Flags = PLACETEXT_LEFT; \
    gad[id] = g = CreateGadget(kind, g, &ng, __VA_ARGS__, TAG_DONE); y += row;
    FIELD(G_NAME, "Your _name", STRING_KIND, GTST_String, (ULONG)a->name, GTST_MaxChars, sizeof a->name - 1, GT_Underscore, '_')
    FIELD(G_ADDRESS, "_Address", STRING_KIND, GTST_String, (ULONG)a->address, GTST_MaxChars, sizeof a->address - 1, GT_Underscore, '_')
    FIELD(G_PASSWORD, "_Password", STRING_KIND, GTST_String, (ULONG)stars, GTST_MaxChars, sizeof pw - 1, GTST_EditHook, (ULONG)&pw_hook, GT_Underscore, '_')
    FIELD(G_PROVIDER, "Provider", TEXT_KIND, GTTX_Text, (ULONG)ptext, GTTX_Border, TRUE, GTTX_CopyText, TRUE)
    FIELD(G_NOTE1, NULL, TEXT_KIND, GTTX_Text, (ULONG)line1, GTTX_CopyText, TRUE, GTTX_Clipped, TRUE)
    FIELD(G_NOTE2, NULL, TEXT_KIND, GTTX_Text, (ULONG)line2, GTTX_CopyText, TRUE, GTTX_Clipped, TRUE)
#undef FIELD
    y += 4;
    ng.ng_LeftEdge = scr->WBorLeft + 8; ng.ng_TopEdge = y; ng.ng_Width = cw * 12; ng.ng_Height = fh + 6;
    ng.ng_GadgetText = (UBYTE *)"_Save"; ng.ng_GadgetID = G_SAVE; ng.ng_Flags = PLACETEXT_IN;
    gad[G_SAVE] = g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_', GA_Disabled, !can_save, TAG_DONE);
    ng.ng_LeftEdge = scr->WBorLeft + 8 + ww - cw * 12; ng.ng_GadgetText = (UBYTE *)"_Cancel"; ng.ng_GadgetID = G_CANCEL;
    gad[G_CANCEL] = g = CreateGadget(BUTTON_KIND, g, &ng, GT_Underscore, '_', TAG_DONE);
    y += fh + 6 + 8;

    if (g) w = OpenWindowTags(NULL, WA_Title, (ULONG)"OpenMail: your account", WA_PubScreen, (ULONG)scr,
                              WA_InnerWidth, ww + 16, WA_InnerHeight, y - scr->WBorTop - scr->Font->ta_YSize - 1,
                              WA_Left, (scr->Width - ww - 16) / 2, WA_Top, scr->Height / 4,
                              WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                              WA_Gadgets, (ULONG)glist,
                              WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | IDCMP_VANILLAKEY | STRINGIDCMP | BUTTONIDCMP,
                              TAG_DONE);
    if (!w) { FreeGadgets(glist); CloseFont(font); return 0; }
    GT_RefreshWindow(w, NULL);
    if (!a->address[0]) ActivateGadget(gad[G_NAME], w, NULL);

    while (!done) {
        struct IntuiMessage *im;
        WaitPort(w->UserPort);
        while ((im = GT_GetIMsg(w->UserPort))) {
            ULONG class = im->Class;
            UWORD code = im->Code;
            struct Gadget *src = (struct Gadget *)im->IAddress;
            GT_ReplyIMsg(im);
            if (class == IDCMP_CLOSEWINDOW) done = 1;
            else if (class == IDCMP_REFRESHWINDOW) { GT_BeginRefresh(w); GT_EndRefresh(w, TRUE); }
            else if (class == IDCMP_GADGETUP) {
                int id = src->GadgetID;
                if (id == G_NAME) ActivateGadget(gad[G_ADDRESS], w, NULL);
                else if (id == G_ADDRESS || id == G_PASSWORD) {
                    can_save = describe(gadget_text(gad[G_ADDRESS]), &prov, &found, line1, line2, sizeof line1);
                    snprintf(ptext, sizeof ptext, "%s", found ? prov.name : "");
                    GT_SetGadgetAttrs(gad[G_PROVIDER], w, NULL, GTTX_Text, (ULONG)ptext, TAG_DONE);
                    GT_SetGadgetAttrs(gad[G_NOTE1], w, NULL, GTTX_Text, (ULONG)line1, TAG_DONE);
                    GT_SetGadgetAttrs(gad[G_NOTE2], w, NULL, GTTX_Text, (ULONG)line2, TAG_DONE);
                    GT_SetGadgetAttrs(gad[G_SAVE], w, NULL, GA_Disabled, !can_save, TAG_DONE);
                    if (id == G_ADDRESS) ActivateGadget(gad[G_PASSWORD], w, NULL);
                } else if (id == G_SAVE && can_save && pw_len) {
                    oam_account n;
                    memset(&n, 0, sizeof n);
                    snprintf(n.name, sizeof n.name, "%s", gadget_text(gad[G_NAME]));
                    snprintf(n.address, sizeof n.address, "%s", gadget_text(gad[G_ADDRESS]));
                    oam_account_from_provider(&n, &prov);
                    snprintf(n.auth, sizeof n.auth, "password");
                    snprintf(n.secret, sizeof n.secret, "%s", pw);
                    if (save(&n)) { *a = n; result = 1; done = 1; }
                    else GT_SetGadgetAttrs(gad[G_NOTE1], w, NULL, GTTX_Text, (ULONG)"The account could not be saved to ENVARC:OpenMail.", TAG_DONE);
                    memset(&n, 0, sizeof n);
                } else if (id == G_SAVE) {
                    GT_SetGadgetAttrs(gad[G_NOTE1], w, NULL, GTTX_Text, (ULONG)"Type your password first.", TAG_DONE);
                } else if (id == G_CANCEL) done = 1;
            } else if (class == IDCMP_VANILLAKEY && code == 27) done = 1;
        }
    }
    CloseWindow(w);
    FreeGadgets(glist);
    CloseFont(font);
    memset(pw, 0, sizeof pw);
    pw_len = 0;
    return result;
}
