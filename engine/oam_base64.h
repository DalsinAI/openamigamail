/* oam_base64: RFC 4648 base64, for SASL (AUTHENTICATE) and MIME parts. */
#ifndef OAM_BASE64_H
#define OAM_BASE64_H

#include "oam_buf.h"

int oam_base64_encode(oam_buf *out, const void *data, size_t len);
/* decodes, skipping whitespace and line breaks; stops at '=' padding.
 * 0 on a character that is not base64. */
int oam_base64_decode(oam_buf *out, const char *text, size_t len);

#endif
