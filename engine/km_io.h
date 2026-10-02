/* km_io: buffered reading of a connection by lines (CRLF or LF) and by
 * byte counts (IMAP literals), and writing. Protocol code reads through it,
 * never from km_net directly. */
#ifndef KM_IO_H
#define KM_IO_H

#include "km_buf.h"
#include "km_net.h"

typedef struct km_io {
    km_conn *conn;
    char in[4096];
    long pos, end;          /* unread bytes are in[pos..end) */
    void (*trace)(void *user, int sent, const char *line, size_t len);   /* optional protocol log */
    void *trace_user;
    int closed;             /* the peer closed the connection, or it failed */
} km_io;

void km_io_init(km_io *io, km_conn *conn);
/* one line into line (cleared first), without its CRLF: 1, or 0 at the end
 * of the connection. A line longer than max bytes fails (0). */
int km_io_readline(km_io *io, km_buf *line, size_t max);
/* exactly n bytes appended to out: 1, or 0 if the connection ended first */
int km_io_readn(km_io *io, km_buf *out, size_t n);
int km_io_write(km_io *io, const char *s, size_t n);       /* 1 when all was written */
int km_io_writes(km_io *io, const char *s);

#endif
