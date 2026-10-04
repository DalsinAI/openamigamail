#include "oam_html.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct out {
    oam_buf *text, *links;
    int nlinks, pending;        /* pending: line breaks owed before the next text (1 or 2) */
    int space;                  /* a space is owed */
    int at_start;               /* nothing written yet */
    int pre;                    /* inside <pre> */
    int bol;                    /* at the beginning of a line: whitespace is dropped */
    char href[512];             /* the link being written, if any */
};

static void utf8(oam_buf *b, unsigned long c)
{
    char s[4];
    if (c < 0x80) { oam_buf_addc(b, (char)c); return; }
    if (c < 0x800) { s[0] = 0xc0 | (c >> 6); s[1] = 0x80 | (c & 63); oam_buf_add(b, s, 2); return; }
    if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
    if (c < 0x10000) { s[0] = 0xe0 | (c >> 12); s[1] = 0x80 | ((c >> 6) & 63); s[2] = 0x80 | (c & 63); oam_buf_add(b, s, 3); return; }
    if (c > 0x10ffff) { utf8(b, 0xfffd); return; }
    s[0] = 0xf0 | (c >> 18); s[1] = 0x80 | ((c >> 12) & 63); s[2] = 0x80 | ((c >> 6) & 63); s[3] = 0x80 | (c & 63);
    oam_buf_add(b, s, 4);
}

static void brk(struct out *o, int n)
{
    if (!o->at_start && n > o->pending) o->pending = n;
    o->space = 0;
}

/* Text about to be written: the breaks and the space owed first. */
static void flush(struct out *o)
{
    if (o->pending) {
        oam_buf_add(o->text, "\n\n", o->pending);
        o->pending = 0;
    } else if (o->space) {
        oam_buf_addc(o->text, ' ');
    }
    o->space = 0;
    o->at_start = 0;
    o->bol = 0;
}

static void put(struct out *o, const char *s, size_t n)
{
    flush(o);
    oam_buf_add(o->text, s, n);
}

/* The common named entities; anything else is numeric or left as written. */
static const struct { const char *name; unsigned short code; } entity[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xa0 },
    { "copy", 0xa9 }, { "reg", 0xae }, { "trade", 0x2122 }, { "euro", 0x20ac }, { "pound", 0xa3 },
    { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "bull", 0x2022 },
    { "lsquo", 0x2018 }, { "rsquo", 0x2019 }, { "ldquo", 0x201c }, { "rdquo", 0x201d },
};

/* An entity at s (after '&'): its code point and length, or 0. */
static size_t decode_entity(const char *s, const char *end, unsigned long *code)
{
    const char *p = s;
    size_t i, n;
    if (p < end && *p == '#') {
        int hex = p + 1 < end && (p[1] == 'x' || p[1] == 'X');
        unsigned long v = 0;
        p += hex ? 2 : 1;
        n = 0;
        while (p < end && (hex ? isxdigit((unsigned char)*p) : isdigit((unsigned char)*p)) && n < 8) {
            v = v * (hex ? 16 : 10) + (isdigit((unsigned char)*p) ? *p - '0' : (tolower((unsigned char)*p) - 'a' + 10));
            p++; n++;
        }
        if (!n) return 0;
        if (p < end && *p == ';') p++;
        *code = v ? v : 0xfffd;
        return p - s;
    }
    for (i = 0; i < sizeof entity / sizeof entity[0]; i++) {
        n = strlen(entity[i].name);
        if ((size_t)(end - s) > n && !strncmp(s, entity[i].name, n) && s[n] == ';') {
            *code = entity[i].code;
            return n + 1;
        }
    }
    return 0;
}

static void text_run(struct out *o, const char *s, const char *end)
{
    while (s < end) {
        unsigned long c;
        size_t n;
        if (*s == '&' && (n = decode_entity(s + 1, end, &c))) {
            s += 1 + n;
            if (c == 0xa0 && !o->pre) { o->space = 1; continue; }
            flush(o);
            utf8(o->text, c);
        } else if (!o->pre && isspace((unsigned char)*s)) {
            if (!o->at_start && !o->pending && !o->bol) o->space = 1;
            s++;
        } else if (o->pre && *s == '\n') {
            flush(o);
            oam_buf_addc(o->text, '\n');
            s++;
        } else {
            const char *q = s + 1;              /* at least one: a '&' that is no entity is text */
            while (q < end && *q != '&' && !(!o->pre && isspace((unsigned char)*q)) && !(o->pre && *q == '\n')) q++;
            put(o, s, q - s);
            s = q;
        }
    }
}

/* attr="value" (or 'value' or bare) of a tag's text, into out. */
static int attribute(const char *tag, const char *end, const char *name, char *out, size_t size)
{
    size_t nl = strlen(name);
    const char *p = tag;
    while (p < end) {
        while (p < end && !isspace((unsigned char)*p)) p++;
        while (p < end && isspace((unsigned char)*p)) p++;
        if ((size_t)(end - p) > nl && !strncasecmp(p, name, nl) && (p[nl] == '=' || isspace((unsigned char)p[nl]))) {
            const char *v = p + nl, *ve;
            while (v < end && isspace((unsigned char)*v)) v++;
            if (v >= end || *v != '=') continue;
            v++;
            while (v < end && isspace((unsigned char)*v)) v++;
            if (v < end && (*v == '"' || *v == '\'')) {
                char q = *v++;
                ve = memchr(v, q, end - v);
                if (!ve) ve = end;
            } else {
                ve = v;
                while (ve < end && !isspace((unsigned char)*ve)) ve++;
            }
            if ((size_t)(ve - v) >= size) ve = v + size - 1;
            memcpy(out, v, ve - v);
            out[ve - v] = 0;
            return 1;
        }
    }
    return 0;
}

static int is(const char *name, size_t n, const char *want)
{
    return strlen(want) == n && !strncasecmp(name, want, n);
}

int oam_html_to_text(const char *html, size_t len, oam_buf *text, oam_buf *links)
{
    const char *s = html, *end = html + len;
    struct out o;
    memset(&o, 0, sizeof o);
    o.text = text;
    o.links = links;
    o.at_start = 1;
    while (s < end) {
        const char *lt = memchr(s, '<', end - s), *gt, *name, *ne;
        int closing;
        size_t n;
        if (!lt) { text_run(&o, s, end); break; }
        text_run(&o, s, lt);
        if (lt + 3 < end && !strncmp(lt, "<!--", 4)) {             /* a comment */
            const char *c = lt + 4;
            while (c + 2 < end && strncmp(c, "-->", 3)) c++;
            s = c + 2 < end ? c + 3 : end;
            continue;
        }
        gt = memchr(lt, '>', end - lt);
        if (!gt) { text_run(&o, lt, end); break; }
        s = gt + 1;
        name = lt + 1;
        closing = name < gt && *name == '/';
        if (closing) name++;
        ne = name;
        while (ne < gt && (isalnum((unsigned char)*ne))) ne++;
        n = ne - name;
        if (!n) continue;                                          /* <!DOCTYPE>, <?xml?> */
        if (!closing && (is(name, n, "head") || is(name, n, "script") || is(name, n, "style") || is(name, n, "title"))) {
            char close[16];                                        /* skip to its end tag */
            const char *c = s;
            close[0] = '<'; close[1] = '/';
            memcpy(close + 2, name, n < 12 ? n : 12);
            close[2 + (n < 12 ? n : 12)] = 0;
            while (c < end && strncasecmp(c, close, strlen(close))) c++;
            c = memchr(c, '>', end - c);
            s = c ? c + 1 : end;
            continue;
        }
        if (is(name, n, "br")) {
            if (o.pending < 1) { flush(&o); oam_buf_addc(text, '\n'); o.bol = 1; }
            o.space = 0;
        } else if (is(name, n, "p") || is(name, n, "div") || is(name, n, "table") || is(name, n, "blockquote") ||
                   is(name, n, "ul") || is(name, n, "ol") || is(name, n, "h1") || is(name, n, "h2") ||
                   is(name, n, "h3") || is(name, n, "h4") || is(name, n, "h5") || is(name, n, "h6") ||
                   is(name, n, "section") || is(name, n, "article") || is(name, n, "header") || is(name, n, "footer")) {
            brk(&o, 2);
        } else if (is(name, n, "tr")) {
            brk(&o, 1);
        } else if (is(name, n, "td") || is(name, n, "th")) {
            if (!closing && !o.at_start && !o.pending) o.space = 1;
        } else if (is(name, n, "li") && !closing) {
            brk(&o, 1);
            put(&o, "- ", 2);
        } else if (is(name, n, "hr")) {
            brk(&o, 1);
            put(&o, "----", 4);
            brk(&o, 1);
        } else if (is(name, n, "pre")) {
            brk(&o, 2);
            o.pre = !closing;
        } else if (is(name, n, "a")) {
            if (!closing) {
                o.href[0] = 0;
                attribute(name, gt, "href", o.href, sizeof o.href);
            } else if (o.href[0] && o.href[0] != '#') {           /* in-page anchors are not links to show */
                char num[16];
                o.nlinks++;
                oam_buf_printf(links, "%s\n", o.href);
                snprintf(num, sizeof num, " [%d]", o.nlinks);
                put(&o, num, strlen(num));
                o.href[0] = 0;
            }
        } else if (is(name, n, "img") && !closing) {
            char alt[128];
            if (attribute(name, gt, "alt", alt, sizeof alt) && alt[0]) {
                put(&o, "[", 1);
                text_run(&o, alt, alt + strlen(alt));
                put(&o, "]", 1);
            }
        }
    }
    return !text->failed && !links->failed;
}
