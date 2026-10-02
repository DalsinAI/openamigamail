#include "km_io.h"

#include <string.h>

void km_io_init(km_io *io, km_conn *conn)
{
    memset(io, 0, sizeof *io);
    io->conn = conn;
}

static int fill(km_io *io)
{
    long n;
    if (io->closed) return 0;
    io->pos = 0;
    n = km_net_read(io->conn, io->in, (long)sizeof io->in);
    if (n <= 0) { io->closed = 1; io->end = 0; return 0; }
    io->end = n;
    return 1;
}

int km_io_readline(km_io *io, km_buf *line, size_t max)
{
    km_buf_clear(line);
    for (;;) {
        char *nl;
        long avail;
        if (io->pos >= io->end && !fill(io)) return 0;
        avail = io->end - io->pos;
        nl = memchr(io->in + io->pos, '\n', (size_t)avail);
        if (nl) {
            long n = (long)(nl - (io->in + io->pos));
            if (!km_buf_add(line, io->in + io->pos, (size_t)n)) return 0;
            io->pos += n + 1;
            if (line->len && line->data[line->len - 1] == '\r') line->data[--line->len] = 0;
            if (io->trace) io->trace(io->trace_user, 0, km_buf_str(line), line->len);
            return 1;
        }
        if (!km_buf_add(line, io->in + io->pos, (size_t)avail)) return 0;
        io->pos = io->end;
        if (line->len > max) return 0;
    }
}

int km_io_readn(km_io *io, km_buf *out, size_t n)
{
    while (n) {
        long avail, take;
        if (io->pos >= io->end && !fill(io)) return 0;
        avail = io->end - io->pos;
        take = (size_t)avail < n ? avail : (long)n;
        if (!km_buf_add(out, io->in + io->pos, (size_t)take)) return 0;
        io->pos += take;
        n -= (size_t)take;
    }
    return 1;
}

int km_io_write(km_io *io, const char *s, size_t n)
{
    if (io->trace) io->trace(io->trace_user, 1, s, n);
    return km_net_write(io->conn, s, (long)n) == (long)n;
}

int km_io_writes(km_io *io, const char *s) { return km_io_write(io, s, strlen(s)); }
