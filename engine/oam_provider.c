#include "oam_provider.h"
#include "oam_imap.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy(char *out, size_t size, const char *s, size_t n)
{
    if (n >= size) n = size - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

static int lower_eq(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

/* "host port security" -> s. 1, or 0 when it does not read. */
static int server(const char *v, oam_server *s)
{
    char host[128], sec[16];
    int port;
    if (sscanf(v, "%127s %d %15s", host, &port, sec) != 3 || port < 1 || port > 65535) return 0;
    if (!strcmp(sec, "tls")) s->security = OAM_IMAP_TLS;
    else if (!strcmp(sec, "starttls")) s->security = OAM_IMAP_STARTTLS;
    else if (!strcmp(sec, "plain")) s->security = OAM_IMAP_PLAIN;
    else return 0;
    copy(s->host, sizeof s->host, host, strlen(host));
    s->port = port;
    return 1;
}

int oam_provider_parse(const char *text, oam_provider *p, char *err, size_t errlen)
{
    int line = 0;
    memset(p, 0, sizeof *p);
    while (*text) {
        const char *end = strchr(text, '\n'), *k, *ke, *v, *ve;
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char key[32], value[256];
        line++;
        k = text;
        text += n + (end ? 1 : 0);
        ve = k + n;
        while (k < ve && isspace((unsigned char)*k)) k++;
        if (k == ve || *k == '#') continue;
        if (!(v = memchr(k, '=', ve - k))) {
            snprintf(err, errlen, "line %d: no '='", line);
            return 0;
        }
        ke = v++;
        while (ke > k && isspace((unsigned char)ke[-1])) ke--;
        while (v < ve && isspace((unsigned char)*v)) v++;
        while (ve > v && isspace((unsigned char)ve[-1])) ve--;
        if (ke == k) { snprintf(err, errlen, "line %d: no key", line); return 0; }
        copy(key, sizeof key, k, ke - k);
        copy(value, sizeof value, v, ve - v);
        if (!strcmp(key, "name")) copy(p->name, sizeof p->name, value, strlen(value));
        else if (!strcmp(key, "domains")) copy(p->domains, sizeof p->domains, value, strlen(value));
        else if (!strcmp(key, "imap") || !strcmp(key, "smtp")) {
            if (!server(value, key[0] == 'i' ? &p->imap : &p->smtp)) {
                snprintf(err, errlen, "line %d: %s wants \"host port tls|starttls|plain\"", line, key);
                return 0;
            }
        } else if (!strcmp(key, "auth")) {
            char *save = value, *w;
            p->nauth = 0;
            while ((w = strtok(save, " \t")) && p->nauth < OAM_PROVIDER_AUTHS) {
                save = NULL;
                copy(p->auth[p->nauth++], sizeof p->auth[0], w, strlen(w));
            }
        } else if (p->nextra < OAM_PROVIDER_EXTRAS) {
            copy(p->extra[p->nextra].key, sizeof p->extra[0].key, key, strlen(key));
            copy(p->extra[p->nextra].value, sizeof p->extra[0].value, value, strlen(value));
            p->nextra++;
        }
    }
    if (!p->name[0]) { snprintf(err, errlen, "no name"); return 0; }
    if (!p->imap.port) { snprintf(err, errlen, "no imap server"); return 0; }
    if (!p->nauth) { snprintf(err, errlen, "no auth methods"); return 0; }
    return 1;
}

const char *oam_provider_get(const oam_provider *p, const char *key)
{
    int i;
    for (i = 0; i < p->nextra; i++)
        if (!strcmp(p->extra[i].key, key)) return p->extra[i].value;
    return NULL;
}

int oam_provider_matches(const oam_provider *p, const char *address)
{
    const char *at = strrchr(address, '@'), *d = p->domains;
    size_t dl;
    if (!at || !at[1]) return 0;
    at++;
    dl = strlen(at);
    while (*d) {
        size_t n;
        while (*d == ' ') d++;
        n = strcspn(d, " ");
        if (n && (dl == n || (dl > n && at[dl - n - 1] == '.')) && lower_eq(at + dl - n, d, n)) return 1;
        d += n;
    }
    return 0;
}

int oam_provider_has_auth(const oam_provider *p, const char *method)
{
    int i;
    for (i = 0; i < p->nauth; i++)
        if (!strcmp(p->auth[i], method)) return 1;
    return 0;
}
