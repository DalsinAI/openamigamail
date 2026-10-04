#include "oam_mime.h"
#include "oam_base64.h"
#include "oam_html.h"
#include "oam_text.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 8

static void lower_copy(char *out, size_t size, const char *s, size_t n)
{
    size_t i;
    if (n >= size) n = size - 1;
    for (i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)s[i]);
    out[n] = 0;
}

/* The end of the header block and the start of the body. */
static int split(const char *msg, size_t len, size_t *hlen, const char **body)
{
    size_t i;
    if (len >= 1 && msg[0] == '\n') { *hlen = 0; *body = msg + 1; return 1; }
    if (len >= 2 && msg[0] == '\r' && msg[1] == '\n') { *hlen = 0; *body = msg + 2; return 1; }
    for (i = 0; i + 1 < len; i++) {
        if (msg[i] == '\n' && msg[i + 1] == '\n') { *hlen = i + 1; *body = msg + i + 2; return 1; }
        if (msg[i] == '\n' && msg[i + 1] == '\r' && i + 2 < len && msg[i + 2] == '\n') { *hlen = i + 1; *body = msg + i + 3; return 1; }
    }
    return 0;
}

/* A parameter's value from a Content-Type or Content-Disposition value:
 * name=value, name="value", or name*=charset''pct-encoded (RFC 2231). */
static int param(const char *v, const char *name, char *out, size_t size)
{
    size_t nl = strlen(name);
    const char *p = strchr(v, ';');
    while (p) {
        int ext = 0;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, name, nl) && (p[nl] == '=' || (p[nl] == '*' && p[nl + 1] == '='))) {
            const char *q = p + nl;
            size_t n = 0;
            if (*q == '*') { ext = 1; q++; }
            q++;
            if (ext) {                              /* charset'lang'value, %XX bytes */
                const char *t = strchr(q, '\''), *v2 = t ? strchr(t + 1, '\'') : NULL;
                char raw[256], cs[40];
                size_t r = 0;
                if (!t || !v2) return 0;
                lower_copy(cs, sizeof cs, q, t - q);
                for (q = v2 + 1; *q && *q != ';' && !isspace((unsigned char)*q) && r < sizeof raw - 1; q++) {
                    if (*q == '%' && isxdigit((unsigned char)q[1]) && isxdigit((unsigned char)q[2])) {
                        char hex[3] = { q[1], q[2], 0 };
                        raw[r++] = (char)strtol(hex, NULL, 16);
                        q += 2;
                    } else raw[r++] = *q;
                }
                {
                    oam_buf b;
                    oam_buf_init(&b);
                    oam_charset_to_utf8(&b, cs, raw, r);
                    snprintf(out, size, "%s", oam_buf_str(&b));
                    oam_buf_free(&b);
                }
                return 1;
            }
            if (*q == '"') {
                q++;
                while (*q && *q != '"' && n < size - 1) {
                    if (*q == '\\' && q[1]) q++;
                    out[n++] = *q++;
                }
            } else {
                while (*q && *q != ';' && !isspace((unsigned char)*q) && n < size - 1) out[n++] = *q++;
            }
            out[n] = 0;
            return 1;
        }
        p = strchr(p, ';');
    }
    return 0;
}

static void fill_part(oam_part *pt, const char *header, size_t hlen, const char *body, size_t blen)
{
    char *ct = oam_hdr_get(header, hlen, "Content-Type");
    char *te = oam_hdr_get(header, hlen, "Content-Transfer-Encoding");
    char *cd = oam_hdr_get(header, hlen, "Content-Disposition");
    char *id = oam_hdr_get(header, hlen, "Content-ID");
    char name[200];
    memset(pt, 0, sizeof *pt);
    if (ct) {
        size_t n = strcspn(ct, "; \t");
        lower_copy(pt->type, sizeof pt->type, ct, n);
        param(ct, "charset", pt->charset, sizeof pt->charset);
    }
    if (!pt->type[0] || !strchr(pt->type, '/')) strcpy(pt->type, "text/plain");
    if (te) {
        const char *s = te;
        while (*s == ' ') s++;
        lower_copy(pt->encoding, sizeof pt->encoding, s, strcspn(s, " ;\t\r\n"));
    }
    name[0] = 0;
    if (cd) {
        const char *s = cd;
        while (*s == ' ') s++;
        if (!strncasecmp(s, "attachment", 10)) pt->attachment = 1;
        param(cd, "filename", name, sizeof name);
    }
    if (!name[0] && ct) param(ct, "name", name, sizeof name);
    if (name[0]) {
        char *d = oam_hdr_decode(name);                 /* RFC 2047 words in names, as some mailers send */
        snprintf(pt->filename, sizeof pt->filename, "%.*s", (int)sizeof pt->filename - 1, d ? d : name);
        free(d);
        if (strncmp(pt->type, "text/", 5)) pt->attachment = 1;
    }
    if (id) {
        const char *s = id, *e;
        while (*s == ' ' || *s == '<') s++;
        size_t n;
        e = s + strcspn(s, "> \t");
        n = (size_t)(e - s) < sizeof pt->cid - 1 ? (size_t)(e - s) : sizeof pt->cid - 1;
        memcpy(pt->cid, s, n);                          /* as written: a cid keeps its case */
        pt->cid[n] = 0;
    }
    pt->body = body;
    pt->len = blen;
    free(ct);
    free(te);
    free(cd);
    free(id);
}

/* The next line of [p, end): its end (before the LF) and the start of the following line. */
static const char *line_end(const char *p, const char *end, const char **next)
{
    const char *lf = memchr(p, '\n', end - p);
    *next = lf ? lf + 1 : end;
    return lf ? (lf > p && lf[-1] == '\r' ? lf - 1 : lf) : end;
}

static int walk(const char *msg, size_t len, oam_part_fn fn, void *user, int depth)
{
    size_t hlen;
    const char *body, *end = msg + len, *p, *next, *part = NULL;
    char boundary[200];
    char *ct;
    oam_part pt;
    if (!split(msg, len, &hlen, &body)) return 0;
    fill_part(&pt, msg, hlen, body, end - body);
    if (strncmp(pt.type, "multipart/", 10) || depth >= MAX_DEPTH) {
        fn(&pt, user);
        return 1;
    }
    ct = oam_hdr_get(msg, hlen, "Content-Type");
    if (!ct || !param(ct, "boundary", boundary, sizeof boundary) || !boundary[0]) {
        free(ct);
        fn(&pt, user);                                  /* a multipart without a boundary: show it as it is */
        return 1;
    }
    free(ct);
    {
        size_t bl = strlen(boundary);
        for (p = body; p < end; p = next) {
            const char *le = line_end(p, end, &next);
            if ((size_t)(le - p) >= bl + 2 && p[0] == '-' && p[1] == '-' && !strncmp(p + 2, boundary, bl)) {
                int last = (size_t)(le - p) >= bl + 4 && p[bl + 2] == '-' && p[bl + 3] == '-';
                if (part) {
                    const char *pe = p;                 /* the line break before the boundary belongs to it */
                    if (pe > part && pe[-1] == '\n') pe--;
                    if (pe > part && pe[-1] == '\r') pe--;
                    walk(part, pe - part, fn, user, depth + 1);
                }
                part = last ? NULL : next;
                if (last) break;
            }
        }
        if (part && part < end) walk(part, end - part, fn, user, depth + 1);   /* no closing boundary */
    }
    return 1;
}

int oam_mime_walk(const char *msg, size_t len, oam_part_fn fn, void *user)
{
    return walk(msg, len, fn, user, 0);
}

static int hexval(int c)
{
    return isdigit(c) ? c - '0' : isxdigit(c) ? tolower(c) - 'a' + 10 : -1;
}

int oam_mime_decode(const oam_part *p, oam_buf *out)
{
    const char *s = p->body, *end = p->body + p->len;
    if (!strcmp(p->encoding, "base64")) return oam_base64_decode(out, s, p->len);
    if (strcmp(p->encoding, "quoted-printable")) return oam_buf_add(out, s, p->len);
    while (s < end) {
        if (*s == '=') {
            if (s + 1 < end && s[1] == '\n') { s += 2; continue; }                /* a soft line break */
            if (s + 2 < end && s[1] == '\r' && s[2] == '\n') { s += 3; continue; }
            if (s + 2 < end && hexval((unsigned char)s[1]) >= 0 && hexval((unsigned char)s[2]) >= 0) {
                oam_buf_addc(out, (char)(hexval((unsigned char)s[1]) * 16 + hexval((unsigned char)s[2])));
                s += 3;
                continue;
            }
        }
        oam_buf_addc(out, *s++);
    }
    return !out->failed;
}

int oam_mime_text(const oam_part *p, oam_buf *utf8)
{
    oam_buf raw, conv;
    size_t i;
    int ok;
    oam_buf_init(&raw);
    oam_buf_init(&conv);
    ok = oam_mime_decode(p, &raw) && oam_charset_to_utf8(&conv, p->charset, oam_buf_str(&raw), raw.len);
    for (i = 0; i < conv.len; i++)
        if (!(conv.data[i] == '\r' && i + 1 < conv.len && conv.data[i + 1] == '\n')) oam_buf_addc(utf8, conv.data[i]);
    oam_buf_free(&raw);
    oam_buf_free(&conv);
    return ok && !utf8->failed;
}

void oam_view_init(oam_view *v)
{
    memset(v, 0, sizeof *v);
    oam_buf_init(&v->text);
    oam_buf_init(&v->links);
    oam_buf_init(&v->html);
}

void oam_view_free(oam_view *v)
{
    oam_buf_free(&v->text);
    oam_buf_free(&v->links);
    oam_buf_free(&v->html);
}

struct pick {
    oam_part plain, html;
    int have_plain, have_html, attachments;
};

static void pick_part(const oam_part *p, void *user)
{
    struct pick *k = user;
    if (p->attachment) { k->attachments++; return; }
    if (!strcmp(p->type, "text/plain") && !k->have_plain) { k->plain = *p; k->have_plain = 1; }
    else if (!strcmp(p->type, "text/html") && !k->have_html) { k->html = *p; k->have_html = 1; }
    else if (strncmp(p->type, "text/", 5) && !p->cid[0]) k->attachments++;   /* inline images with a cid belong to the HTML */
}

/* Addresses written out in plain text: http://, https://, www. */
static void plain_links(const char *s, oam_buf *links)
{
    while (*s) {
        const char *u = NULL, *a = strstr(s, "http://"), *b = strstr(s, "https://"), *e;
        if (a && (!b || a < b)) u = a; else u = b;
        if (!u) break;
        e = u;
        while (*e && !isspace((unsigned char)*e) && *e != '>' && *e != '"' && *e != ')') e++;
        while (e > u && strchr(".,;:!?", e[-1])) e--;                 /* sentence punctuation is not the address's */
        oam_buf_add(links, u, e - u);
        oam_buf_addc(links, '\n');
        s = e;
    }
}

int oam_mime_view(const char *msg, size_t len, oam_view *v)
{
    struct pick k;
    memset(&k, 0, sizeof k);
    if (!oam_mime_walk(msg, len, pick_part, &k)) {
        oam_buf_add(&v->text, msg, len);               /* not a message: show what there is */
        return 1;
    }
    v->attachments = k.attachments;
    if (k.have_html) {
        v->has_html = 1;
        oam_mime_text(&k.html, &v->html);
    }
    if (k.have_plain) {
        oam_mime_text(&k.plain, &v->text);
        plain_links(oam_buf_str(&v->text), &v->links);
    } else if (k.have_html) {
        oam_html_to_text(oam_buf_str(&v->html), v->html.len, &v->text, &v->links);
    }
    return !v->text.failed;
}
