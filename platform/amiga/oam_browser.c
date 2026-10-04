#include "oam_browser.h"

#include <exec/types.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>

#include <stdio.h>
#include <string.h>

struct RxsLib *RexxSysBase;

static int is_url(const char *what)
{
    return strstr(what, "://") != NULL || !strncmp(what, "mailto:", 7);
}

/* An AmigaDOS path made absolute ("T:x" -> "Ram Disk:T/x"), or a copy. */
static void absolute(const char *path, char *out, size_t size)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock || !NameFromLock(lock, (STRPTR)out, size)) snprintf(out, size, "%s", path);
    if (lock) UnLock(lock);
}

/* 1. The user's own command. */
static int by_command(const char *what, char *how, size_t howlen)
{
    char tmpl[256], cmd[1024], *pct;
    BPTR nil;
    if (GetVar((STRPTR)"OpenAmigaMail/Browser", (STRPTR)tmpl, sizeof tmpl, GVF_GLOBAL_ONLY) <= 0) return 0;
    if ((pct = strstr(tmpl, "%s"))) {
        *pct = 0;
        snprintf(cmd, sizeof cmd, "%s\"%s\"%s", tmpl, what, pct + 2);
    } else {
        snprintf(cmd, sizeof cmd, "%s \"%s\"", tmpl, what);
    }
    nil = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (SystemTags((STRPTR)cmd, SYS_Asynch, TRUE, SYS_Input, (ULONG)nil, SYS_Output, 0, TAG_DONE) == -1) {
        if (nil) Close(nil);
        snprintf(how, howlen, "ENV:OpenAmigaMail/Browser did not start: %s", tmpl);
        return 0;
    }
    snprintf(how, howlen, "opened with your browser command");
    return 1;
}

/* 2. AmigaChrome's browser, by ARexx, waiting up to 5 s for its answer. */
static int by_arexx(const char *what, char *how, size_t howlen)
{
    struct MsgPort *reply = NULL, *tport = NULL, *port;
    struct timerequest *tr = NULL;
    struct RexxMsg *rm = NULL;
    char line[600];
    int sent = 0, ok = 0;

    if (!FindPort((STRPTR)OAM_BROWSER_PORT)) return 0;                 /* not running: no need to try */
    if (!(RexxSysBase = (struct RxsLib *)OpenLibrary((STRPTR)"rexxsyslib.library", 36))) return 0;
    if (is_url(what)) snprintf(line, sizeof line, "OPENURL \"%s\"", what);
    else {
        char abs[512];
        absolute(what, abs, sizeof abs);
        snprintf(line, sizeof line, "OPENFILE \"%s\"", abs);
    }
    if ((reply = CreateMsgPort()) && (rm = CreateRexxMsg(reply, NULL, NULL)) &&
        (rm->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)line, strlen(line)))) {
        rm->rm_Action = RXCOMM;
        Forbid();
        if ((port = FindPort((STRPTR)OAM_BROWSER_PORT))) { PutMsg(port, &rm->rm_Node); sent = 1; }
        Permit();
    }
    if (sent) {
        ULONG tsig = 0;
        if ((tport = CreateMsgPort()) && (tr = (struct timerequest *)CreateIORequest(tport, sizeof(*tr))) &&
            !OpenDevice((STRPTR)TIMERNAME, UNIT_VBLANK, (struct IORequest *)tr, 0)) {
            tr->tr_node.io_Command = TR_ADDREQUEST;
            tr->tr_time.tv_secs = 5;
            tr->tr_time.tv_micro = 0;
            SendIO((struct IORequest *)tr);
            tsig = 1UL << tport->mp_SigBit;
        }
        for (;;) {
            ULONG got = Wait((1UL << reply->mp_SigBit) | tsig);
            if (GetMsg(reply)) { ok = rm->rm_Result1 == 0; break; }
            if (got & tsig) { sent = 2; break; }                         /* no answer: the message stays theirs */
        }
        if (tsig) {
            if (!CheckIO((struct IORequest *)tr)) AbortIO((struct IORequest *)tr);
            WaitIO((struct IORequest *)tr);
            CloseDevice((struct IORequest *)tr);
        }
    }
    if (rm && sent != 2) {
        if (rm->rm_Args[0]) DeleteArgstring((UBYTE *)rm->rm_Args[0]);
        DeleteRexxMsg(rm);
    }
    if (tr) DeleteIORequest((struct IORequest *)tr);
    if (tport) DeleteMsgPort(tport);
    if (reply && sent != 2) DeleteMsgPort(reply);
    CloseLibrary((struct Library *)RexxSysBase);
    RexxSysBase = NULL;
    if (ok) snprintf(how, howlen, "opened in AmigaChrome's browser");
    else if (sent == 2) snprintf(how, howlen, "AmigaChrome's browser did not answer");
    else if (sent) snprintf(how, howlen, "AmigaChrome's browser could not open it");
    return ok;
}

/* 3. OpenURL: URL_OpenA(url, tags), its first call (-30). */
static int by_openurl(const char *what, char *how, size_t howlen)
{
    struct Library *base = OpenLibrary((STRPTR)"openurl.library", 1);
    char url[600];
    ULONG ok;
    if (!base) return 0;
    if (is_url(what)) snprintf(url, sizeof url, "%s", what);
    else {
        char abs[512];
        absolute(what, abs, sizeof abs);
        snprintf(url, sizeof url, "file://localhost/%s", abs);
    }
    {
        register struct Library *a6 __asm("a6") = base;
        register const char *a0 __asm("a0") = url;
        register APTR a1 __asm("a1") = NULL;
        register ULONG d0 __asm("d0");
        __asm volatile ("jsr -30(a6)" : "=r"(d0) : "r"(a6), "r"(a0), "r"(a1) : "d1", "cc", "memory");
        ok = d0;
    }
    CloseLibrary(base);
    if (ok) snprintf(how, howlen, "opened through OpenURL");
    else snprintf(how, howlen, "OpenURL could not open it");
    return ok != 0;
}

int oam_browser_open(const char *what, char *how, size_t howlen)
{
    if (howlen) how[0] = 0;
    if (by_command(what, how, howlen) || by_arexx(what, how, howlen) || by_openurl(what, how, howlen)) return 1;
    if (!how[0])
        snprintf(how, howlen, "No browser found: install one with OpenURL, or set ENV:OpenAmigaMail/Browser");
    return 0;
}
