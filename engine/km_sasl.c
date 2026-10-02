#include "km_sasl.h"
#include "km_base64.h"

#include <string.h>

static int clean(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 32 || *s == 127) return 0;
    return 1;
}

int km_sasl_plain(km_buf *out, const char *user, const char *password)
{
    km_buf raw;
    int ok;
    if (!clean(user) || !clean(password)) return 0;
    km_buf_init(&raw);
    ok = km_buf_addc(&raw, 0) && km_buf_adds(&raw, user) && km_buf_addc(&raw, 0) && km_buf_adds(&raw, password)
         && km_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);         /* the password does not linger */
    km_buf_free(&raw);
    return ok;
}

int km_sasl_xoauth2(km_buf *out, const char *user, const char *token)
{
    km_buf raw;
    int ok;
    if (!clean(user) || !clean(token) || strchr(token, ' ')) return 0;
    km_buf_init(&raw);
    ok = km_buf_printf(&raw, "user=%s\001auth=Bearer %s\001\001", user, token) && km_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);
    km_buf_free(&raw);
    return ok;
}
