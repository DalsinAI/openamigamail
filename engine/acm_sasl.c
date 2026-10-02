#include "acm_sasl.h"
#include "acm_base64.h"

#include <string.h>

static int clean(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 32 || *s == 127) return 0;
    return 1;
}

int acm_sasl_plain(acm_buf *out, const char *user, const char *password)
{
    acm_buf raw;
    int ok;
    if (!clean(user) || !clean(password)) return 0;
    acm_buf_init(&raw);
    ok = acm_buf_addc(&raw, 0) && acm_buf_adds(&raw, user) && acm_buf_addc(&raw, 0) && acm_buf_adds(&raw, password)
         && acm_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);         /* the password does not linger */
    acm_buf_free(&raw);
    return ok;
}

int acm_sasl_xoauth2(acm_buf *out, const char *user, const char *token)
{
    acm_buf raw;
    int ok;
    if (!clean(user) || !clean(token) || strchr(token, ' ')) return 0;
    acm_buf_init(&raw);
    ok = acm_buf_printf(&raw, "user=%s\001auth=Bearer %s\001\001", user, token) && acm_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);
    acm_buf_free(&raw);
    return ok;
}
