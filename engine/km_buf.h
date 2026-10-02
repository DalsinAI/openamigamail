/* km_buf: a growable byte buffer, always NUL-terminated so text can be
 * read straight out of it. KyneMail's engine is portable C: the same files
 * build for AmigaOS 3.x, AROS and the host's tests. */
#ifndef KM_BUF_H
#define KM_BUF_H

#include <stddef.h>

typedef struct km_buf {
    char *data;        /* NULL until the first append; then data[len] == 0 */
    size_t len, cap;
    int failed;        /* an allocation failed: the contents are incomplete */
} km_buf;

void km_buf_init(km_buf *b);
void km_buf_free(km_buf *b);
void km_buf_clear(km_buf *b);                 /* empty, keeping the memory */
int km_buf_add(km_buf *b, const void *p, size_t n);
int km_buf_adds(km_buf *b, const char *s);
int km_buf_addc(km_buf *b, char c);
int km_buf_printf(km_buf *b, const char *fmt, ...);
const char *km_buf_str(const km_buf *b);      /* "" for an empty buffer */
char *km_buf_take(km_buf *b);                 /* the string, now the caller's; the buffer is left empty */

#endif
