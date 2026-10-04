/* oam_http: one HTTPS POST of a form, on oam_net, for sign-in services
 * (OAuth). Not a browser: one request, the whole answer, Connection: close.
 *
 * OpenMail. MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OAM_HTTP_H
#define OAM_HTTP_H

#include <stddef.h>
#include "oam_buf.h"

/* POST form (already URL-encoded, "a=1&b=2") to https://host/path. 1 with
 * *status and body filled in (any status), 0 with the reason in err. */
int oam_http_post_form(const char *host, const char *path, const char *form,
                       int *status, oam_buf *body, char *err, size_t errlen);

/* Appends key=value to a form, URL-encoding the value. */
void oam_form_add(oam_buf *form, const char *key, const char *value);

/* The text of a top-level JSON string or number field, unescaped, or 0 when
 * it isn't there. Enough for OAuth answers; not a JSON library. */
int oam_json_field(const char *json, const char *key, char *out, size_t outlen);

#endif
