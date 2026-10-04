/* oam_http (oam_http.h). OpenMail. MIT, Copyright (c) 2026 Dalsin Limited. */
#include "oam_http.h"
#include "oam_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t errlen, const char *what)
{
    if (err && errlen) snprintf(err, errlen, "%s", what);
}

void oam_form_add(oam_buf *form, const char *key, const char *value)
{
    static const char hex[] = "0123456789ABCDEF";
    if (form->len) oam_buf_addc(form, '&');
    oam_buf_adds(form, key);
    oam_buf_addc(form, '=');
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
            *p == '-' || *p == '_' || *p == '.' || *p == '~') oam_buf_addc(form, (char)*p);
        else { oam_buf_addc(form, '%'); oam_buf_addc(form, hex[*p >> 4]); oam_buf_addc(form, hex[*p & 15]); }
    }
}

static int dechunk(const char *p, size_t len, oam_buf *out)
{
    const char *end = p + len;
    while (p < end) {
        char *next;
        unsigned long n = strtoul(p, &next, 16);
        const char *line = strstr(p, "\r\n");
        if (!line || next == p) return 0;
        p = line + 2;
        if (n == 0) return 1;
        if ((size_t)(end - p) < n) return 0;
        oam_buf_add(out, p, n);
        p += n + 2;
    }
    return 1;
}

int oam_http_post_form(const char *host, const char *path, const char *form,
                       int *status, oam_buf *body, char *err, size_t errlen)
{
    oam_buf req, raw;
    oam_conn *c;
    char buf[4096];
    long n;
    int ok = 0;
    *status = 0;
    if (!(c = oam_net_connect(host, 443, 1, err, errlen))) return 0;
    oam_net_set_timeout(c, 30);
    oam_buf_init(&req);
    oam_buf_init(&raw);
    oam_buf_printf(&req, "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: OpenMail/0.2 (AmigaOS)\r\n"
                   "Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n"
                   "Content-Length: %lu\r\nConnection: close\r\n\r\n%s",
                   path, host, (unsigned long)strlen(form), form);
    if (oam_net_write(c, req.data, (long)req.len) < 0) { set_err(err, errlen, oam_net_error(c)); goto done; }
    while ((n = oam_net_read(c, buf, sizeof buf)) > 0) oam_buf_add(&raw, buf, (size_t)n);
    if (!raw.len) { set_err(err, errlen, "the server sent nothing back"); goto done; }
    {
        const char *head_end = strstr(raw.data, "\r\n\r\n");
        if (!head_end || sscanf(raw.data, "HTTP/1.%*c %d", status) != 1) { set_err(err, errlen, "not an HTTP answer"); goto done; }
        const char *bodyp = head_end + 4;
        size_t blen = raw.len - (size_t)(bodyp - raw.data);
        char *head = malloc((size_t)(head_end - raw.data) + 1);
        if (!head) { set_err(err, errlen, "out of memory"); goto done; }
        memcpy(head, raw.data, (size_t)(head_end - raw.data));
        head[head_end - raw.data] = 0;
        for (char *q = head; *q; q++) if (*q >= 'A' && *q <= 'Z') *q += 32;
        int chunked = strstr(head, "transfer-encoding: chunked") != NULL;
        free(head);
        oam_buf_clear(body);
        if (chunked ? !dechunk(bodyp, blen, body) : !oam_buf_add(body, bodyp, blen)) { set_err(err, errlen, "a broken answer"); goto done; }
        ok = 1;
    }
done:
    oam_net_close(c);
    memset(req.data ? req.data : (char *)"", 0, req.len);   /* the form may hold a token */
    oam_buf_free(&req);
    oam_buf_free(&raw);
    return ok;
}

int oam_json_field(const char *json, const char *key, char *out, size_t outlen)
{
    char pat[96];
    const char *p;
    size_t o = 0;
    if (!outlen) return 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    for (p = strstr(json, pat); p; p = strstr(p + 1, pat)) {
        const char *q = p + strlen(pat);
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q != ':') continue;
        q++;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n') q++;
        if (*q == '"') {
            for (q++; *q && *q != '"' && o + 1 < outlen; q++) {
                if (*q == '\\' && q[1]) {
                    q++;
                    char c = *q == 'n' ? '\n' : *q == 't' ? '\t' : *q == 'r' ? '\r' : *q;
                    if (*q == 'u') { c = '?'; if (q[1] && q[2] && q[3] && q[4]) q += 4; }
                    out[o++] = c;
                } else out[o++] = *q;
            }
        } else {
            while (*q && (*q == '-' || (*q >= '0' && *q <= '9') || *q == '.') && o + 1 < outlen) out[o++] = *q++;
        }
        out[o] = 0;
        return o > 0;
    }
    out[0] = 0;
    return 0;
}
