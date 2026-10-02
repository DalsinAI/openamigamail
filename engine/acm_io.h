/* acm_io: buffered reading of a connection by lines (CRLF or LF) and by
 * byte counts (IMAP literals), and writing. Protocol code reads through it,
 * never from acm_net directly. */
#ifndef ACM_IO_H
#define ACM_IO_H

#include "acm_buf.h"
#include "acm_net.h"

typedef struct acm_io {
    acm_conn *conn;
    char in[4096];
    long pos, end;          /* unread bytes are in[pos..end) */
    void (*trace)(void *user, int sent, const char *line, size_t len);   /* optional protocol log */
    void *trace_user;
    int closed;             /* the peer closed the connection, or it failed */
} acm_io;

void acm_io_init(acm_io *io, acm_conn *conn);
/* one line into line (cleared first), without its CRLF: 1, or 0 at the end
 * of the connection. A line longer than max bytes fails (0). */
int acm_io_readline(acm_io *io, acm_buf *line, size_t max);
/* exactly n bytes appended to out: 1, or 0 if the connection ended first */
int acm_io_readn(acm_io *io, acm_buf *out, size_t n);
int acm_io_write(acm_io *io, const char *s, size_t n);       /* 1 when all was written */
int acm_io_writes(acm_io *io, const char *s);

#endif
