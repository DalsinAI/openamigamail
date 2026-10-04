#include "oam_io.h"

#include <string.h>

void oam_io_init(oam_io *io, oam_conn *conn)
{
    memset(io, 0, sizeof *io);
    io->conn = conn;
}

static int fill(oam_io *io)
{
    long n;
    if (io->closed) return 0;
    io->pos = 0;
    n = oam_net_read(io->conn, io->in, (long)sizeof io->in);
    if (n <= 0) { io->closed = 1; io->end = 0; return 0; }
    io->end = n;
    return 1;
}

int oam_io_readline(oam_io *io, oam_buf *line, size_t max)
{
    oam_buf_clear(line);
    for (;;) {
        char *nl;
        long avail;
        if (io->pos >= io->end && !fill(io)) return 0;
        avail = io->end - io->pos;
        nl = memchr(io->in + io->pos, '\n', (size_t)avail);
        if (nl) {
            long n = (long)(nl - (io->in + io->pos));
            if (!oam_buf_add(line, io->in + io->pos, (size_t)n)) return 0;
            io->pos += n + 1;
            if (line->len && line->data[line->len - 1] == '\r') line->data[--line->len] = 0;
            if (io->trace) io->trace(io->trace_user, 0, oam_buf_str(line), line->len);
            return 1;
        }
        if (!oam_buf_add(line, io->in + io->pos, (size_t)avail)) return 0;
        io->pos = io->end;
        if (line->len > max) return 0;
    }
}

int oam_io_readn(oam_io *io, oam_buf *out, size_t n)
{
    while (n) {
        long avail, take;
        if (io->pos >= io->end && !fill(io)) return 0;
        avail = io->end - io->pos;
        take = (size_t)avail < n ? avail : (long)n;
        if (!oam_buf_add(out, io->in + io->pos, (size_t)take)) return 0;
        io->pos += take;
        n -= (size_t)take;
    }
    return 1;
}

int oam_io_write(oam_io *io, const char *s, size_t n)
{
    if (io->trace) io->trace(io->trace_user, 1, s, n);
    return oam_net_write(io->conn, s, (long)n) == (long)n;
}

int oam_io_writes(oam_io *io, const char *s) { return oam_io_write(io, s, strlen(s)); }
