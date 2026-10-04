/* oam_io: buffered reading of a connection by lines (CRLF or LF) and by
 * byte counts (IMAP literals), and writing. Protocol code reads through it,
 * never from oam_net directly. */
#ifndef OAM_IO_H
#define OAM_IO_H

#include "oam_buf.h"
#include "oam_net.h"

typedef struct oam_io {
    oam_conn *conn;
    char in[4096];
    long pos, end;          /* unread bytes are in[pos..end) */
    void (*trace)(void *user, int sent, const char *line, size_t len);   /* optional protocol log */
    void *trace_user;
    int closed;             /* the peer closed the connection, or it failed */
} oam_io;

void oam_io_init(oam_io *io, oam_conn *conn);
/* one line into line (cleared first), without its CRLF: 1, or 0 at the end
 * of the connection. A line longer than max bytes fails (0). */
int oam_io_readline(oam_io *io, oam_buf *line, size_t max);
/* exactly n bytes appended to out: 1, or 0 if the connection ended first */
int oam_io_readn(oam_io *io, oam_buf *out, size_t n);
int oam_io_write(oam_io *io, const char *s, size_t n);       /* 1 when all was written */
int oam_io_writes(oam_io *io, const char *s);

#endif
