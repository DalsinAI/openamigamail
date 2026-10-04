#include "oam_account.h"
#include "oam_base64.h"
#include "oam_imap.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Obscuring: each byte XORed with a rolling value, then base64, marked "o1:".
 * It keeps a password from being read at a glance in the file, nothing more. */
static void obscure(const char *in, size_t n, char *out, int on)
{
    unsigned char k = 0x5a;
    size_t i;
    for (i = 0; i < n; i++) {
        out[i] = in[i] ^ k;
        k = (unsigned char)(k * 13 + 7 + (on ? (unsigned char)in[i] : (unsigned char)out[i]));
    }
}

static void copy(char *out, size_t size, const char *s)
{
    snprintf(out, size, "%s", s);
}

static int server_text(const char *v, oam_server *s)
{
    char host[128], sec[16];
    int port;
    if (sscanf(v, "%127s %d %15s", host, &port, sec) != 3 || port < 1 || port > 65535) return 0;
    s->security = !strcmp(sec, "tls") ? OAM_IMAP_TLS : !strcmp(sec, "starttls") ? OAM_IMAP_STARTTLS :
                  !strcmp(sec, "plain") ? OAM_IMAP_PLAIN : -1;
    if (s->security < 0) return 0;
    copy(s->host, sizeof s->host, host);
    s->port = port;
    return 1;
}

static const char *security_word(int s)
{
    return s == OAM_IMAP_TLS ? "tls" : s == OAM_IMAP_STARTTLS ? "starttls" : "plain";
}

int oam_account_parse(const char *text, oam_account *a, char *err, size_t errlen)
{
    int line = 0;
    memset(a, 0, sizeof *a);
    while (*text) {
        const char *end = strchr(text, '\n'), *k = text, *ve, *v, *ke;
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char key[24], value[512];
        line++;
        text += n + (end ? 1 : 0);
        ve = k + n;
        while (k < ve && isspace((unsigned char)*k)) k++;
        if (k == ve || *k == '#') continue;
        if (!(v = memchr(k, '=', ve - k))) { snprintf(err, errlen, "line %d: no '='", line); return 0; }
        ke = v++;
        while (ke > k && isspace((unsigned char)ke[-1])) ke--;
        while (v < ve && isspace((unsigned char)*v)) v++;
        while (ve > v && isspace((unsigned char)ve[-1])) ve--;
        snprintf(key, sizeof key, "%.*s", (int)(ke - k), k);
        snprintf(value, sizeof value, "%.*s", (int)(ve - v), v);
        if (!strcmp(key, "name")) copy(a->name, sizeof a->name, value);
        else if (!strcmp(key, "address")) copy(a->address, sizeof a->address, value);
        else if (!strcmp(key, "provider")) copy(a->provider, sizeof a->provider, value);
        else if (!strcmp(key, "auth")) copy(a->auth, sizeof a->auth, value);
        else if (!strcmp(key, "user")) copy(a->user, sizeof a->user, value);
        else if (!strcmp(key, "oauth_token")) copy(a->oauth_token, sizeof a->oauth_token, value);
        else if (!strcmp(key, "oauth_client")) copy(a->oauth_client, sizeof a->oauth_client, value);
        else if (!strcmp(key, "oauth_scope")) copy(a->oauth_scope, sizeof a->oauth_scope, value);
        else if (!strcmp(key, "imap") || !strcmp(key, "smtp")) {
            if (!server_text(value, key[0] == 'i' ? &a->imap : &a->smtp)) {
                snprintf(err, errlen, "line %d: %s wants \"host port tls|starttls|plain\"", line, key);
                return 0;
            }
        } else if (!strcmp(key, "secret")) {
            oam_buf raw;
            oam_buf_init(&raw);
            if (strncmp(value, "o1:", 3) || !oam_base64_decode(&raw, value + 3, strlen(value + 3)) ||
                raw.len >= sizeof a->secret) {
                oam_buf_free(&raw);
                snprintf(err, errlen, "line %d: the secret does not read", line);
                return 0;
            }
            obscure(raw.data ? raw.data : "", raw.len, a->secret, 0);
            a->secret[raw.len] = 0;
            oam_buf_free(&raw);
        }
    }
    if (!a->address[0]) { snprintf(err, errlen, "no address"); return 0; }
    if (!a->imap.port) { snprintf(err, errlen, "no imap server"); return 0; }
    if (!a->auth[0]) copy(a->auth, sizeof a->auth, "password");
    return 1;
}

int oam_account_format(const oam_account *a, oam_buf *out)
{
    char mixed[256];
    size_t n = strlen(a->secret);
    oam_buf_printf(out, "name     = %s\naddress  = %s\nprovider = %s\n", a->name, a->address, a->provider);
    oam_buf_printf(out, "imap     = %s %d %s\n", a->imap.host, a->imap.port, security_word(a->imap.security));
    if (a->smtp.port)
        oam_buf_printf(out, "smtp     = %s %d %s\n", a->smtp.host, a->smtp.port, security_word(a->smtp.security));
    oam_buf_printf(out, "auth     = %s\n", a->auth);
    if (a->user[0]) oam_buf_printf(out, "user     = %s\n", a->user);
    if (a->oauth_token[0]) oam_buf_printf(out, "oauth_token  = %s\noauth_client = %s\noauth_scope  = %s\n", a->oauth_token, a->oauth_client, a->oauth_scope);
    if (n) {
        obscure(a->secret, n, mixed, 1);
        oam_buf_adds(out, "secret   = o1:");
        oam_base64_encode(out, mixed, n);
        oam_buf_addc(out, '\n');
    }
    return !out->failed;
}

void oam_account_from_provider(oam_account *a, const oam_provider *p)
{
    copy(a->provider, sizeof a->provider, p->name);
    a->imap = p->imap;
    a->smtp = p->smtp;
    copy(a->auth, sizeof a->auth, p->nauth ? p->auth[0] : "password");
}
