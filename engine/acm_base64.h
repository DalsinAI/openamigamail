/* acm_base64: RFC 4648 base64, for SASL (AUTHENTICATE) and MIME parts. */
#ifndef ACM_BASE64_H
#define ACM_BASE64_H

#include "acm_buf.h"

int acm_base64_encode(acm_buf *out, const void *data, size_t len);
/* decodes, skipping whitespace and line breaks; stops at '=' padding.
 * 0 on a character that is not base64. */
int acm_base64_decode(acm_buf *out, const char *text, size_t len);

#endif
