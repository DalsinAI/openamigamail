/* km_imap: see km_imap.h. Written for KyneMail from RFC 3501 (IMAP4rev1),
 * RFC 4959 (SASL-IR), RFC 4616 (PLAIN) and Google's XOAUTH2 notes. */
#include "km_imap.h"
#include "km_base64.h"
#include "km_sasl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RESPONSE (64ul * 1024 * 1024)   /* one response, literals included */
#define MAX_LINE (1024ul * 1024)

/* ---- a parsed response ---------------------------------------------------- */

enum { IT_ATOM, IT_STRING, IT_NIL, IT_LIST };

typedef struct item {
    int kind;
    char *s;               /* atoms and strings: the text, NUL-terminated (strings may hold NULs: len) */
    size_t len;
    struct item *child;    /* a list's first member */
    struct item *next;
} item;

enum { R_DATA, R_TAGGED, R_CONTINUE };

typedef struct resp {
    km_buf raw;
    int kind;
    char tag[24];
    char status[12];       /* OK, NO, BAD, BYE, PREAUTH for status responses; "" otherwise */
    char *code;            /* the [response code] of a status response, or NULL */
    char *text;            /* a status response's text; a continuation's text */
    unsigned long num;     /* "* 12 EXISTS": 12 */
    char name[24];         /* "* 12 EXISTS": EXISTS; "* LIST ...": LIST */
    item *items;           /* a data response's values after its name */
} resp;

static void free_items(item *it)
{
    while (it) {
        item *next = it->next;
        free_items(it->child);
        free(it->s);
        free(it);
        it = next;
    }
}

static void resp_init(resp *r) { memset(r, 0, sizeof *r); km_buf_init(&r->raw); }

static void resp_free(resp *r)
{
    km_buf_free(&r->raw);
    free(r->code);
    free(r->text);
    free_items(r->items);
    resp_init(r);
}

static item *new_item(int kind, const char *s, size_t len)
{
    item *it = calloc(1, sizeof *it);
    if (!it) return NULL;
    it->kind = kind;
    if (kind != IT_LIST) {
        it->s = malloc(len + 1);
        if (!it->s) { free(it); return NULL; }
        if (len) memcpy(it->s, s, len);
        it->s[len] = 0;
        it->len = len;
    }
    return it;
}

/* values from *pp up to the end, or to the ')' closing the list being read */
static item *parse_items(const char **pp, const char *end, int in_list, int *bad)
{
    item *first = NULL, **link = &first;
    const char *p = *pp;
    while (!*bad) {
        item *it = NULL;
        while (p < end && *p == ' ') p++;
        if (p >= end) break;
        if (*p == ')') { if (in_list) break; *bad = 1; break; }
        if (*p == '(') {
            p++;
            it = new_item(IT_LIST, NULL, 0);
            if (!it) { *bad = 1; break; }
            it->child = parse_items(&p, end, 1, bad);
            if (p >= end || *p != ')') *bad = 1;
            else p++;
        } else if (*p == '"') {
            km_buf s;
            km_buf_init(&s);
            for (p++; p < end && *p != '"'; p++) {
                if (*p == '\\' && p + 1 < end) p++;
                km_buf_addc(&s, *p);
            }
            if (p >= end) *bad = 1;
            else p++;
            it = new_item(IT_STRING, km_buf_str(&s), s.len);
            km_buf_free(&s);
        } else if (*p == '{') {
            unsigned long n = 0;
            const char *q = p + 1;
            while (q < end && isdigit((unsigned char)*q)) n = n * 10 + (unsigned long)(*q++ - '0');
            if (q < end && *q == '+') q++;
            if (q + 2 >= end + 0 || *q != '}' || q[1] != '\r' || q[2] != '\n' || (unsigned long)(end - (q + 3)) < n) { *bad = 1; break; }
            q += 3;
            it = new_item(IT_STRING, q, n);
            p = q + n;
        } else {
            const char *start = p;
            int depth = 0;
            while (p < end) {
                if (*p == '[') depth++;
                else if (*p == ']' && depth) depth--;
                else if (!depth && (*p == ' ' || *p == '(' || *p == ')')) break;
                p++;
            }
            if (p - start == 3 && !strncasecmp(start, "NIL", 3)) it = new_item(IT_NIL, "", 0);
            else it = new_item(IT_ATOM, start, (size_t)(p - start));
        }
        if (!it) { *bad = 1; break; }
        *link = it;
        link = &it->next;
    }
    *pp = p;
    return first;
}

static char *dup_range(const char *s, size_t n)
{
    char *d = malloc(n + 1);
    if (d) { memcpy(d, s, n); d[n] = 0; }
    return d;
}

static int is_status(const char *w, size_t n)
{
    static const char *words[] = { "OK", "NO", "BAD", "BYE", "PREAUTH" };
    size_t i;
    for (i = 0; i < sizeof words / sizeof *words; i++)
        if (strlen(words[i]) == n && !strncasecmp(w, words[i], n)) return 1;
    return 0;
}

/* "[code] text" after a status word */
static void parse_status_text(resp *r, const char *p, const char *end)
{
    while (p < end && *p == ' ') p++;
    if (p < end && *p == '[') {
        const char *q = memchr(p, ']', (size_t)(end - p));
        if (q) {
            r->code = dup_range(p + 1, (size_t)(q - p - 1));
            p = q + 1;
            while (p < end && *p == ' ') p++;
        }
    }
    r->text = dup_range(p, (size_t)(end - p));
}

static int parse_response(resp *r)
{
    const char *p = km_buf_str(&r->raw), *end = p + r->raw.len, *w;
    int bad = 0;
    w = p;
    while (p < end && *p != ' ') p++;
    if ((size_t)(p - w) >= sizeof r->tag) return 0;
    memcpy(r->tag, w, (size_t)(p - w));
    r->tag[p - w] = 0;
    if (p < end) p++;
    if (!strcmp(r->tag, "+")) {
        r->kind = R_CONTINUE;
        r->text = dup_range(p, (size_t)(end - p));
        return 1;
    }
    r->kind = strcmp(r->tag, "*") ? R_TAGGED : R_DATA;
    if (r->kind == R_DATA && p < end && isdigit((unsigned char)*p)) {
        while (p < end && isdigit((unsigned char)*p)) r->num = r->num * 10 + (unsigned long)(*p++ - '0');
        while (p < end && *p == ' ') p++;
    }
    w = p;
    while (p < end && *p != ' ' && *p != '(') p++;
    if ((size_t)(p - w) >= sizeof r->name) return 0;
    if (is_status(w, (size_t)(p - w))) {
        memcpy(r->status, w, (size_t)(p - w));
        r->status[p - w] = 0;
        parse_status_text(r, p, end);
        return 1;
    }
    if (r->kind == R_TAGGED) return 0;
    memcpy(r->name, w, (size_t)(p - w));
    r->name[p - w] = 0;
    r->items = parse_items(&p, end, 0, &bad);
    return !bad;
}

/* one whole response: its lines and the literals between them */
static int read_response(km_imap *m, resp *r)
{
    km_buf line;
    int ok = 0;
    resp_free(r);
    km_buf_init(&line);
    for (;;) {
        const char *s, *open;
        unsigned long n = 0;
        if (!km_io_readline(&m->io, &line, MAX_LINE)) goto out;
        if (m->trace) m->trace(m->trace_user, 0, km_buf_str(&line));
        if (!km_buf_add(&r->raw, line.data ? line.data : "", line.len)) goto out;
        s = km_buf_str(&line);
        if (!line.len || s[line.len - 1] != '}') break;
        open = strrchr(s, '{');
        if (!open) break;
        {
            const char *q = open + 1;
            if (!isdigit((unsigned char)*q)) break;
            while (isdigit((unsigned char)*q)) n = n * 10 + (unsigned long)(*q++ - '0');
            if (*q == '+') q++;
            if (*q != '}' || q + 1 != s + line.len) break;
        }
        if (r->raw.len + n > MAX_RESPONSE) goto out;
        if (!km_buf_add(&r->raw, "\r\n", 2) || !km_io_readn(&m->io, &r->raw, n)) goto out;
    }
    ok = parse_response(r);
out:
    km_buf_free(&line);
    if (!ok && !m->err.len) km_buf_adds(&m->err, m->io.closed ? "The server closed the connection." : "The server's answer could not be read.");
    return ok;
}

/* ---- capabilities and state ------------------------------------------------ */

static void set_caps(km_imap *m, const char *list)
{
    const char *p = list;
    km_buf_clear(&m->caps);
    km_buf_addc(&m->caps, ' ');
    for (; *p; p++) km_buf_addc(&m->caps, (char)toupper((unsigned char)*p));
    km_buf_addc(&m->caps, ' ');
}

int km_imap_has(km_imap *m, const char *capability)
{
    char want[64];
    size_t i, n = strlen(capability);
    if (n + 3 > sizeof want) return 0;
    want[0] = ' ';
    for (i = 0; i < n; i++) want[i + 1] = (char)toupper((unsigned char)capability[i]);
    want[n + 1] = ' ';
    want[n + 2] = 0;
    return strstr(km_buf_str(&m->caps), want) != NULL;
}

static void caps_from_items(km_imap *m, item *it)
{
    km_buf list;
    km_buf_init(&list);
    for (; it; it = it->next)
        if (it->kind == IT_ATOM) { if (list.len) km_buf_addc(&list, ' '); km_buf_adds(&list, it->s); }
    set_caps(m, km_buf_str(&list));
    km_buf_free(&list);
}

static void status_code(km_imap *m, const char *code)
{
    if (!code) return;
    if (!strncasecmp(code, "CAPABILITY ", 11)) set_caps(m, code + 11);
    else if (!strncasecmp(code, "UIDVALIDITY ", 12)) m->uidvalidity = strtoul(code + 12, NULL, 10);
    else if (!strncasecmp(code, "UIDNEXT ", 8)) m->uidnext = strtoul(code + 8, NULL, 10);
    else if (!strcasecmp(code, "READ-ONLY")) m->readonly = 1;
    else if (!strcasecmp(code, "READ-WRITE")) m->readonly = 0;
}

/* untagged data every command may bring */
static void common_data(km_imap *m, resp *r)
{
    if (r->status[0]) status_code(m, r->code);
    if (!strcasecmp(r->name, "CAPABILITY")) caps_from_items(m, r->items);
    else if (!strcasecmp(r->name, "EXISTS")) m->exists = r->num;
    else if (!strcasecmp(r->name, "RECENT")) m->recent = r->num;
    else if (!strcasecmp(r->name, "EXPUNGE") && m->exists) m->exists--;
}

/* ---- commands ------------------------------------------------------------ */

typedef int (*on_data_fn)(km_imap *m, resp *r, void *user);
typedef int (*on_continue_fn)(km_imap *m, resp *r, void *user);   /* 0 ends the command (the connection is then unusable) */

static int send_line(km_imap *m, const char *line, size_t len)
{
    if (m->trace) {
        if (m->hide_next) {
            /* the tag and command words stay; the secret goes */
            const char *sp = memchr(line, ' ', len);
            const char *sp2 = sp ? memchr(sp + 1, ' ', len - (size_t)(sp + 1 - line)) : NULL;
            km_buf shown;
            km_buf_init(&shown);
            if (sp2) { const char *sp3 = memchr(sp2 + 1, ' ', len - (size_t)(sp2 + 1 - line)); km_buf_add(&shown, line, (size_t)((sp3 ? sp3 : sp2) - line)); }
            km_buf_adds(&shown, " [hidden]");
            m->trace(m->trace_user, 1, km_buf_str(&shown));
            km_buf_free(&shown);
        } else {
            km_buf shown;
            km_buf_init(&shown);
            km_buf_add(&shown, line, len >= 2 ? len - 2 : len);
            m->trace(m->trace_user, 1, km_buf_str(&shown));
            km_buf_free(&shown);
        }
    }
    m->hide_next = 0;
    if (!km_io_write(&m->io, line, len)) {
        km_buf_clear(&m->err);
        km_buf_adds(&m->err, "Sending to the server failed.");
        return 0;
    }
    return 1;
}

static int command(km_imap *m, const char *cmd, int secret, on_data_fn on_data, on_continue_fn on_continue, void *user)
{
    char tag[16];
    km_buf line;
    resp r;
    int result = 0;
    if (!m->conn) { km_buf_clear(&m->err); km_buf_adds(&m->err, "Not connected."); return 0; }
    km_buf_clear(&m->err);
    snprintf(tag, sizeof tag, "A%04u", ++m->tagn);
    km_buf_init(&line);
    km_buf_printf(&line, "%s %s\r\n", tag, cmd);
    m->hide_next = secret;
    if (line.failed || !send_line(m, line.data, line.len)) { km_buf_free(&line); return 0; }
    km_buf_free(&line);
    resp_init(&r);
    for (;;) {
        if (!read_response(m, &r)) break;
        if (r.kind == R_CONTINUE) {
            if (!on_continue || !on_continue(m, &r, user)) {
                if (!m->err.len) km_buf_adds(&m->err, "The server asked for more than the command had.");
                break;
            }
            continue;
        }
        if (r.kind == R_DATA) {
            common_data(m, &r);
            if (!strcasecmp(r.status, "BYE")) {
                km_buf_clear(&m->err);
                km_buf_printf(&m->err, "The server ended the session: %s", r.text ? r.text : "");
            }
            if (on_data && !on_data(m, &r, user)) {
                if (!m->err.len) km_buf_adds(&m->err, "The server's answer was not understood.");
            }
            continue;
        }
        if (strcmp(r.tag, tag)) continue;           /* not ours */
        status_code(m, r.code);
        if (!strcasecmp(r.status, "OK")) result = !m->err.len;
        else {
            km_buf_clear(&m->err);
            if (r.code) km_buf_printf(&m->err, "[%s] ", r.code);
            km_buf_adds(&m->err, r.text && *r.text ? r.text : "The server refused the command.");
        }
        break;
    }
    resp_free(&r);
    return result;
}

/* a quoted string for a command, or 0 if it has to be a literal */
static int add_quoted(km_buf *b, const char *s)
{
    const char *p;
    for (p = s; *p; p++) if ((unsigned char)*p < 32 || (unsigned char)*p > 126) return 0;
    km_buf_addc(b, '"');
    for (p = s; *p; p++) {
        if (*p == '"' || *p == '\\') km_buf_addc(b, '\\');
        km_buf_addc(b, *p);
    }
    km_buf_addc(b, '"');
    return !b->failed;
}

/* ---- connecting and logging in --------------------------------------------- */

void km_imap_init(km_imap *m)
{
    memset(m, 0, sizeof *m);
    km_buf_init(&m->caps);
    km_buf_init(&m->err);
}

const char *km_imap_error(km_imap *m) { return m->err.len ? km_buf_str(&m->err) : "No error."; }

static int greeting(km_imap *m)
{
    resp r;
    int ok = 0;
    resp_init(&r);
    if (read_response(m, &r) && r.kind == R_DATA && r.status[0]) {
        status_code(m, r.code);
        if (!strcasecmp(r.status, "OK")) ok = 1;
        else if (!strcasecmp(r.status, "PREAUTH")) ok = m->logged_in = 1;
        else { km_buf_clear(&m->err); km_buf_printf(&m->err, "The server turned the connection away: %s", r.text ? r.text : ""); }
    } else if (!m->err.len) km_buf_adds(&m->err, "The server did not greet as an IMAP server.");
    resp_free(&r);
    return ok;
}

int km_imap_connect(km_imap *m, const char *host, int port, int security)
{
    char err[256];
    km_imap_close(m);
    km_buf_clear(&m->err);
    err[0] = 0;
    m->conn = km_net_connect(host, port, security == KM_IMAP_TLS, err, sizeof err);
    if (!m->conn) { km_buf_adds(&m->err, err[0] ? err : "The connection failed."); return 0; }
    km_io_init(&m->io, m->conn);
    if (!greeting(m)) { km_imap_close(m); return 0; }
    if (!m->caps.len && !command(m, "CAPABILITY", 0, NULL, NULL, NULL)) { km_imap_close(m); return 0; }
    if (security == KM_IMAP_STARTTLS) {
        if (!km_imap_has(m, "STARTTLS")) {
            km_buf_clear(&m->err);
            km_buf_adds(&m->err, "The server does not offer STARTTLS, so the connection would not be private.");
            km_imap_close(m);
            return 0;
        }
        if (!command(m, "STARTTLS", 0, NULL, NULL, NULL)) { km_imap_close(m); return 0; }
        if (m->io.pos < m->io.end) {      /* nothing may come between STARTTLS and the handshake */
            km_buf_clear(&m->err);
            km_buf_adds(&m->err, "The server sent data before TLS started.");
            km_imap_close(m);
            return 0;
        }
        err[0] = 0;
        if (!km_net_starttls(m->conn, host, err, sizeof err)) {
            km_buf_clear(&m->err);
            km_buf_adds(&m->err, err[0] ? err : "TLS could not be started.");
            km_imap_close(m);
            return 0;
        }
        km_buf_clear(&m->caps);          /* what the server offered before TLS no longer counts */
        if (!command(m, "CAPABILITY", 0, NULL, NULL, NULL)) { km_imap_close(m); return 0; }
    }
    return 1;
}

typedef struct sasl_state { const char *response; int sent; km_buf server_error; } sasl_state;

static int sasl_continue(km_imap *m, resp *r, void *user)
{
    sasl_state *st = user;
    km_buf line;
    int ok;
    km_buf_init(&line);
    if (!st->sent) {                      /* the server is ready for the initial response */
        st->sent = 1;
        km_buf_printf(&line, "%s\r\n", st->response);
        m->hide_next = 1;
    } else {                              /* a challenge after the response: XOAUTH2's error report */
        if (r->text) km_base64_decode(&st->server_error, r->text, strlen(r->text));
        km_buf_adds(&line, "\r\n");
    }
    if (m->hide_next && m->trace) { m->trace(m->trace_user, 1, "[hidden]"); m->hide_next = 0; ok = km_io_write(&m->io, line.data, line.len); }
    else ok = send_line(m, line.data, line.len);
    km_buf_free(&line);
    return ok;
}

static int authenticate(km_imap *m, const char *mechanism, const char *response)
{
    sasl_state st;
    km_buf cmd;
    int ok;
    memset(&st, 0, sizeof st);
    km_buf_init(&st.server_error);
    st.response = response;
    km_buf_init(&cmd);
    if (km_imap_has(m, "SASL-IR")) {
        km_buf_printf(&cmd, "AUTHENTICATE %s %s", mechanism, response);
        st.sent = 1;
    } else km_buf_printf(&cmd, "AUTHENTICATE %s", mechanism);
    ok = !cmd.failed && command(m, km_buf_str(&cmd), st.sent, NULL, sasl_continue, &st);
    if (cmd.data) memset(cmd.data, 0, cmd.len);
    km_buf_free(&cmd);
    if (!ok && st.server_error.len) km_buf_printf(&m->err, " (%s)", km_buf_str(&st.server_error));
    km_buf_free(&st.server_error);
    return ok;
}

static int after_login(km_imap *m, int ok)
{
    if (!ok) return 0;
    m->logged_in = 1;
    /* capabilities can change once logged in; ask unless the OK said */
    return command(m, "CAPABILITY", 0, NULL, NULL, NULL) || 1;
}

int km_imap_login(km_imap *m, const char *user, const char *password)
{
    km_buf b;
    int ok;
    km_buf_init(&b);
    if (km_imap_has(m, "AUTH=PLAIN")) {
        if (!km_sasl_plain(&b, user, password)) {
            km_buf_free(&b);
            km_buf_clear(&m->err);
            km_buf_adds(&m->err, "The user name or password has a character that cannot be sent.");
            return 0;
        }
        ok = authenticate(m, "PLAIN", km_buf_str(&b));
    } else if (km_imap_has(m, "LOGINDISABLED")) {
        km_buf_free(&b);
        km_buf_clear(&m->err);
        km_buf_adds(&m->err, "The server does not allow a password login on this connection. Use TLS.");
        return 0;
    } else {
        km_buf_adds(&b, "LOGIN ");
        if (!add_quoted(&b, user) || !km_buf_addc(&b, ' ') || !add_quoted(&b, password)) {
            if (b.data) memset(b.data, 0, b.len);
            km_buf_free(&b);
            km_buf_clear(&m->err);
            km_buf_adds(&m->err, "The user name or password has a character that LOGIN cannot carry.");
            return 0;
        }
        ok = command(m, km_buf_str(&b), 1, NULL, NULL, NULL);
    }
    if (b.data) memset(b.data, 0, b.len);
    km_buf_free(&b);
    return after_login(m, ok);
}

int km_imap_login_xoauth2(km_imap *m, const char *user, const char *token)
{
    km_buf b;
    int ok;
    if (!km_imap_has(m, "AUTH=XOAUTH2")) {
        km_buf_clear(&m->err);
        km_buf_adds(&m->err, "The server does not offer OAuth sign-in (XOAUTH2).");
        return 0;
    }
    km_buf_init(&b);
    if (!km_sasl_xoauth2(&b, user, token)) {
        km_buf_free(&b);
        km_buf_clear(&m->err);
        km_buf_adds(&m->err, "The user name or token has a character that cannot be sent.");
        return 0;
    }
    ok = authenticate(m, "XOAUTH2", km_buf_str(&b));
    if (b.data) memset(b.data, 0, b.len);
    km_buf_free(&b);
    return after_login(m, ok);
}

/* ---- folders --------------------------------------------------------------- */

typedef struct list_state { km_imap_folder *v; int n, cap; } list_state;

static int on_list(km_imap *m, resp *r, void *user)
{
    list_state *st = user;
    item *attrs = r->items, *delim, *name;
    km_imap_folder *f;
    (void)m;
    if (strcasecmp(r->name, "LIST")) return 1;
    if (!attrs || attrs->kind != IT_LIST || !(delim = attrs->next) || !(name = delim->next)) return 0;
    if (st->n == st->cap) {
        int cap = st->cap ? st->cap * 2 : 16;
        km_imap_folder *v = realloc(st->v, (size_t)cap * sizeof *v);
        if (!v) return 0;
        st->v = v;
        st->cap = cap;
    }
    f = &st->v[st->n];
    memset(f, 0, sizeof *f);
    f->name = dup_range(name->s, name->len);
    f->display = km_mutf7_decode(f->name ? f->name : "");
    f->delimiter = delim->kind == IT_STRING && delim->len == 1 ? delim->s[0] : 0;
    {
        item *a;
        for (a = attrs->child; a; a = a->next) {
            if (a->kind != IT_ATOM) continue;
            if (!strcasecmp(a->s, "\\Noselect") || !strcasecmp(a->s, "\\NonExistent")) f->attrs |= KM_FOLDER_NOSELECT;
            else if (!strcasecmp(a->s, "\\HasChildren")) f->attrs |= KM_FOLDER_CHILDREN;
            else if (!strcasecmp(a->s, "\\HasNoChildren")) f->attrs |= KM_FOLDER_NOCHILDREN;
        }
    }
    if (!f->name || !f->display) { free(f->name); free(f->display); return 0; }
    st->n++;
    return 1;
}

int km_imap_list(km_imap *m, km_imap_folder **folders, int *count)
{
    list_state st;
    memset(&st, 0, sizeof st);
    if (!command(m, "LIST \"\" \"*\"", 0, on_list, NULL, &st)) {
        km_imap_free_folders(st.v, st.n);
        *folders = NULL;
        *count = 0;
        return 0;
    }
    *folders = st.v;
    *count = st.n;
    return 1;
}

void km_imap_free_folders(km_imap_folder *folders, int count)
{
    int i;
    for (i = 0; i < count; i++) { free(folders[i].name); free(folders[i].display); }
    free(folders);
}

int km_imap_select(km_imap *m, const char *name, int readonly)
{
    km_buf cmd;
    int ok;
    m->exists = m->recent = m->uidvalidity = m->uidnext = 0;
    m->readonly = readonly;
    km_buf_init(&cmd);
    km_buf_adds(&cmd, readonly ? "EXAMINE " : "SELECT ");
    if (!add_quoted(&cmd, name)) {
        km_buf_free(&cmd);
        km_buf_clear(&m->err);
        km_buf_adds(&m->err, "The folder's name cannot be sent.");
        return 0;
    }
    ok = command(m, km_buf_str(&cmd), 0, NULL, NULL, NULL);
    km_buf_free(&cmd);
    return ok;
}

/* ---- messages -------------------------------------------------------------- */

static unsigned flags_of(item *list)
{
    unsigned f = 0;
    for (; list; list = list->next) {
        if (list->kind != IT_ATOM) continue;
        if (!strcasecmp(list->s, "\\Seen")) f |= KM_F_SEEN;
        else if (!strcasecmp(list->s, "\\Answered")) f |= KM_F_ANSWERED;
        else if (!strcasecmp(list->s, "\\Flagged")) f |= KM_F_FLAGGED;
        else if (!strcasecmp(list->s, "\\Deleted")) f |= KM_F_DELETED;
        else if (!strcasecmp(list->s, "\\Draft")) f |= KM_F_DRAFT;
        else if (!strcasecmp(list->s, "\\Recent")) f |= KM_F_RECENT;
    }
    return f;
}

typedef struct fetch_state {
    km_imap_msg *v;
    int n, cap;
    unsigned long uid_from;
    km_buf *body;          /* km_imap_fetch_message: the message goes here */
    int got_body;
} fetch_state;

static int on_fetch(km_imap *m, resp *r, void *user)
{
    fetch_state *st = user;
    item *it;
    km_imap_msg msg;
    (void)m;
    if (strcasecmp(r->name, "FETCH")) return 1;
    if (!r->items || r->items->kind != IT_LIST) return 0;
    memset(&msg, 0, sizeof msg);
    for (it = r->items->child; it && it->next; it = it->next->next) {
        item *v = it->next;
        if (it->kind != IT_ATOM) return 0;
        if (!strcasecmp(it->s, "UID") && v->kind == IT_ATOM) msg.uid = strtoul(v->s, NULL, 10);
        else if (!strcasecmp(it->s, "FLAGS") && v->kind == IT_LIST) msg.flags = flags_of(v->child);
        else if (!strcasecmp(it->s, "RFC822.SIZE") && v->kind == IT_ATOM) msg.size = strtoul(v->s, NULL, 10);
        else if (!strcasecmp(it->s, "INTERNALDATE") && v->kind == IT_STRING) { free(msg.internaldate); msg.internaldate = dup_range(v->s, v->len); }
        else if (!strncasecmp(it->s, "BODY[", 5) && (v->kind == IT_STRING || v->kind == IT_NIL)) {
            if (st->body) { km_buf_add(st->body, v->s, v->len); st->got_body = 1; }
            else { free(msg.header); msg.header = dup_range(v->s, v->len); }
        }
    }
    if (st->body || !msg.uid || msg.uid < st->uid_from) {   /* "n:*" also brings the last message when none is newer */
        free(msg.internaldate);
        free(msg.header);
        return 1;
    }
    if (st->n == st->cap) {
        int cap = st->cap ? st->cap * 2 : 64;
        km_imap_msg *v = realloc(st->v, (size_t)cap * sizeof *v);
        if (!v) { free(msg.internaldate); free(msg.header); return 0; }
        st->v = v;
        st->cap = cap;
    }
    st->v[st->n++] = msg;
    return 1;
}

int km_imap_fetch_summaries(km_imap *m, unsigned long uid_from, km_imap_msg **msgs, int *count)
{
    fetch_state st;
    char cmd[256];
    memset(&st, 0, sizeof st);
    st.uid_from = uid_from ? uid_from : 1;
    snprintf(cmd, sizeof cmd, "UID FETCH %lu:* (UID FLAGS RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER.FIELDS "
             "(DATE FROM TO CC REPLY-TO SUBJECT MESSAGE-ID IN-REPLY-TO REFERENCES CONTENT-TYPE)])", st.uid_from);
    *msgs = NULL;
    *count = 0;
    if (!m->exists) return command(m, "NOOP", 0, NULL, NULL, NULL);   /* an empty folder: nothing to fetch */
    if (!command(m, cmd, 0, on_fetch, NULL, &st)) { km_imap_free_msgs(st.v, st.n); return 0; }
    *msgs = st.v;
    *count = st.n;
    return 1;
}

int km_imap_fetch_message(km_imap *m, unsigned long uid, km_buf *out)
{
    fetch_state st;
    char cmd[64];
    memset(&st, 0, sizeof st);
    st.body = out;
    snprintf(cmd, sizeof cmd, "UID FETCH %lu (UID BODY.PEEK[])", uid);
    if (!command(m, cmd, 0, on_fetch, NULL, &st)) return 0;
    if (!st.got_body) { km_buf_clear(&m->err); km_buf_adds(&m->err, "The server has no such message."); return 0; }
    return 1;
}

void km_imap_free_msgs(km_imap_msg *msgs, int count)
{
    int i;
    for (i = 0; i < count; i++) { free(msgs[i].internaldate); free(msgs[i].header); }
    free(msgs);
}

int km_imap_set_flags(km_imap *m, unsigned long uid, unsigned flags, int add)
{
    km_buf cmd;
    int ok;
    km_buf_init(&cmd);
    km_buf_printf(&cmd, "UID STORE %lu %cFLAGS.SILENT (", uid, add ? '+' : '-');
    if (flags & KM_F_SEEN) km_buf_adds(&cmd, "\\Seen ");
    if (flags & KM_F_ANSWERED) km_buf_adds(&cmd, "\\Answered ");
    if (flags & KM_F_FLAGGED) km_buf_adds(&cmd, "\\Flagged ");
    if (flags & KM_F_DELETED) km_buf_adds(&cmd, "\\Deleted ");
    if (flags & KM_F_DRAFT) km_buf_adds(&cmd, "\\Draft ");
    if (cmd.len && cmd.data[cmd.len - 1] == ' ') cmd.data[--cmd.len] = 0;
    km_buf_addc(&cmd, ')');
    ok = !cmd.failed && command(m, km_buf_str(&cmd), 0, NULL, NULL, NULL);
    km_buf_free(&cmd);
    return ok;
}

int km_imap_noop(km_imap *m) { return command(m, "NOOP", 0, NULL, NULL, NULL); }

void km_imap_logout(km_imap *m)
{
    if (m->conn) command(m, "LOGOUT", 0, NULL, NULL, NULL);
    km_imap_close(m);
}

void km_imap_close(km_imap *m)
{
    if (m->conn) km_net_close(m->conn);
    m->conn = NULL;
    m->logged_in = 0;
    km_buf_clear(&m->caps);
}

void km_imap_dispose(km_imap *m)
{
    km_imap_close(m);
    km_buf_free(&m->caps);
    km_buf_free(&m->err);
}

/* ---- modified UTF-7 ---------------------------------------------------------- */

static const char mb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";

static void put_utf8(km_buf *b, unsigned long c)
{
    if (c < 0x80) km_buf_addc(b, (char)c);
    else if (c < 0x800) { km_buf_addc(b, (char)(0xc0 | c >> 6)); km_buf_addc(b, (char)(0x80 | (c & 63))); }
    else if (c < 0x10000) { km_buf_addc(b, (char)(0xe0 | c >> 12)); km_buf_addc(b, (char)(0x80 | (c >> 6 & 63))); km_buf_addc(b, (char)(0x80 | (c & 63))); }
    else { km_buf_addc(b, (char)(0xf0 | c >> 18)); km_buf_addc(b, (char)(0x80 | (c >> 12 & 63))); km_buf_addc(b, (char)(0x80 | (c >> 6 & 63))); km_buf_addc(b, (char)(0x80 | (c & 63))); }
}

char *km_mutf7_decode(const char *name)
{
    km_buf out;
    const char *p = name;
    km_buf_init(&out);
    while (*p) {
        if (*p != '&') { km_buf_addc(&out, *p++); continue; }
        p++;
        if (*p == '-') { km_buf_addc(&out, '&'); p++; continue; }
        {
            unsigned long acc = 0, hi = 0;
            int bits = 0;
            for (; *p && *p != '-'; p++) {
                const char *k = strchr(mb64, *p);
                if (!k) break;
                acc = (acc << 6 | (unsigned long)(k - mb64)) & 0xffffff;
                bits += 6;
                if (bits >= 16) {
                    unsigned long u;
                    bits -= 16;
                    u = acc >> bits & 0xffff;
                    if (u >= 0xd800 && u < 0xdc00) hi = u;
                    else if (u >= 0xdc00 && u < 0xe000 && hi) { put_utf8(&out, 0x10000 + ((hi - 0xd800) << 10) + (u - 0xdc00)); hi = 0; }
                    else put_utf8(&out, u);
                }
            }
            if (*p == '-') p++;
        }
    }
    return km_buf_take(&out);
}

static unsigned long next_utf8(const unsigned char **pp)
{
    const unsigned char *p = *pp;
    unsigned long c = *p++;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    if (extra) c &= 0x3f >> extra;
    while (extra-- && (*p & 0xc0) == 0x80) c = c << 6 | (*p++ & 63);
    *pp = p;
    return c;
}

char *km_mutf7_encode(const char *utf8)
{
    km_buf out;
    const unsigned char *p = (const unsigned char *)utf8;
    km_buf_init(&out);
    while (*p) {
        if (*p >= 0x20 && *p < 0x7f) {
            if (*p == '&') km_buf_adds(&out, "&-");
            else km_buf_addc(&out, (char)*p);
            p++;
            continue;
        }
        {
            unsigned long acc = 0;
            int bits = 0;
            km_buf_addc(&out, '&');
            while (*p && (*p < 0x20 || *p >= 0x7f)) {
                unsigned long c = next_utf8(&p), units[2];
                int n = 1, i;
                if (c >= 0x10000) { c -= 0x10000; units[0] = 0xd800 + (c >> 10); units[1] = 0xdc00 + (c & 0x3ff); n = 2; }
                else units[0] = c;
                for (i = 0; i < n; i++) {
                    acc = acc << 16 | units[i];
                    bits += 16;
                    while (bits >= 6) { bits -= 6; km_buf_addc(&out, mb64[acc >> bits & 63]); }
                }
            }
            if (bits) km_buf_addc(&out, mb64[acc << (6 - bits) & 63]);
            km_buf_addc(&out, '-');
        }
    }
    return km_buf_take(&out);
}
