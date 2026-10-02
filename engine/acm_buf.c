#include "acm_buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void acm_buf_init(acm_buf *b) { b->data = NULL; b->len = b->cap = 0; b->failed = 0; }

void acm_buf_free(acm_buf *b) { free(b->data); acm_buf_init(b); }

void acm_buf_clear(acm_buf *b) { b->len = 0; if (b->data) b->data[0] = 0; b->failed = 0; }

static int grow(acm_buf *b, size_t need)
{
    size_t cap;
    char *p;
    if (need + 1 <= b->cap) return 1;
    cap = b->cap ? b->cap : 64;
    while (cap < need + 1) cap *= 2;
    p = realloc(b->data, cap);
    if (!p) { b->failed = 1; return 0; }
    b->data = p;
    b->cap = cap;
    return 1;
}

int acm_buf_add(acm_buf *b, const void *p, size_t n)
{
    if (!grow(b, b->len + n)) return 0;
    if (n) memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
    return 1;
}

int acm_buf_adds(acm_buf *b, const char *s) { return acm_buf_add(b, s, strlen(s)); }

int acm_buf_addc(acm_buf *b, char c) { return acm_buf_add(b, &c, 1); }

int acm_buf_printf(acm_buf *b, const char *fmt, ...)
{
    va_list ap;
    int n;
    char small[256];
    va_start(ap, fmt);
    n = vsnprintf(small, sizeof small, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if ((size_t)n < sizeof small) return acm_buf_add(b, small, (size_t)n);
    if (!grow(b, b->len + (size_t)n)) return 0;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 1;
}

const char *acm_buf_str(const acm_buf *b) { return b->data ? b->data : ""; }

char *acm_buf_take(acm_buf *b)
{
    char *s = b->data;
    if (!s) { s = malloc(1); if (s) s[0] = 0; }
    acm_buf_init(b);
    return s;
}
