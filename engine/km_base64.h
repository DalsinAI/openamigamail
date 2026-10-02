/* km_base64: RFC 4648 base64, for SASL (AUTHENTICATE) and MIME parts. */
#ifndef KM_BASE64_H
#define KM_BASE64_H

#include "km_buf.h"

int km_base64_encode(km_buf *out, const void *data, size_t len);
/* decodes, skipping whitespace and line breaks; stops at '=' padding.
 * 0 on a character that is not base64. */
int km_base64_decode(km_buf *out, const char *text, size_t len);

#endif
