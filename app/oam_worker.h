/* oam_worker: the process that talks to the mail servers (DESIGN.md 1).
 *
 * It owns bsdsocket.library and AmiSSL and runs the engine, whose calls
 * block until a server answers; so the window never waits on the network.
 * The window sends jobs (exec messages) and the worker replies to each
 * when it is done. One worker, one connection, jobs in the order sent. */
#ifndef OAM_WORKER_H
#define OAM_WORKER_H

#include <exec/ports.h>
#include "oam_account.h"
#include "oam_imap.h"
#include "oam_buf.h"

enum { OAM_JOB_CONNECT, OAM_JOB_FOLDERS, OAM_JOB_OPEN, OAM_JOB_FETCH, OAM_JOB_FLAG, OAM_JOB_QUIT };

struct oam_job {
    struct Message msg;
    int kind;
    /* what the job needs */
    oam_account account;            /* CONNECT */
    char folder[256];               /* OPEN: the folder's name as the server spells it */
    unsigned long uid;              /* FETCH, FLAG */
    unsigned flags;                 /* FLAG: OAM_F_* */
    int add;                        /* FLAG: 1 sets, 0 clears */
    long tag;                       /* the window's own: which request this answers */
    /* what it brings back */
    int ok;
    char error[256];
    oam_imap_folder *folders;       /* FOLDERS: the window frees them (oam_imap_free_folders) */
    int nfolders;
    oam_imap_msg *msgs;             /* OPEN: the newest summaries; the window frees them */
    int nmsgs;
    unsigned long exists;           /* OPEN */
    oam_buf message;                /* FETCH: the whole message */
};

/* A new job of a kind, cleared; NULL when memory is short. */
struct oam_job *oam_job_new(int kind, struct MsgPort *reply);
/* Frees a job and whatever it brought back. */
void oam_job_free(struct oam_job *j);

/* Starts the worker; jobs come back to replies. 1, or 0 with the reason. */
int oam_worker_start(char *err, size_t errlen);
void oam_worker_put(struct oam_job *j);
/* Logs out, closes the network and ends the worker. */
void oam_worker_stop(struct MsgPort *replies);

#endif
