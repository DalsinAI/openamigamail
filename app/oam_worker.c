#include "oam_worker.h"
#include "oam_net.h"

#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWEST 300                  /* summaries fetched when a folder opens */

static struct MsgPort *volatile worker_port;
static struct Task *parent;
static BYTE ready_sig = -1;
static char start_err[200];

struct oam_job *oam_job_new(int kind, struct MsgPort *reply)
{
    struct oam_job *j = AllocVec(sizeof *j, MEMF_ANY | MEMF_CLEAR);
    if (!j) return NULL;
    j->msg.mn_ReplyPort = reply;
    j->msg.mn_Length = sizeof *j;
    j->kind = kind;
    oam_buf_init(&j->message);
    return j;
}

void oam_job_free(struct oam_job *j)
{
    if (!j) return;
    if (j->folders) oam_imap_free_folders(j->folders, j->nfolders);
    if (j->msgs) oam_imap_free_msgs(j->msgs, j->nmsgs);
    oam_buf_free(&j->message);
    memset(j->account.secret, 0, sizeof j->account.secret);
    FreeVec(j);
}

/* ---- the worker's side -------------------------------------------------------- */

static oam_imap imap;
static int connected;

/* The job's error: a server's text can be any length, so a long one is cut
 * to fit and ends in "..." to show it. */
static void __attribute__((format(printf, 2, 3))) set_error(struct oam_job *j, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(j->error, sizeof j->error, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof j->error) strcpy(j->error + sizeof j->error - 4, "...");
}

static void fail(struct oam_job *j, const char *what)
{
    set_error(j, "%s: %s", what, oam_imap_error(&imap));
}

static int read_token(const char *path, char *out, size_t size)
{
    BPTR fh = Open((STRPTR)path, MODE_OLDFILE);
    LONG n;
    if (!fh) return 0;
    n = Read(fh, out, size - 1);
    Close(fh);
    if (n <= 0) return 0;
    out[n] = 0;
    out[strcspn(out, "\r\n \t")] = 0;
    return out[0] != 0;
}

static void do_connect(struct oam_job *j)
{
    const oam_account *a = &j->account;
    const char *user = a->user[0] ? a->user : a->address;
    if (connected) { oam_imap_logout(&imap); connected = 0; }
    oam_imap_dispose(&imap);
    oam_imap_init(&imap);
    if (!oam_imap_connect(&imap, a->imap.host, a->imap.port, a->imap.security)) { fail(j, a->imap.host); return; }
    if (!strcmp(a->auth, "password")) {
        j->ok = oam_imap_login(&imap, user, a->secret);
    } else if (!strcmp(a->auth, "xoauth2-token")) {
        static char token[4096];
        if (!read_token(a->secret, token, sizeof token)) {
            set_error(j, "The token file %s could not be read", a->secret);
            oam_imap_close(&imap);
            return;
        }
        j->ok = oam_imap_login_xoauth2(&imap, user, token);
        memset(token, 0, sizeof token);
    } else {
        snprintf(j->error, sizeof j->error, "Signing in with %s comes in a later version", a->auth);
        oam_imap_close(&imap);
        return;
    }
    if (!j->ok) { fail(j, "Signing in"); oam_imap_close(&imap); return; }
    connected = 1;
}

static void do_job(struct oam_job *j)
{
    j->ok = 0;
    if (j->kind == OAM_JOB_CONNECT) { do_connect(j); return; }
    if (!connected) { snprintf(j->error, sizeof j->error, "Not connected"); return; }
    switch (j->kind) {
        case OAM_JOB_FOLDERS:
            if (!(j->ok = oam_imap_list(&imap, &j->folders, &j->nfolders))) fail(j, "Listing folders");
            break;
        case OAM_JOB_OPEN:
            if (!(j->ok = oam_imap_select(&imap, j->folder, 0))) { fail(j, j->folder); break; }
            j->exists = imap.exists;
            j->ok = oam_imap_fetch_summaries(&imap, imap.uidnext > NEWEST ? imap.uidnext - NEWEST : 1, &j->msgs, &j->nmsgs);
            if (!j->ok) fail(j, "Reading the folder");
            break;
        case OAM_JOB_FETCH:
            if (!(j->ok = oam_imap_fetch_message(&imap, j->uid, &j->message))) fail(j, "Fetching the message");
            break;
        case OAM_JOB_FLAG:
            if (!(j->ok = oam_imap_set_flags(&imap, j->uid, j->flags, j->add))) fail(j, "Setting flags");
            break;
    }
    if (!j->ok && !imap.conn) connected = 0;            /* the connection is gone: CONNECT again */
}

static void worker_entry(void)
{
    struct MsgPort *port = CreateMsgPort();
    char err[200];
    int net = 0;
    if (port && !(net = oam_net_init(err, sizeof err))) snprintf(start_err, sizeof start_err, "%s", err);
    if (!port) snprintf(start_err, sizeof start_err, "no message port");
    worker_port = net ? port : NULL;
    Signal(parent, 1UL << ready_sig);
    if (!net) {
        if (port) DeleteMsgPort(port);
        return;
    }
    oam_imap_init(&imap);
    for (;;) {
        struct oam_job *j;
        WaitPort(port);
        while ((j = (struct oam_job *)GetMsg(port))) {
            if (j->kind == OAM_JOB_QUIT) {
                if (connected) oam_imap_logout(&imap);
                oam_imap_dispose(&imap);
                oam_net_cleanup();
                Forbid();                                  /* the window may unload us once replied */
                worker_port = NULL;
                DeleteMsgPort(port);
                ReplyMsg(&j->msg);
                return;
            }
            do_job(j);
            ReplyMsg(&j->msg);
        }
    }
}

/* ---- the window's side ----------------------------------------------------------- */

int oam_worker_start(char *err, size_t errlen)
{
    parent = FindTask(NULL);
    if ((ready_sig = AllocSignal(-1)) < 0) { snprintf(err, errlen, "no free signal"); return 0; }
    SetSignal(0, 1UL << ready_sig);
    if (!CreateNewProcTags(NP_Entry, (ULONG)worker_entry, NP_Name, (ULONG)"OpenMail network",
                           NP_StackSize, 65536, NP_Priority, 0, TAG_DONE)) {
        snprintf(err, errlen, "the network process could not start");
        FreeSignal(ready_sig);
        ready_sig = -1;
        return 0;
    }
    Wait(1UL << ready_sig);
    FreeSignal(ready_sig);
    ready_sig = -1;
    if (!worker_port) {
        snprintf(err, errlen, "%s", start_err[0] ? start_err : "the network did not start");
        return 0;
    }
    return 1;
}

void oam_worker_put(struct oam_job *j)
{
    PutMsg(worker_port, &j->msg);
}

void oam_worker_stop(struct MsgPort *replies)
{
    struct oam_job *q, *j;
    if (!worker_port) return;
    if (!(q = oam_job_new(OAM_JOB_QUIT, replies))) return;
    PutMsg(worker_port, &q->msg);
    for (;;) {                                             /* earlier jobs come back first */
        WaitPort(replies);
        j = (struct oam_job *)GetMsg(replies);
        if (!j) continue;
        if (j == q) break;
        oam_job_free(j);
    }
    oam_job_free(q);
}
