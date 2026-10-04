/* oam_buf: a growable byte buffer, always NUL-terminated so text can be
 * read straight out of it. OpenAmigaMail's engine is portable C: the same files
 * build for AmigaOS 3.x, AROS and the host's tests. */
#ifndef OAM_BUF_H
#define OAM_BUF_H

#include <stddef.h>

typedef struct oam_buf {
    char *data;        /* NULL until the first append; then data[len] == 0 */
    size_t len, cap;
    int failed;        /* an allocation failed: the contents are incomplete */
} oam_buf;

void oam_buf_init(oam_buf *b);
void oam_buf_free(oam_buf *b);
void oam_buf_clear(oam_buf *b);                 /* empty, keeping the memory */
int oam_buf_add(oam_buf *b, const void *p, size_t n);
int oam_buf_adds(oam_buf *b, const char *s);
int oam_buf_addc(oam_buf *b, char c);
int oam_buf_printf(oam_buf *b, const char *fmt, ...);
const char *oam_buf_str(const oam_buf *b);      /* "" for an empty buffer */
char *oam_buf_take(oam_buf *b);                 /* the string, now the caller's; the buffer is left empty */

#endif
