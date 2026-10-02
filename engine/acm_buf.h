/* acm_buf: a growable byte buffer, always NUL-terminated so text can be
 * read straight out of it. ACMail's engine is portable C: the same files
 * build for AmigaOS 3.x, AROS and the host's tests. */
#ifndef ACM_BUF_H
#define ACM_BUF_H

#include <stddef.h>

typedef struct acm_buf {
    char *data;        /* NULL until the first append; then data[len] == 0 */
    size_t len, cap;
    int failed;        /* an allocation failed: the contents are incomplete */
} acm_buf;

void acm_buf_init(acm_buf *b);
void acm_buf_free(acm_buf *b);
void acm_buf_clear(acm_buf *b);                 /* empty, keeping the memory */
int acm_buf_add(acm_buf *b, const void *p, size_t n);
int acm_buf_adds(acm_buf *b, const char *s);
int acm_buf_addc(acm_buf *b, char c);
int acm_buf_printf(acm_buf *b, const char *fmt, ...);
const char *acm_buf_str(const acm_buf *b);      /* "" for an empty buffer */
char *acm_buf_take(acm_buf *b);                 /* the string, now the caller's; the buffer is left empty */

#endif
