/* acm_text: see acm_text.h. From RFC 5322 (headers, dates, addresses),
 * RFC 2047 (encoded words) and RFC 2231 (charset languages). */
#include "acm_text.h"
#include "acm_base64.h"
#include "acm_charsets.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- UTF-8 ------------------------------------------------------------------ */

static void put_utf8(acm_buf *b, unsigned long c)
{
    if (c < 0x80) acm_buf_addc(b, (char)c);
    else if (c < 0x800) { acm_buf_addc(b, (char)(0xc0 | c >> 6)); acm_buf_addc(b, (char)(0x80 | (c & 63))); }
    else if (c < 0x10000) { acm_buf_addc(b, (char)(0xe0 | c >> 12)); acm_buf_addc(b, (char)(0x80 | (c >> 6 & 63))); acm_buf_addc(b, (char)(0x80 | (c & 63))); }
    else { acm_buf_addc(b, (char)(0xf0 | c >> 18)); acm_buf_addc(b, (char)(0x80 | (c >> 12 & 63))); acm_buf_addc(b, (char)(0x80 | (c >> 6 & 63))); acm_buf_addc(b, (char)(0x80 | (c & 63))); }
}

/* one character from UTF-8: its length in *n, or 0xFFFD and 1 for a bad byte */
static unsigned long get_utf8(const unsigned char *p, size_t left, size_t *n)
{
    unsigned long c = p[0], min;
    size_t len, i;
    if (c < 0x80) { *n = 1; return c; }
    if (c >= 0xc2 && c < 0xe0) { len = 2; c &= 0x1f; min = 0x80; }
    else if (c >= 0xe0 && c < 0xf0) { len = 3; c &= 0x0f; min = 0x800; }
    else if (c >= 0xf0 && c < 0xf5) { len = 4; c &= 0x07; min = 0x10000; }
    else { *n = 1; return 0xfffd; }
    if (len > left) { *n = 1; return 0xfffd; }
    for (i = 1; i < len; i++) {
        if ((p[i] & 0xc0) != 0x80) { *n = 1; return 0xfffd; }
        c = c << 6 | (p[i] & 63);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c < 0xe000)) { *n = 1; return 0xfffd; }
    *n = len;
    return c;
}

int acm_utf8_valid(const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        size_t n;
        if (get_utf8(p + i, len - i, &n) == 0xfffd && n == 1) return 0;     /* a bad byte; a real U+FFFD is 3 */
        i += n;
    }
    return 1;
}

/* ---- charsets ----------------------------------------------------------------- */

static const unsigned short *table_for(const char *cs)
{
    if (!strcmp(cs, "iso-8859-2") || !strcmp(cs, "iso8859-2") || !strcmp(cs, "latin2") || !strcmp(cs, "l2")) return cs_8859_2;
    if (!strcmp(cs, "iso-8859-15") || !strcmp(cs, "iso8859-15") || !strcmp(cs, "latin-9") || !strcmp(cs, "latin9")) return cs_8859_15;
    if (!strcmp(cs, "windows-1250") || !strcmp(cs, "cp1250")) return cs_1250;
    if (!strcmp(cs, "windows-1252") || !strcmp(cs, "cp1252")) return cs_1252;
    return NULL;
}

int acm_charset_to_utf8(acm_buf *out, const char *charset, const char *s, size_t len)
{
    char cs[40];
    size_t i, n = 0;
    const unsigned short *table;
    int is_latin1;
    const unsigned char *p = (const unsigned char *)s;
    if (!charset) charset = "";
    while (*charset == '"' || *charset == ' ') charset++;
    for (; charset[n] && charset[n] != '*' && charset[n] != '"' && n + 1 < sizeof cs; n++) cs[n] = (char)tolower((unsigned char)charset[n]);
    cs[n] = 0;
    table = table_for(cs);
    is_latin1 = !strcmp(cs, "iso-8859-1") || !strcmp(cs, "iso8859-1") || !strcmp(cs, "latin1") || !strcmp(cs, "l1");
    /* UTF-8 as UTF-8; US-ASCII and unknown charsets as UTF-8 when they are
     * valid UTF-8, else as ISO-8859-1 (8-bit bytes in headers mostly are) */
    if (!strcmp(cs, "utf-8") || !strcmp(cs, "utf8") || (!is_latin1 && !table && acm_utf8_valid(s, len))) {
        for (i = 0; i < len;) {                 /* checked one character at a time: bad bytes become U+FFFD */
            size_t k;
            unsigned long c = get_utf8(p + i, len - i, &k);
            put_utf8(out, c);
            i += k;
        }
        return !out->failed;
    }
    for (i = 0; i < len; i++) put_utf8(out, p[i] < 0x80 ? p[i] : table ? table[p[i] - 0x80] : p[i]);
    return !out->failed;
}

char *acm_utf8_to_latin1(const char *utf8)
{
    acm_buf out;
    const unsigned char *p = (const unsigned char *)utf8;
    size_t left = strlen(utf8);
    acm_buf_init(&out);
    while (left) {
        size_t n;
        unsigned long c = get_utf8(p, left, &n);
        p += n;
        left -= n;
        if (c < 0x80 || (c >= 0xa0 && c < 0x100)) { acm_buf_addc(&out, (char)c); continue; }
        if (c >= 0x100 && c < 0x180) {
            if (c == 0x152) acm_buf_adds(&out, "OE");
            else if (c == 0x153) acm_buf_adds(&out, "oe");
            else if (c == 0x132) acm_buf_adds(&out, "IJ");
            else if (c == 0x133) acm_buf_adds(&out, "ij");
            else acm_buf_addc(&out, latin_ext_a_base[c - 0x100]);
            continue;
        }
        switch (c) {
        case 0x2018: case 0x2019: case 0x201a: case 0x2032: case 0x02bc: acm_buf_addc(&out, '\''); break;
        case 0x201c: case 0x201d: case 0x201e: case 0x2033: acm_buf_addc(&out, '"'); break;
        case 0x2013: case 0x2014: case 0x2212: case 0x2010: case 0x2011: acm_buf_addc(&out, '-'); break;
        case 0x2026: acm_buf_adds(&out, "..."); break;
        case 0x20ac: acm_buf_adds(&out, "EUR"); break;
        case 0x2022: acm_buf_addc(&out, (char)0xb7); break;
        case 0x2122: acm_buf_adds(&out, "(TM)"); break;
        case 0x2039: acm_buf_addc(&out, '<'); break;
        case 0x203a: acm_buf_addc(&out, '>'); break;
        case 0x0178: acm_buf_addc(&out, 'Y'); break;
        case 0x2009: case 0x200a: case 0x2002: case 0x2003: case 0x202f: acm_buf_addc(&out, ' '); break;
        case 0x200b: case 0x200c: case 0x200d: case 0xfeff: case 0x00ad: break;      /* invisible: dropped */
        default: acm_buf_addc(&out, '?'); break;
        }
    }
    return acm_buf_take(&out);
}

/* ---- header fields -------------------------------------------------------------- */

char *acm_hdr_get(const char *header, size_t len, const char *name)
{
    size_t nlen = strlen(name), i = 0;
    while (i < len) {
        size_t eol = i;
        while (eol < len && header[eol] != '\n') eol++;
        if (eol - i > nlen && !strncasecmp(header + i, name, nlen) && header[i + nlen] == ':') {
            acm_buf v;
            size_t j = i + nlen + 1;
            acm_buf_init(&v);
            for (;;) {
                size_t end = eol;
                if (end > j && header[end - 1] == '\r') end--;
                acm_buf_add(&v, header + j, end - j);
                if (eol + 1 < len && (header[eol + 1] == ' ' || header[eol + 1] == '\t')) {   /* folded */
                    j = eol + 1;
                    eol = j;
                    while (eol < len && header[eol] != '\n') eol++;
                    continue;
                }
                break;
            }
            {   /* trimmed */
                char *s = acm_buf_take(&v), *a = s, *z;
                if (!s) return NULL;
                while (*a == ' ' || *a == '\t') a++;
                z = a + strlen(a);
                while (z > a && (z[-1] == ' ' || z[-1] == '\t')) z--;
                *z = 0;
                memmove(s, a, (size_t)(z - a) + 1);
                return s;
            }
        }
        i = eol + 1;
    }
    return NULL;
}

/* "=?charset?B|Q?text?=" at p: the decoded word appended as UTF-8, and its length in *used */
static int encoded_word(acm_buf *out, const char *p, size_t *used)
{
    const char *cs = p + 2, *q1, *q2, *end;
    char charset[40], enc;
    acm_buf raw;
    size_t n;
    q1 = strchr(cs, '?');
    if (!q1 || (size_t)(q1 - cs) >= sizeof charset || q1 == cs) return 0;
    enc = (char)toupper((unsigned char)q1[1]);
    if ((enc != 'B' && enc != 'Q') || q1[2] != '?') return 0;
    q2 = q1 + 3;
    end = strstr(q2, "?=");
    if (!end) return 0;
    for (n = 0; q2 + n < end; n++) if (q2[n] == ' ' || q2[n] == '\t') return 0;   /* no spaces inside a word */
    memcpy(charset, cs, (size_t)(q1 - cs));
    charset[q1 - cs] = 0;
    acm_buf_init(&raw);
    if (enc == 'B') {
        if (!acm_base64_decode(&raw, q2, (size_t)(end - q2))) { acm_buf_free(&raw); return 0; }
    } else {
        const char *t;
        for (t = q2; t < end; t++) {
            if (*t == '_') acm_buf_addc(&raw, ' ');
            else if (*t == '=' && t + 2 < end && isxdigit((unsigned char)t[1]) && isxdigit((unsigned char)t[2])) {
                char hex[3] = { t[1], t[2], 0 };
                acm_buf_addc(&raw, (char)strtol(hex, NULL, 16));
                t += 2;
            } else acm_buf_addc(&raw, *t);
        }
    }
    acm_charset_to_utf8(out, charset, raw.data ? raw.data : "", raw.len);
    acm_buf_free(&raw);
    *used = (size_t)(end + 2 - p);
    return 1;
}

char *acm_hdr_decode(const char *value)
{
    acm_buf out, plain, space;
    const char *p = value;
    int after_word = 0;
    acm_buf_init(&out);
    acm_buf_init(&plain);
    acm_buf_init(&space);
    while (*p) {
        size_t used;
        if (p[0] == '=' && p[1] == '?') {
            acm_buf word;
            acm_buf_init(&word);
            if (encoded_word(&word, p, &used)) {
                if (plain.len) acm_charset_to_utf8(&out, NULL, plain.data, plain.len);
                acm_buf_clear(&plain);
                if (!after_word && space.len) acm_buf_add(&out, space.data, space.len);   /* space between two words goes */
                acm_buf_clear(&space);
                acm_buf_add(&out, word.data ? word.data : "", word.len);
                acm_buf_free(&word);
                after_word = 1;
                p += used;
                continue;
            }
            acm_buf_free(&word);
        }
        if (*p == ' ' || *p == '\t') {
            if (plain.len) { acm_charset_to_utf8(&out, NULL, plain.data, plain.len); acm_buf_clear(&plain); after_word = 0; }
            acm_buf_addc(&space, *p++);
            continue;
        }
        if (space.len) { acm_buf_add(&out, space.data, space.len); acm_buf_clear(&space); }
        after_word = 0;
        acm_buf_addc(&plain, *p++);
    }
    if (plain.len) acm_charset_to_utf8(&out, NULL, plain.data, plain.len);
    if (space.len) acm_buf_add(&out, space.data, space.len);
    acm_buf_free(&plain);
    acm_buf_free(&space);
    return acm_buf_take(&out);
}

/* ---- addresses -------------------------------------------------------------------- */

static char *trimmed(const char *a, const char *z)
{
    char *s;
    while (a < z && (*a == ' ' || *a == '\t')) a++;
    while (z > a && (z[-1] == ' ' || z[-1] == '\t')) z--;
    s = malloc((size_t)(z - a) + 1);
    if (s) { memcpy(s, a, (size_t)(z - a)); s[z - a] = 0; }
    return s;
}

/* a display name: quotes and escapes gone, encoded words decoded */
static char *display_name(const char *a, const char *z)
{
    acm_buf b;
    char *raw, *decoded;
    acm_buf_init(&b);
    for (; a < z; a++) {
        if (*a == '"') continue;
        if (*a == '\\' && a + 1 < z) a++;
        acm_buf_addc(&b, *a);
    }
    raw = acm_buf_take(&b);
    if (!raw) return NULL;
    decoded = acm_hdr_decode(raw);
    free(raw);
    if (decoded) {
        char *t = trimmed(decoded, decoded + strlen(decoded));
        free(decoded);
        return t;
    }
    return NULL;
}

int acm_hdr_address(const char *value, char **name, char **address)
{
    const char *p = value, *end, *lt = NULL, *gt = NULL, *cl = NULL, *cr = NULL;
    int quoted = 0, depth = 0;
    *name = *address = NULL;
    /* the first mailbox ends at a comma outside quotes, <> and () */
    for (end = p; *end; end++) {
        if (*end == '\\' && end[1]) { end++; continue; }
        if (*end == '"') quoted = !quoted;
        else if (quoted) continue;
        else if (*end == '(') { if (!depth++ && !cl) cl = end; }
        else if (*end == ')') { if (depth && !--depth && !cr) cr = end; }
        else if (!depth && *end == '<' && !lt) lt = end;
        else if (!depth && *end == '>' && lt && !gt) gt = end;
        else if (!depth && *end == ',') break;
        else if (!depth && *end == ':' && !lt) p = end + 1;       /* "group: a@b, ..." */
    }
    if (lt && gt) {
        *address = trimmed(lt + 1, gt);
        *name = display_name(p, lt);
    } else {
        const char *a = p, *z = cl && cl < end ? cl : end;
        *address = trimmed(a, z);
        *name = cl && cr && cr < end ? display_name(cl + 1, cr) : trimmed("", "");
    }
    if (*address && **address && (*address)[strlen(*address) - 1] == ';') (*address)[strlen(*address) - 1] = 0;
    return *name && *address && **address;
}

/* ---- dates --------------------------------------------------------------------------- */

static long long days_from_civil(long long y, unsigned m, unsigned d)
{
    long long era;
    unsigned yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long long)doe - 719468;
}

int acm_hdr_date(const char *value, long long *when, int *zone)
{
    static const char months[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    const char *p = value;
    long day, year, hh, mm, ss = 0, tz = 0;
    int mon = -1, i;
    char *e;
    while (*p == ' ') p++;
    if (isalpha((unsigned char)*p)) {          /* "Thu," */
        while (*p && *p != ',' && *p != ' ') p++;
        while (*p == ',' || *p == ' ') p++;
    }
    day = strtol(p, &e, 10);
    if (e == p || day < 1 || day > 31) return 0;
    p = e;
    while (*p == ' ' || *p == '-') p++;
    for (i = 0; i < 12; i++) if (!strncasecmp(p, months + 3 * i, 3)) mon = i;
    if (mon < 0) return 0;
    p += 3;
    while (*p == ' ' || *p == '-') p++;
    year = strtol(p, &e, 10);
    if (e == p) return 0;
    if (e - p <= 2) year += year < 50 ? 2000 : 1900;
    else if (e - p == 3) year += 1900;
    p = e;
    hh = strtol(p, &e, 10);
    if (e == p || *e != ':') return 0;
    mm = strtol(e + 1, &e, 10);
    if (*e == ':') ss = strtol(e + 1, &e, 10);
    p = e;
    while (*p == ' ') p++;
    if (*p == '+' || *p == '-') {
        long v = strtol(p + 1, NULL, 10);
        tz = (v / 100) * 60 + v % 100;
        if (*p == '-') tz = -tz;
    } else {
        static const struct { const char *name; int minutes; } zones[] = {
            { "UT", 0 }, { "GMT", 0 }, { "Z", 0 }, { "EST", -300 }, { "EDT", -240 }, { "CST", -360 }, { "CDT", -300 },
            { "MST", -420 }, { "MDT", -360 }, { "PST", -480 }, { "PDT", -420 }
        };
        for (i = 0; i < (int)(sizeof zones / sizeof *zones); i++)
            if (!strncasecmp(p, zones[i].name, strlen(zones[i].name)) && !isalpha((unsigned char)p[strlen(zones[i].name)])) tz = zones[i].minutes;
    }
    if (hh > 23 || mm > 59 || ss > 60) return 0;
    *when = days_from_civil(year, (unsigned)mon + 1, (unsigned)day) * 86400 + hh * 3600 + mm * 60 + ss - tz * 60;
    if (zone) *zone = (int)tz;
    return 1;
}
