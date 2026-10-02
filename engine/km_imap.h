/* km_imap: an IMAP4rev1 client (RFC 3501): connect (plain, TLS, or
 * STARTTLS), log in (AUTHENTICATE PLAIN or LOGIN with a password,
 * AUTHENTICATE XOAUTH2 with an OAuth token), list folders, select one,
 * fetch message summaries and whole messages, set flags.
 *
 * Every call blocks until the server's tagged answer and returns 1 for OK.
 * On failure, km_imap_error() says why: the server's own text where it
 * gave one. Mailbox names are kept as the server sends them (modified
 * UTF-7, used in commands) and as UTF-8 (for people). */
#ifndef KM_IMAP_H
#define KM_IMAP_H

#include "km_buf.h"
#include "km_io.h"

enum { KM_IMAP_PLAIN = 0, KM_IMAP_TLS = 1, KM_IMAP_STARTTLS = 2 };

/* message flags */
enum {
    KM_F_SEEN = 1, KM_F_ANSWERED = 2, KM_F_FLAGGED = 4, KM_F_DELETED = 8, KM_F_DRAFT = 16, KM_F_RECENT = 32
};

/* folder attributes */
enum { KM_FOLDER_NOSELECT = 1, KM_FOLDER_CHILDREN = 2, KM_FOLDER_NOCHILDREN = 4 };

typedef struct km_imap_folder {
    char *name;          /* as the server spells it (modified UTF-7) */
    char *display;       /* UTF-8 */
    char delimiter;      /* the hierarchy separator, or 0 */
    unsigned attrs;      /* KM_FOLDER_* */
} km_imap_folder;

typedef struct km_imap_msg {
    unsigned long uid, size;
    unsigned flags;      /* KM_F_* */
    char *internaldate;  /* the server's arrival date, as sent */
    char *header;        /* the requested header fields, raw (RFC 5322) */
} km_imap_msg;

/* the protocol, line by line: sent = 1 for the client's lines. AUTHENTICATE
 * and LOGIN lines are shown with their secrets replaced. */
typedef void (*km_imap_trace)(void *user, int sent, const char *line);

typedef struct km_imap {
    km_conn *conn;
    km_io io;
    unsigned tagn;
    km_buf caps;        /* " CAP1 CAP2 ... " in capitals, for km_imap_has */
    km_buf err;
    int logged_in;
    /* the selected mailbox */
    unsigned long exists, recent, uidvalidity, uidnext;
    int readonly;
    km_imap_trace trace;
    void *trace_user;
    int hide_next;       /* the next line sent carries a secret */
} km_imap;

void km_imap_init(km_imap *m);
int km_imap_connect(km_imap *m, const char *host, int port, int security);
int km_imap_has(km_imap *m, const char *capability);              /* e.g. "AUTH=XOAUTH2", "IDLE" */
int km_imap_login(km_imap *m, const char *user, const char *password);
int km_imap_login_xoauth2(km_imap *m, const char *user, const char *token);
int km_imap_list(km_imap *m, km_imap_folder **folders, int *count);
int km_imap_select(km_imap *m, const char *name, int readonly);
/* summaries of messages with UID uid_from and up: flags, size, arrival date
 * and the header fields that a message list shows */
int km_imap_fetch_summaries(km_imap *m, unsigned long uid_from, km_imap_msg **msgs, int *count);
int km_imap_fetch_message(km_imap *m, unsigned long uid, km_buf *out);   /* the whole message, unread flag kept */
int km_imap_set_flags(km_imap *m, unsigned long uid, unsigned flags, int add);
int km_imap_noop(km_imap *m);
void km_imap_logout(km_imap *m);                                  /* LOGOUT, then closes */
void km_imap_close(km_imap *m);                                   /* closes without LOGOUT */
void km_imap_dispose(km_imap *m);                                 /* closes, and frees what the client holds */
const char *km_imap_error(km_imap *m);

void km_imap_free_folders(km_imap_folder *folders, int count);
void km_imap_free_msgs(km_imap_msg *msgs, int count);

/* modified UTF-7 (RFC 3501 5.1.3) to UTF-8, and back */
char *km_mutf7_decode(const char *name);
char *km_mutf7_encode(const char *utf8);

#endif
