/* oam_imap: an IMAP4rev1 client (RFC 3501): connect (plain, TLS, or
 * STARTTLS), log in (AUTHENTICATE PLAIN or LOGIN with a password,
 * AUTHENTICATE XOAUTH2 with an OAuth token), list folders, select one,
 * fetch message summaries and whole messages, set flags.
 *
 * Every call blocks until the server's tagged answer and returns 1 for OK.
 * On failure, oam_imap_error() says why: the server's own text where it
 * gave one. Mailbox names are kept as the server sends them (modified
 * UTF-7, used in commands) and as UTF-8 (for people). */
#ifndef OAM_IMAP_H
#define OAM_IMAP_H

#include "oam_buf.h"
#include "oam_io.h"

enum { OAM_IMAP_PLAIN = 0, OAM_IMAP_TLS = 1, OAM_IMAP_STARTTLS = 2 };

/* message flags */
enum {
    OAM_F_SEEN = 1, OAM_F_ANSWERED = 2, OAM_F_FLAGGED = 4, OAM_F_DELETED = 8, OAM_F_DRAFT = 16, OAM_F_RECENT = 32
};

/* folder attributes */
enum { OAM_FOLDER_NOSELECT = 1, OAM_FOLDER_CHILDREN = 2, OAM_FOLDER_NOCHILDREN = 4 };

typedef struct oam_imap_folder {
    char *name;          /* as the server spells it (modified UTF-7) */
    char *display;       /* UTF-8 */
    char delimiter;      /* the hierarchy separator, or 0 */
    unsigned attrs;      /* OAM_FOLDER_* */
} oam_imap_folder;

typedef struct oam_imap_msg {
    unsigned long uid, size;
    unsigned flags;      /* OAM_F_* */
    char *internaldate;  /* the server's arrival date, as sent */
    char *header;        /* the requested header fields, raw (RFC 5322) */
} oam_imap_msg;

/* the protocol, line by line: sent = 1 for the client's lines. AUTHENTICATE
 * and LOGIN lines are shown with their secrets replaced. */
typedef void (*oam_imap_trace)(void *user, int sent, const char *line);

typedef struct oam_imap {
    oam_conn *conn;
    oam_io io;
    unsigned tagn;
    oam_buf caps;        /* " CAP1 CAP2 ... " in capitals, for oam_imap_has */
    oam_buf err;
    int logged_in;
    /* the selected mailbox */
    unsigned long exists, recent, uidvalidity, uidnext;
    int readonly;
    oam_imap_trace trace;
    void *trace_user;
    int hide_next;       /* the next line sent carries a secret */
} oam_imap;

void oam_imap_init(oam_imap *m);
int oam_imap_connect(oam_imap *m, const char *host, int port, int security);
int oam_imap_has(oam_imap *m, const char *capability);              /* e.g. "AUTH=XOAUTH2", "IDLE" */
int oam_imap_login(oam_imap *m, const char *user, const char *password);
int oam_imap_login_xoauth2(oam_imap *m, const char *user, const char *token);
int oam_imap_list(oam_imap *m, oam_imap_folder **folders, int *count);
int oam_imap_select(oam_imap *m, const char *name, int readonly);
/* summaries of messages with UID uid_from and up: flags, size, arrival date
 * and the header fields that a message list shows */
int oam_imap_fetch_summaries(oam_imap *m, unsigned long uid_from, oam_imap_msg **msgs, int *count);
int oam_imap_fetch_message(oam_imap *m, unsigned long uid, oam_buf *out);   /* the whole message, unread flag kept */
int oam_imap_set_flags(oam_imap *m, unsigned long uid, unsigned flags, int add);
int oam_imap_noop(oam_imap *m);
void oam_imap_logout(oam_imap *m);                                  /* LOGOUT, then closes */
void oam_imap_close(oam_imap *m);                                   /* closes without LOGOUT */
void oam_imap_dispose(oam_imap *m);                                 /* closes, and frees what the client holds */
const char *oam_imap_error(oam_imap *m);

void oam_imap_free_folders(oam_imap_folder *folders, int count);
void oam_imap_free_msgs(oam_imap_msg *msgs, int count);

/* modified UTF-7 (RFC 3501 5.1.3) to UTF-8, and back */
char *oam_mutf7_decode(const char *name);
char *oam_mutf7_encode(const char *utf8);

#endif
