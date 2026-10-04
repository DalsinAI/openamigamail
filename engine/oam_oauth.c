/* oam_oauth (oam_oauth.h). OpenMail. MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oam_oauth.h"
#include "oam_http.h"
#include "oam_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int oam_oauth_split_url(const char *url, char *host, size_t hostlen, char *path, size_t pathlen)
{
    const char *h, *slash;
    if (strncmp(url, "https://", 8)) return 0;
    h = url + 8;
    slash = strchr(h, '/');
    snprintf(host, hostlen, "%.*s", slash ? (int)(slash - h) : (int)strlen(h), h);
    snprintf(path, pathlen, "%s", slash ? slash : "/");
    return host[0] != 0;
}

/* POSTs form to url; the body's JSON in out. 1 with *status, or 0 with err. */
static int post(const char *url, oam_buf *form, int *status, oam_buf *out, char *err, size_t errlen)
{
    char host[128], path[256];
    if (!oam_oauth_split_url(url, host, sizeof host, path, sizeof path)) {
        snprintf(err, errlen, "the sign-in address is not https: %s", url);
        return 0;
    }
    return oam_http_post_form(host, path, form->data ? form->data : "", status, out, err, errlen);
}

static void explain(const char *json, int status, char *err, size_t errlen)
{
    char code[64], text[256];
    if (!oam_json_field(json, "error", code, sizeof code)) snprintf(code, sizeof code, "HTTP %d", status);
    if (oam_json_field(json, "error_description", text, sizeof text)) {
        char *nl = strpbrk(text, "\r\n");          /* Microsoft adds trace lines */
        if (nl) *nl = 0;
        snprintf(err, errlen, "%s: %s", code, text);
    } else snprintf(err, errlen, "%s", code);
}

int oam_oauth_device_start(const oam_oauth_client *c, oam_device_code *d, char *err, size_t errlen)
{
    oam_buf form, out;
    char num[16];
    int status, ok = 0;
    memset(d, 0, sizeof *d);
    if (!c->client[0] || !strcmp(c->client, "REGISTER-ME")) {
        snprintf(err, errlen, "OpenMail isn't registered with this provider yet (no client id)");
        return 0;
    }
    oam_buf_init(&form);
    oam_buf_init(&out);
    oam_form_add(&form, "client_id", c->client);
    oam_form_add(&form, "scope", c->scope);
    if (post(c->device_url, &form, &status, &out, err, errlen)) {
        const char *j = out.data ? out.data : "";
        if (status == 200 && oam_json_field(j, "device_code", d->device_code, sizeof d->device_code) &&
            oam_json_field(j, "user_code", d->user_code, sizeof d->user_code)) {
            if (!oam_json_field(j, "verification_uri", d->verify_url, sizeof d->verify_url))
                oam_json_field(j, "verification_url", d->verify_url, sizeof d->verify_url);   /* Google's spelling */
            d->interval = oam_json_field(j, "interval", num, sizeof num) ? atoi(num) : 5;
            d->expires_in = oam_json_field(j, "expires_in", num, sizeof num) ? atoi(num) : 900;
            if (d->interval < 1) d->interval = 5;
            ok = 1;
        } else explain(j, status, err, errlen);
    }
    oam_buf_free(&form);
    oam_buf_free(&out);
    return ok;
}

static int tokens_from(const char *j, oam_tokens *t)
{
    char num[16];
    memset(t, 0, sizeof *t);
    if (!oam_json_field(j, "access_token", t->access, sizeof t->access)) return 0;
    oam_json_field(j, "refresh_token", t->refresh, sizeof t->refresh);
    t->expires_in = oam_json_field(j, "expires_in", num, sizeof num) ? atoi(num) : 3600;
    return 1;
}

int oam_oauth_device_poll(const oam_oauth_client *c, oam_device_code *d, oam_tokens *t, char *err, size_t errlen)
{
    oam_buf form, out;
    int status, result = OAM_OAUTH_ERROR;
    oam_buf_init(&form);
    oam_buf_init(&out);
    oam_form_add(&form, "grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    oam_form_add(&form, "client_id", c->client);
    oam_form_add(&form, "device_code", d->device_code);
    if (post(c->token_url, &form, &status, &out, err, errlen)) {
        const char *j = out.data ? out.data : "";
        char code[64];
        if (status == 200 && tokens_from(j, t)) result = OAM_OAUTH_DONE;
        else if (oam_json_field(j, "error", code, sizeof code) && !strcmp(code, "authorization_pending")) result = OAM_OAUTH_PENDING;
        else if (!strcmp(code, "slow_down")) { d->interval += 5; result = OAM_OAUTH_PENDING; }
        else if (!strcmp(code, "authorization_declined") || !strcmp(code, "access_denied")) snprintf(err, errlen, "The sign-in was declined");
        else if (!strcmp(code, "expired_token")) snprintf(err, errlen, "The code expired: start again");
        else explain(j, status, err, errlen);
    }
    oam_buf_free(&form);
    oam_buf_free(&out);
    return result;
}

int oam_oauth_refresh(const oam_oauth_client *c, const char *refresh, oam_tokens *t, char *err, size_t errlen)
{
    oam_buf form, out;
    int status, ok = 0;
    oam_buf_init(&form);
    oam_buf_init(&out);
    oam_form_add(&form, "grant_type", "refresh_token");
    oam_form_add(&form, "client_id", c->client);
    oam_form_add(&form, "refresh_token", refresh);
    oam_form_add(&form, "scope", c->scope);
    if (post(c->token_url, &form, &status, &out, err, errlen)) {
        const char *j = out.data ? out.data : "";
        if (status == 200 && tokens_from(j, t)) {
            if (!t->refresh[0]) snprintf(t->refresh, sizeof t->refresh, "%s", refresh);   /* not rotated: keep it */
            ok = 1;
        } else explain(j, status, err, errlen);
    }
    memset(form.data ? form.data : (char *)"", 0, form.len);
    oam_buf_free(&form);
    oam_buf_free(&out);
    return ok;
}
