#include "oam_sasl.h"
#include "oam_base64.h"

#include <string.h>

static int clean(const char *s)
{
    for (; *s; s++) if ((unsigned char)*s < 32 || *s == 127) return 0;
    return 1;
}

int oam_sasl_plain(oam_buf *out, const char *user, const char *password)
{
    oam_buf raw;
    int ok;
    if (!clean(user) || !clean(password)) return 0;
    oam_buf_init(&raw);
    ok = oam_buf_addc(&raw, 0) && oam_buf_adds(&raw, user) && oam_buf_addc(&raw, 0) && oam_buf_adds(&raw, password)
         && oam_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);         /* the password does not linger */
    oam_buf_free(&raw);
    return ok;
}

int oam_sasl_xoauth2(oam_buf *out, const char *user, const char *token)
{
    oam_buf raw;
    int ok;
    if (!clean(user) || !clean(token) || strchr(token, ' ')) return 0;
    oam_buf_init(&raw);
    ok = oam_buf_printf(&raw, "user=%s\001auth=Bearer %s\001\001", user, token) && oam_base64_encode(out, raw.data, raw.len);
    if (raw.data) memset(raw.data, 0, raw.len);
    oam_buf_free(&raw);
    return ok;
}
