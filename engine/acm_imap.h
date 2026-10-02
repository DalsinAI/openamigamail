/* acm_imap: an IMAP4rev1 client (RFC 3501): connect (plain, TLS, or
 * STARTTLS), log in (AUTHENTICATE PLAIN or LOGIN with a password,
 * AUTHENTICATE XOAUTH2 with an OAuth token), list folders, select one,
 * fetch message summaries and whole messages, set flags.
 *
 * Every call blocks until the server's tagged answer and returns 1 for OK.
 * On failure, acm_imap_error() says why: the server's own text where it
 * gave one. Mailbox names are kept as the server sends them (modified
 * UTF-7, used in commands) and as UTF-8 (for people). */
#ifndef ACM_IMAP_H
#define ACM_IMAP_H

#include "acm_buf.h"
#include "acm_io.h"

enum { ACM_IMAP_PLAIN = 0, ACM_IMAP_TLS = 1, ACM_IMAP_STARTTLS = 2 };

/* message flags */
enum {
    ACM_F_SEEN = 1, ACM_F_ANSWERED = 2, ACM_F_FLAGGED = 4, ACM_F_DELETED = 8, ACM_F_DRAFT = 16, ACM_F_RECENT = 32
};

/* folder attributes */
enum { ACM_FOLDER_NOSELECT = 1, ACM_FOLDER_CHILDREN = 2, ACM_FOLDER_NOCHILDREN = 4 };

typedef struct acm_imap_folder {
    char *name;          /* as the server spells it (modified UTF-7) */
    char *display;       /* UTF-8 */
    char delimiter;      /* the hierarchy separator, or 0 */
    unsigned attrs;      /* ACM_FOLDER_* */
} acm_imap_folder;

typedef struct acm_imap_msg {
    unsigned long uid, size;
    unsigned flags;      /* ACM_F_* */
    char *internaldate;  /* the server's arrival date, as sent */
    char *header;        /* the requested header fields, raw (RFC 5322) */
} acm_imap_msg;

/* the protocol, line by line: sent = 1 for the client's lines. AUTHENTICATE
 * and LOGIN lines are shown with their secrets replaced. */
typedef void (*acm_imap_trace)(void *user, int sent, const char *line);

typedef struct acm_imap {
    acm_conn *conn;
    acm_io io;
    unsigned tagn;
    acm_buf caps;        /* " CAP1 CAP2 ... " in capitals, for acm_imap_has */
    acm_buf err;
    int logged_in;
    /* the selected mailbox */
    unsigned long exists, recent, uidvalidity, uidnext;
    int readonly;
    acm_imap_trace trace;
    void *trace_user;
    int hide_next;       /* the next line sent carries a secret */
} acm_imap;

void acm_imap_init(acm_imap *m);
int acm_imap_connect(acm_imap *m, const char *host, int port, int security);
int acm_imap_has(acm_imap *m, const char *capability);              /* e.g. "AUTH=XOAUTH2", "IDLE" */
int acm_imap_login(acm_imap *m, const char *user, const char *password);
int acm_imap_login_xoauth2(acm_imap *m, const char *user, const char *token);
int acm_imap_list(acm_imap *m, acm_imap_folder **folders, int *count);
int acm_imap_select(acm_imap *m, const char *name, int readonly);
/* summaries of messages with UID uid_from and up: flags, size, arrival date
 * and the header fields that a message list shows */
int acm_imap_fetch_summaries(acm_imap *m, unsigned long uid_from, acm_imap_msg **msgs, int *count);
int acm_imap_fetch_message(acm_imap *m, unsigned long uid, acm_buf *out);   /* the whole message, unread flag kept */
int acm_imap_set_flags(acm_imap *m, unsigned long uid, unsigned flags, int add);
int acm_imap_noop(acm_imap *m);
void acm_imap_logout(acm_imap *m);                                  /* LOGOUT, then closes */
void acm_imap_close(acm_imap *m);                                   /* closes without LOGOUT */
void acm_imap_dispose(acm_imap *m);                                 /* closes, and frees what the client holds */
const char *acm_imap_error(acm_imap *m);

void acm_imap_free_folders(acm_imap_folder *folders, int count);
void acm_imap_free_msgs(acm_imap_msg *msgs, int count);

/* modified UTF-7 (RFC 3501 5.1.3) to UTF-8, and back */
char *acm_mutf7_decode(const char *name);
char *acm_mutf7_encode(const char *utf8);

#endif
