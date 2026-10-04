/* oam_sasl: the initial responses for SASL mechanisms, base64-encoded and
 * ready to send after AUTHENTICATE (IMAP) or AUTH (SMTP).
 *   PLAIN    RFC 4616: "\0user\0password"
 *   XOAUTH2  Google's and Microsoft's: "user=U\1auth=Bearer T\1\1"
 * A user, password or token with a control character is refused (0). */
#ifndef OAM_SASL_H
#define OAM_SASL_H

#include "oam_buf.h"

int oam_sasl_plain(oam_buf *out, const char *user, const char *password);
int oam_sasl_xoauth2(oam_buf *out, const char *user, const char *token);

#endif
