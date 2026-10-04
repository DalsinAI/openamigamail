/* OpenAmigaMail engine tests on the host.
 *   test_engine unit                 the engine's pieces on their own
 *   test_engine imap SCENARIO PORT   a session against fake_imapd.py
 * Exit 0 when every check passes; each failure is printed. */
#include "oam_base64.h"
#include "oam_imap.h"
#include "oam_sasl.h"
#include "oam_text.h"
#include "oam_provider.h"
#include "oam_html.h"
#include "oam_mime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void check_str(const char *got, const char *want, const char *what)
{
    CHECK(got && !strcmp(got, want), "%s: got \"%s\", wanted \"%s\"", what, got ? got : "(null)", want);
}

static void unit_base64(void)
{
    static const char *plain[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
    static const char *coded[] = { "", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy" };
    size_t i;
    for (i = 0; i < 7; i++) {
        oam_buf b, d;
        oam_buf_init(&b);
        oam_buf_init(&d);
        oam_base64_encode(&b, plain[i], strlen(plain[i]));
        check_str(oam_buf_str(&b), coded[i], "base64 encode");
        CHECK(oam_base64_decode(&d, coded[i], strlen(coded[i])), "base64 decode %s", coded[i]);
        check_str(oam_buf_str(&d), plain[i], "base64 decode");
        oam_buf_free(&b);
        oam_buf_free(&d);
    }
    {
        oam_buf d;
        oam_buf_init(&d);
        CHECK(oam_base64_decode(&d, "Zm9v\r\nYmFy", 10) && !strcmp(oam_buf_str(&d), "foobar"), "base64 across a line break");
        oam_buf_clear(&d);
        CHECK(!oam_base64_decode(&d, "Zm9v!", 5), "base64 refuses '!'");
        oam_buf_free(&d);
    }
}

static void unit_sasl(void)
{
    oam_buf b;
    oam_buf_init(&b);
    /* Google's published XOAUTH2 example */
    CHECK(oam_sasl_xoauth2(&b, "someuser@example.com", "ya29.vF9dft4qmTc2Nvb3RlckBhdHRhdmljYS5vY29tCg"), "xoauth2");
    check_str(oam_buf_str(&b), "dXNlcj1zb21ldXNlckBleGFtcGxlLmNvbQFhdXRoPUJlYXJlciB5YTI5LnZGOWRmdDRxbVRjMk52YjNSbGNrQmhkSFJoZG1sallTNXZZMjl0Q2cBAQ==", "xoauth2");
    oam_buf_clear(&b);
    CHECK(oam_sasl_plain(&b, "dale", "secret"), "plain");
    check_str(oam_buf_str(&b), "AGRhbGUAc2VjcmV0", "plain");
    oam_buf_clear(&b);
    CHECK(!oam_sasl_plain(&b, "dale", "sec\nret"), "plain refuses a line break");
    CHECK(!oam_sasl_xoauth2(&b, "dale", "tok en"), "xoauth2 refuses a space in the token");
    oam_buf_free(&b);
}

static void unit_mutf7(void)
{
    char *s;
    s = oam_mutf7_decode("&U,BTFw-/&ZeVnLIqe-");
    check_str(s, "\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "mutf7 decode (RFC 3501's example)");
    free(s);
    s = oam_mutf7_decode("Gr&APw-&AN8-e");
    check_str(s, "Gr\xc3\xbc\xc3\x9f" "e", "mutf7 decode Gruesse");
    free(s);
    s = oam_mutf7_decode("Tom &- Jerry");
    check_str(s, "Tom & Jerry", "mutf7 decode &-");
    free(s);
    s = oam_mutf7_encode("\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    check_str(s, "&U,BTFw-/&ZeVnLIqe-", "mutf7 encode");
    free(s);
    s = oam_mutf7_encode("Tom & Jerry");
    check_str(s, "Tom &- Jerry", "mutf7 encode &");
    free(s);
    s = oam_mutf7_encode("\xf0\x9f\x93\xa7 Mail");       /* outside the BMP: a surrogate pair */
    {
        char *back = oam_mutf7_decode(s);
        check_str(back, "\xf0\x9f\x93\xa7 Mail", "mutf7 round trip of U+1F4E7");
        free(back);
    }
    free(s);
}


static void check_decode(const char *in, const char *want)
{
    char *s = oam_hdr_decode(in);
    check_str(s, want, in);
    free(s);
}

static void unit_provider(void)
{
    static const char gmail[] =
        "# Gmail, the way OpenAmigaMail ships it\n"
        "name    = Gmail\n"
        "domains = gmail.com googlemail.com\n"
        "imap    = imap.gmail.com 993 tls\n"
        "smtp    = smtp.gmail.com 587 starttls\n"
        "auth    = xoauth2-browser password\n"
        "  oauth.token = https://oauth2.googleapis.com/token  \n"
        "note = Two-step accounts can use an app password.\n";
    oam_provider p;
    char err[96];
    CHECK(oam_provider_parse(gmail, &p, err, sizeof err), "provider parses: %s", err);
    check_str(p.name, "Gmail", "provider name");
    check_str(p.imap.host, "imap.gmail.com", "imap host");
    CHECK(p.imap.port == 993 && p.imap.security == OAM_IMAP_TLS, "imap port and tls");
    CHECK(p.smtp.port == 587 && p.smtp.security == OAM_IMAP_STARTTLS, "smtp starttls");
    CHECK(p.nauth == 2 && !strcmp(p.auth[0], "xoauth2-browser"), "auth methods in order");
    CHECK(oam_provider_has_auth(&p, "password") && !oam_provider_has_auth(&p, "xoauth2-device"), "has_auth");
    check_str(oam_provider_get(&p, "oauth.token"), "https://oauth2.googleapis.com/token", "an extra key, trimmed");
    CHECK(!oam_provider_get(&p, "oauth.scope"), "a missing key is NULL");
    CHECK(oam_provider_matches(&p, "dale@gmail.com"), "matches its domain");
    CHECK(oam_provider_matches(&p, "dale@GoogleMail.COM"), "matches without regard to case");
    CHECK(oam_provider_matches(&p, "dale@mail.gmail.com"), "matches a subdomain");
    CHECK(!oam_provider_matches(&p, "dale@notgmail.com"), "not another domain that ends the same");
    CHECK(!oam_provider_matches(&p, "gmail.com"), "not without an @");
    CHECK(!oam_provider_parse("name = X\nimap = host 993 maybe\nauth = password\n", &p, err, sizeof err) &&
          strstr(err, "line 2"), "a bad security word is refused, with its line: %s", err);
    CHECK(!oam_provider_parse("name = X\nauth = password\n", &p, err, sizeof err), "no imap server is refused");
    CHECK(!oam_provider_parse("name = X\nimap = h 993 tls\njunk\n", &p, err, sizeof err), "a line without = is refused");
}

static void html_case(const char *html, const char *want_text, const char *want_links)
{
    oam_buf t, l;
    oam_buf_init(&t);
    oam_buf_init(&l);
    CHECK(oam_html_to_text(html, strlen(html), &t, &l), "html converts");
    check_str(oam_buf_str(&t), want_text, html);
    check_str(oam_buf_str(&l), want_links, "links");
    oam_buf_free(&t);
    oam_buf_free(&l);
}

static void unit_html(void)
{
    html_case("<p>Hello <b>world</b></p><p>Second</p>", "Hello world\n\nSecond", "");
    html_case("<html><head><title>T</title><style>p{x:1}</style></head><body>Hi<script>alert(1)</script> there</body></html>",
              "Hi there", "");
    html_case("Line one<br>Line   two<br/>\n  three", "Line one\nLine two\nthree", "");
    html_case("<ul><li>apples</li><li>pears</li></ul>after", "- apples\n- pears\n\nafter", "");
    html_case("See <a href=\"https://aminet.net/\">Aminet</a> and <a href='#top'>top</a>.",
              "See Aminet [1] and top.", "https://aminet.net/\n");
    html_case("Fish &amp; chips &lt;3 &#163;5 &#x20AC;2 &nbsp;&copy; &bogus;", "Fish & chips <3 \xc2\xa3" "5 \xe2\x82\xac" "2 \xc2\xa9 &bogus;", "");
    html_case("<pre>a  b\n  c</pre>d", "a  b\n  c\n\nd", "");
    html_case("<img src=\"cid:x\" alt=\"Logo\"> text<!-- hidden <b>x</b> -->", "[Logo] text", "");
    html_case("<table><tr><td>A</td><td>B</td></tr><tr><td>C</td></tr></table>", "A B\nC", "");
}

static void view_case(const char *msg, const char *want_text, const char *want_links, int want_html, int want_att, const char *what)
{
    oam_view v;
    oam_view_init(&v);
    CHECK(oam_mime_view(msg, strlen(msg), &v), "%s: view", what);
    check_str(oam_buf_str(&v.text), want_text, what);
    check_str(oam_buf_str(&v.links), want_links, what);
    CHECK(v.has_html == want_html, "%s: has_html %d", what, v.has_html);
    CHECK(v.attachments == want_att, "%s: attachments %d", what, v.attachments);
    oam_view_free(&v);
}

static void unit_mime(void)
{
    view_case("Subject: x\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\n"
              "Caf=C3=A9 so=\r\nft break, see https://aminet.net/.\r\nLine two\r\n",
              "Caf\xc3\xa9 soft break, see https://aminet.net/.\nLine two\n", "https://aminet.net/\n", 0, 0, "plain QP");
    view_case("Content-Type: multipart/alternative; boundary=\"b1\"\n\nignored preamble\n--b1\nContent-Type: text/plain\n\nplain one\n"
              "--b1\nContent-Type: text/html\n\n<p>html <a href=\"https://x.org\">one</a></p>\n--b1--\nepilogue\n",
              "plain one", "", 1, 0, "alternative prefers the text");
    view_case("Content-Type: multipart/alternative; boundary=b2\n\n--b2\nContent-Type: text/html; charset=iso-8859-1\n"
              "Content-Transfer-Encoding: base64\n\nPHA+Q2Fm6TwvcD48cD48YSBocmVmPSJodHRwOi8veS5vcmciPnk8L2E+PC9wPg==\n--b2--\n",
              "Caf\xc3\xa9\n\ny [1]", "http://y.org\n", 1, 0, "html only, base64, latin-1");
    view_case("Content-Type: multipart/mixed; boundary=outer\n\n--outer\nContent-Type: multipart/alternative; boundary=inner\n\n"
              "--inner\nContent-Type: text/plain\n\nnested text\n--inner--\n--outer\nContent-Type: application/pdf; name=\"a.pdf\"\n"
              "Content-Disposition: attachment; filename*=utf-8''r%C3%A9sum%C3%A9.pdf\nContent-Transfer-Encoding: base64\n\nJVBERg==\n--outer\n"
              "Content-Type: image/png\nContent-ID: <Logo1@x>\n\nPNG\n--outer--\n",
              "nested text", "", 0, 1, "mixed: nested text, one attachment, an inline image");
    view_case("Just a body\nwith no header\n", "Just a body\nwith no header\n", "", 0, 0, "not a message");
    {   /* the attachment's name, decoded */
        struct { char name[128]; char cid[128]; } got = { "", "" };
        const char *m = "Content-Type: multipart/mixed; boundary=z\n\n--z\nContent-Type: application/pdf\n"
                        "Content-Disposition: attachment; filename*=utf-8''r%C3%A9sum%C3%A9.pdf\n\nx\n--z\n"
                        "Content-Type: image/png\nContent-ID: <Logo1@X>\n\ny\n--z--\n";
        void cb(const oam_part *p, void *u);
        oam_mime_walk(m, strlen(m), cb, &got);
        check_str(got.name, "r\xc3\xa9sum\xc3\xa9.pdf", "RFC 2231 filename");
        check_str(got.cid, "Logo1@X", "Content-ID keeps its case");
    }
}

void cb(const oam_part *p, void *u)
{
    struct { char name[128]; char cid[128]; } *got = u;
    if (p->filename[0]) snprintf(got->name, sizeof got->name, "%s", p->filename);
    if (p->cid[0]) snprintf(got->cid, sizeof got->cid, "%s", p->cid);
}

static void unit_text(void)
{
    static const char hdr[] = "Subject: Hello\r\n world\r\nFrom: =?UTF-8?Q?Galen_=E2=9C=A8?= <galen@example.com>\r\n"
                              "To: \"Kirkwood, Dale\" <dale@example.com>, other@example.com\r\nX-Empty:\r\n\r\n";
    char *s, *name, *addr;
    long long when;
    int zone;
    s = oam_hdr_get(hdr, sizeof hdr - 1, "subject");
    check_str(s, "Hello world", "a folded Subject");
    free(s);
    s = oam_hdr_get(hdr, sizeof hdr - 1, "X-Empty");
    check_str(s, "", "an empty field");
    free(s);
    CHECK(!oam_hdr_get(hdr, sizeof hdr - 1, "Cc"), "a missing field is NULL");
    /* expected values from Python's email.header */
    check_decode("=?ISO-8859-1?Q?Gr=FC=DFe_aus_Amiga?=", "Gr\xc3\xbc\xc3\x9f" "e aus Amiga");
    check_decode("=?UTF-8?B?4pyoIFNwYXJrbGU=?=", "\xe2\x9c\xa8 Sparkle");
    check_decode("=?UTF-8?Q?a?= =?UTF-8?Q?b?=", "ab");
    check_decode("x =?UTF-8?Q?a?= y", "x a y");
    check_decode("=?windows-1252?Q?=93quoted=94_=80?=", "\xe2\x80\x9cquoted\xe2\x80\x9d \xe2\x82\xac");
    check_decode("=?ISO-8859-2?Q?Za=BF=F3=B3=E6?=", "Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87");
    check_decode("=?utf-8*en?Q?language?=", "language");
    check_decode("Gr\xfc\xdf" "e raw", "Gr\xc3\xbc\xc3\x9f" "e raw");          /* raw Latin-1 in a header */
    check_decode("=?UTF-8?Q?not a word?=", "=?UTF-8?Q?not a word?=");           /* a space inside: left alone */
    check_decode("Re: plain", "Re: plain");
    s = oam_hdr_get(hdr, sizeof hdr - 1, "From");
    CHECK(oam_hdr_address(s, &name, &addr), "From reads");
    check_str(name, "Galen \xe2\x9c\xa8", "From's name");
    check_str(addr, "galen@example.com", "From's address");
    free(s); free(name); free(addr);
    CHECK(oam_hdr_address("\"Kirkwood, Dale\" <dale@example.com>, other@example.com", &name, &addr), "a quoted name with a comma");
    check_str(name, "Kirkwood, Dale", "quoted name");
    check_str(addr, "dale@example.com", "its address");
    free(name); free(addr);
    CHECK(oam_hdr_address("thufir@example.com (Thufir Hawat)", &name, &addr), "a comment as the name");
    check_str(name, "Thufir Hawat", "comment name");
    check_str(addr, "thufir@example.com", "bare address");
    free(name); free(addr);
    CHECK(oam_hdr_address("<only@example.com>", &name, &addr), "angle brackets alone");
    check_str(name, "", "no name");
    check_str(addr, "only@example.com", "address in brackets");
    free(name); free(addr);
    CHECK(!oam_hdr_address("undisclosed-recipients:;", &name, &addr), "an empty group has no mailbox");
    free(name); free(addr);
    /* dates: values from Python's email.utils */
    CHECK(oam_hdr_date("Thu, 02 Oct 2026 21:00:00 +0100", &when, &zone) && when == 1790971200LL && zone == 60, "RFC 5322 date: %lld %d", when, zone);
    CHECK(oam_hdr_date("2 Oct 26 21:00 GMT", &when, &zone) && when == 1790974800LL && zone == 0, "two-digit year, GMT: %lld", when);
    CHECK(oam_hdr_date("02-Oct-2026 21:01:00 +0100", &when, &zone) && when == 1790971260LL, "IMAP internal date: %lld", when);
    CHECK(!oam_hdr_date("yesterday", &when, &zone), "nonsense is refused");
    /* UTF-8 to the Amiga's Latin-1 */
    s = oam_utf8_to_latin1("Gr\xc3\xbc\xc3\x9f" "e \xe2\x80\x9cq\xe2\x80\x9d \xe2\x80\x93 \xe2\x82\xac" "5\xe2\x80\xa6 Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 \xe2\x9c\xa8");
    CHECK(!strcmp(s, "Gr\xfc\xdf" "e \"q\" - EUR5... Zaz\xf3lc ?"), "latin1: got \"%s\"", s);
    free(s);
    CHECK(oam_utf8_valid("\xef\xbf\xbd", 3), "a real U+FFFD is valid UTF-8");
    CHECK(!oam_utf8_valid("\xc3", 1), "a cut sequence is not");
}

static void trace(void *user, int sent, const char *line)
{
    oam_buf *log = user;
    oam_buf_printf(log, "%s %s\n", sent ? "C:" : "S:", line);
}

static int imap_session(const char *scenario, int port)
{
    oam_imap m;
    oam_buf log;
    int security = !strcmp(scenario, "tls") ? OAM_IMAP_TLS : !strcmp(scenario, "starttls") ? OAM_IMAP_STARTTLS : OAM_IMAP_PLAIN;
    int start = failures;
    oam_imap_init(&m);
    oam_buf_init(&log);
    m.trace = trace;
    m.trace_user = &log;
    if (!oam_imap_connect(&m, "localhost", port, security)) {
        CHECK(0, "connect: %s", oam_imap_error(&m));
        goto done;
    }
    if (!strcmp(scenario, "xoauth2") || !strcmp(scenario, "xoauth2bad")) {
        int ok = oam_imap_login_xoauth2(&m, "dale@example.com", "ya29.test-token");
        if (!strcmp(scenario, "xoauth2bad")) {
            CHECK(!ok, "a refused token logs in");
            CHECK(strstr(oam_imap_error(&m), "AUTHENTICATIONFAILED") && strstr(oam_imap_error(&m), "\"status\":\"401\""),
                  "the error says why: %s", oam_imap_error(&m));
            oam_imap_close(&m);
            goto done;
        }
        CHECK(ok, "xoauth2 login: %s", oam_imap_error(&m));
    } else CHECK(oam_imap_login(&m, "dale@example.com", "pa\"ss\\word"), "login: %s", oam_imap_error(&m));
    CHECK(oam_imap_has(&m, "MOVE"), "capabilities after login");
    {
        oam_imap_folder *f;
        int n;
        CHECK(oam_imap_list(&m, &f, &n), "list: %s", oam_imap_error(&m));
        CHECK(n == 5, "5 folders, got %d", n);
        if (n == 5) {
            check_str(f[0].name, "INBOX", "folder 0");
            CHECK(f[0].delimiter == '/', "INBOX's delimiter");
            CHECK(f[1].attrs & OAM_FOLDER_NOSELECT && f[1].attrs & OAM_FOLDER_CHILDREN, "[Gmail] is not selectable and has children");
            check_str(f[2].display, "[Gmail]/Sent Mail", "folder 2");
            check_str(f[3].display, "Gr\xc3\xbc\xc3\x9f" "e", "folder 3 in UTF-8");
            check_str(f[3].name, "Gr&APw-&AN8-e", "folder 3 as sent");
            check_str(f[4].name, "Braces {in} it", "folder 4, a literal");
            CHECK(f[4].delimiter == 0, "NIL delimiter");
        }
        oam_imap_free_folders(f, n);
    }
    CHECK(oam_imap_select(&m, "INBOX", 0), "select: %s", oam_imap_error(&m));
    CHECK(m.exists == 2 && m.uidvalidity == 1234 && m.uidnext == 43 && !m.readonly,
          "select state: exists %lu uidvalidity %lu uidnext %lu readonly %d", m.exists, m.uidvalidity, m.uidnext, m.readonly);
    {
        oam_imap_msg *v;
        int n;
        CHECK(oam_imap_fetch_summaries(&m, 1, &v, &n), "fetch: %s", oam_imap_error(&m));
        CHECK(n == 2, "2 summaries, got %d", n);
        if (n == 2) {
            CHECK(v[0].uid == 41 && v[0].flags == OAM_F_SEEN && v[0].size == 1001, "message 41: uid %lu flags %u size %lu", v[0].uid, v[0].flags, v[0].size);
            CHECK(v[1].uid == 42 && v[1].flags == 0 && v[1].size == 1002, "message 42");
            CHECK(v[0].header && strstr(v[0].header, "Subject: =?ISO-8859-1?Q?Gr=FC=DFe_aus_Amiga?="), "message 41's header");
            CHECK(v[1].header && strstr(v[1].header, "Subject: (parens) and \"quotes\""), "message 42's header");
            check_str(v[0].internaldate, "02-Oct-2026 21:01:00 +0100", "internal date");
        }
        oam_imap_free_msgs(v, n);
        CHECK(m.exists == 3, "a new message noticed during the fetch: exists %lu", m.exists);
    }
    {
        oam_buf body;
        oam_buf_init(&body);
        CHECK(oam_imap_fetch_message(&m, 42, &body), "fetch message: %s", oam_imap_error(&m));
        CHECK(strstr(oam_buf_str(&body), "Line two with {braces} and ) a paren\r\n") != NULL, "the message's text");
        CHECK(body.len == 101, "the message is 101 bytes, got %lu", (unsigned long)body.len);
        oam_buf_free(&body);
    }
    CHECK(oam_imap_set_flags(&m, 42, OAM_F_SEEN | OAM_F_FLAGGED, 1), "store: %s", oam_imap_error(&m));
    oam_imap_logout(&m);
done:
    if (failures != start) printf("--- protocol ---\n%s", oam_buf_str(&log));
    else if (!strcmp(scenario, "plain")) {
        /* the password never shows in the protocol log */
        CHECK(!strstr(oam_buf_str(&log), "AGRhbGVAZXhhbXBsZS5jb20A"), "the PLAIN response is hidden in the log");
    }
    oam_buf_free(&log);
    oam_imap_dispose(&m);
    return failures == start;
}

/* test_engine providers DIR: every .provider file in DIR parses. */
#include <dirent.h>
static void providers(const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    int seen = 0;
    CHECK(d != NULL, "cannot open %s", dir);
    while (d && (e = readdir(d))) {
        char path[512], text[4096], err[96];
        size_t n = strlen(e->d_name);
        FILE *f;
        oam_provider p;
        if (n < 10 || strcmp(e->d_name + n - 9, ".provider")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        f = fopen(path, "r");
        n = f ? fread(text, 1, sizeof text - 1, f) : 0;
        if (f) fclose(f);
        text[n] = 0;
        CHECK(oam_provider_parse(text, &p, err, sizeof err), "%s: %s", e->d_name, err);
        CHECK(p.smtp.port != 0, "%s has no smtp server", e->d_name);
        seen++;
    }
    if (d) closedir(d);
    CHECK(seen >= 5, "only %d providers in %s", seen, dir);
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "unit")) {
        unit_base64();
        unit_sasl();
        unit_mutf7();
        unit_text();
        unit_provider();
        unit_html();
        unit_mime();
    } else if (argc >= 3 && !strcmp(argv[1], "providers")) {
        providers(argv[2]);
    } else if (argc >= 4 && !strcmp(argv[1], "imap")) {
        imap_session(argv[2], atoi(argv[3]));
    } else {
        fprintf(stderr, "usage: test_engine unit | imap SCENARIO PORT\n");
        return 2;
    }
    printf("%s: %d failure%s\n", argc >= 3 ? argv[2] : argv[1], failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
