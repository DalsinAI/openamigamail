/* KyneMail engine tests on the host.
 *   test_engine unit                 the engine's pieces on their own
 *   test_engine imap SCENARIO PORT   a session against fake_imapd.py
 * Exit 0 when every check passes; each failure is printed. */
#include "km_base64.h"
#include "km_imap.h"
#include "km_sasl.h"
#include "km_text.h"

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
        km_buf b, d;
        km_buf_init(&b);
        km_buf_init(&d);
        km_base64_encode(&b, plain[i], strlen(plain[i]));
        check_str(km_buf_str(&b), coded[i], "base64 encode");
        CHECK(km_base64_decode(&d, coded[i], strlen(coded[i])), "base64 decode %s", coded[i]);
        check_str(km_buf_str(&d), plain[i], "base64 decode");
        km_buf_free(&b);
        km_buf_free(&d);
    }
    {
        km_buf d;
        km_buf_init(&d);
        CHECK(km_base64_decode(&d, "Zm9v\r\nYmFy", 10) && !strcmp(km_buf_str(&d), "foobar"), "base64 across a line break");
        km_buf_clear(&d);
        CHECK(!km_base64_decode(&d, "Zm9v!", 5), "base64 refuses '!'");
        km_buf_free(&d);
    }
}

static void unit_sasl(void)
{
    km_buf b;
    km_buf_init(&b);
    /* Google's published XOAUTH2 example */
    CHECK(km_sasl_xoauth2(&b, "someuser@example.com", "ya29.vF9dft4qmTc2Nvb3RlckBhdHRhdmljYS5vY29tCg"), "xoauth2");
    check_str(km_buf_str(&b), "dXNlcj1zb21ldXNlckBleGFtcGxlLmNvbQFhdXRoPUJlYXJlciB5YTI5LnZGOWRmdDRxbVRjMk52YjNSbGNrQmhkSFJoZG1sallTNXZZMjl0Q2cBAQ==", "xoauth2");
    km_buf_clear(&b);
    CHECK(km_sasl_plain(&b, "dale", "secret"), "plain");
    check_str(km_buf_str(&b), "AGRhbGUAc2VjcmV0", "plain");
    km_buf_clear(&b);
    CHECK(!km_sasl_plain(&b, "dale", "sec\nret"), "plain refuses a line break");
    CHECK(!km_sasl_xoauth2(&b, "dale", "tok en"), "xoauth2 refuses a space in the token");
    km_buf_free(&b);
}

static void unit_mutf7(void)
{
    char *s;
    s = km_mutf7_decode("&U,BTFw-/&ZeVnLIqe-");
    check_str(s, "\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "mutf7 decode (RFC 3501's example)");
    free(s);
    s = km_mutf7_decode("Gr&APw-&AN8-e");
    check_str(s, "Gr\xc3\xbc\xc3\x9f" "e", "mutf7 decode Gruesse");
    free(s);
    s = km_mutf7_decode("Tom &- Jerry");
    check_str(s, "Tom & Jerry", "mutf7 decode &-");
    free(s);
    s = km_mutf7_encode("\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    check_str(s, "&U,BTFw-/&ZeVnLIqe-", "mutf7 encode");
    free(s);
    s = km_mutf7_encode("Tom & Jerry");
    check_str(s, "Tom &- Jerry", "mutf7 encode &");
    free(s);
    s = km_mutf7_encode("\xf0\x9f\x93\xa7 Mail");       /* outside the BMP: a surrogate pair */
    {
        char *back = km_mutf7_decode(s);
        check_str(back, "\xf0\x9f\x93\xa7 Mail", "mutf7 round trip of U+1F4E7");
        free(back);
    }
    free(s);
}


static void check_decode(const char *in, const char *want)
{
    char *s = km_hdr_decode(in);
    check_str(s, want, in);
    free(s);
}

static void unit_text(void)
{
    static const char hdr[] = "Subject: Hello\r\n world\r\nFrom: =?UTF-8?Q?Galen_=E2=9C=A8?= <galen@example.com>\r\n"
                              "To: \"Kirkwood, Dale\" <dale@example.com>, other@example.com\r\nX-Empty:\r\n\r\n";
    char *s, *name, *addr;
    long long when;
    int zone;
    s = km_hdr_get(hdr, sizeof hdr - 1, "subject");
    check_str(s, "Hello world", "a folded Subject");
    free(s);
    s = km_hdr_get(hdr, sizeof hdr - 1, "X-Empty");
    check_str(s, "", "an empty field");
    free(s);
    CHECK(!km_hdr_get(hdr, sizeof hdr - 1, "Cc"), "a missing field is NULL");
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
    s = km_hdr_get(hdr, sizeof hdr - 1, "From");
    CHECK(km_hdr_address(s, &name, &addr), "From reads");
    check_str(name, "Galen \xe2\x9c\xa8", "From's name");
    check_str(addr, "galen@example.com", "From's address");
    free(s); free(name); free(addr);
    CHECK(km_hdr_address("\"Kirkwood, Dale\" <dale@example.com>, other@example.com", &name, &addr), "a quoted name with a comma");
    check_str(name, "Kirkwood, Dale", "quoted name");
    check_str(addr, "dale@example.com", "its address");
    free(name); free(addr);
    CHECK(km_hdr_address("thufir@example.com (Thufir Hawat)", &name, &addr), "a comment as the name");
    check_str(name, "Thufir Hawat", "comment name");
    check_str(addr, "thufir@example.com", "bare address");
    free(name); free(addr);
    CHECK(km_hdr_address("<only@example.com>", &name, &addr), "angle brackets alone");
    check_str(name, "", "no name");
    check_str(addr, "only@example.com", "address in brackets");
    free(name); free(addr);
    CHECK(!km_hdr_address("undisclosed-recipients:;", &name, &addr), "an empty group has no mailbox");
    free(name); free(addr);
    /* dates: values from Python's email.utils */
    CHECK(km_hdr_date("Thu, 02 Oct 2026 21:00:00 +0100", &when, &zone) && when == 1790971200LL && zone == 60, "RFC 5322 date: %lld %d", when, zone);
    CHECK(km_hdr_date("2 Oct 26 21:00 GMT", &when, &zone) && when == 1790974800LL && zone == 0, "two-digit year, GMT: %lld", when);
    CHECK(km_hdr_date("02-Oct-2026 21:01:00 +0100", &when, &zone) && when == 1790971260LL, "IMAP internal date: %lld", when);
    CHECK(!km_hdr_date("yesterday", &when, &zone), "nonsense is refused");
    /* UTF-8 to the Amiga's Latin-1 */
    s = km_utf8_to_latin1("Gr\xc3\xbc\xc3\x9f" "e \xe2\x80\x9cq\xe2\x80\x9d \xe2\x80\x93 \xe2\x82\xac" "5\xe2\x80\xa6 Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87 \xe2\x9c\xa8");
    CHECK(!strcmp(s, "Gr\xfc\xdf" "e \"q\" - EUR5... Zaz\xf3lc ?"), "latin1: got \"%s\"", s);
    free(s);
    CHECK(km_utf8_valid("\xef\xbf\xbd", 3), "a real U+FFFD is valid UTF-8");
    CHECK(!km_utf8_valid("\xc3", 1), "a cut sequence is not");
}

static void trace(void *user, int sent, const char *line)
{
    km_buf *log = user;
    km_buf_printf(log, "%s %s\n", sent ? "C:" : "S:", line);
}

static int imap_session(const char *scenario, int port)
{
    km_imap m;
    km_buf log;
    int security = !strcmp(scenario, "tls") ? KM_IMAP_TLS : !strcmp(scenario, "starttls") ? KM_IMAP_STARTTLS : KM_IMAP_PLAIN;
    int start = failures;
    km_imap_init(&m);
    km_buf_init(&log);
    m.trace = trace;
    m.trace_user = &log;
    if (!km_imap_connect(&m, "localhost", port, security)) {
        CHECK(0, "connect: %s", km_imap_error(&m));
        goto done;
    }
    if (!strcmp(scenario, "xoauth2") || !strcmp(scenario, "xoauth2bad")) {
        int ok = km_imap_login_xoauth2(&m, "dale@example.com", "ya29.test-token");
        if (!strcmp(scenario, "xoauth2bad")) {
            CHECK(!ok, "a refused token logs in");
            CHECK(strstr(km_imap_error(&m), "AUTHENTICATIONFAILED") && strstr(km_imap_error(&m), "\"status\":\"401\""),
                  "the error says why: %s", km_imap_error(&m));
            km_imap_close(&m);
            goto done;
        }
        CHECK(ok, "xoauth2 login: %s", km_imap_error(&m));
    } else CHECK(km_imap_login(&m, "dale@example.com", "pa\"ss\\word"), "login: %s", km_imap_error(&m));
    CHECK(km_imap_has(&m, "MOVE"), "capabilities after login");
    {
        km_imap_folder *f;
        int n;
        CHECK(km_imap_list(&m, &f, &n), "list: %s", km_imap_error(&m));
        CHECK(n == 5, "5 folders, got %d", n);
        if (n == 5) {
            check_str(f[0].name, "INBOX", "folder 0");
            CHECK(f[0].delimiter == '/', "INBOX's delimiter");
            CHECK(f[1].attrs & KM_FOLDER_NOSELECT && f[1].attrs & KM_FOLDER_CHILDREN, "[Gmail] is not selectable and has children");
            check_str(f[2].display, "[Gmail]/Sent Mail", "folder 2");
            check_str(f[3].display, "Gr\xc3\xbc\xc3\x9f" "e", "folder 3 in UTF-8");
            check_str(f[3].name, "Gr&APw-&AN8-e", "folder 3 as sent");
            check_str(f[4].name, "Braces {in} it", "folder 4, a literal");
            CHECK(f[4].delimiter == 0, "NIL delimiter");
        }
        km_imap_free_folders(f, n);
    }
    CHECK(km_imap_select(&m, "INBOX", 0), "select: %s", km_imap_error(&m));
    CHECK(m.exists == 2 && m.uidvalidity == 1234 && m.uidnext == 43 && !m.readonly,
          "select state: exists %lu uidvalidity %lu uidnext %lu readonly %d", m.exists, m.uidvalidity, m.uidnext, m.readonly);
    {
        km_imap_msg *v;
        int n;
        CHECK(km_imap_fetch_summaries(&m, 1, &v, &n), "fetch: %s", km_imap_error(&m));
        CHECK(n == 2, "2 summaries, got %d", n);
        if (n == 2) {
            CHECK(v[0].uid == 41 && v[0].flags == KM_F_SEEN && v[0].size == 1001, "message 41: uid %lu flags %u size %lu", v[0].uid, v[0].flags, v[0].size);
            CHECK(v[1].uid == 42 && v[1].flags == 0 && v[1].size == 1002, "message 42");
            CHECK(v[0].header && strstr(v[0].header, "Subject: =?ISO-8859-1?Q?Gr=FC=DFe_aus_Amiga?="), "message 41's header");
            CHECK(v[1].header && strstr(v[1].header, "Subject: (parens) and \"quotes\""), "message 42's header");
            check_str(v[0].internaldate, "02-Oct-2026 21:01:00 +0100", "internal date");
        }
        km_imap_free_msgs(v, n);
        CHECK(m.exists == 3, "a new message noticed during the fetch: exists %lu", m.exists);
    }
    {
        km_buf body;
        km_buf_init(&body);
        CHECK(km_imap_fetch_message(&m, 42, &body), "fetch message: %s", km_imap_error(&m));
        CHECK(strstr(km_buf_str(&body), "Line two with {braces} and ) a paren\r\n") != NULL, "the message's text");
        CHECK(body.len == 101, "the message is 101 bytes, got %lu", (unsigned long)body.len);
        km_buf_free(&body);
    }
    CHECK(km_imap_set_flags(&m, 42, KM_F_SEEN | KM_F_FLAGGED, 1), "store: %s", km_imap_error(&m));
    km_imap_logout(&m);
done:
    if (failures != start) printf("--- protocol ---\n%s", km_buf_str(&log));
    else if (!strcmp(scenario, "plain")) {
        /* the password never shows in the protocol log */
        CHECK(!strstr(km_buf_str(&log), "AGRhbGVAZXhhbXBsZS5jb20A"), "the PLAIN response is hidden in the log");
    }
    km_buf_free(&log);
    km_imap_dispose(&m);
    return failures == start;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "unit")) {
        unit_base64();
        unit_sasl();
        unit_mutf7();
        unit_text();
    } else if (argc >= 4 && !strcmp(argv[1], "imap")) {
        imap_session(argv[2], atoi(argv[3]));
    } else {
        fprintf(stderr, "usage: test_engine unit | imap SCENARIO PORT\n");
        return 2;
    }
    printf("%s: %d failure%s\n", argc >= 3 ? argv[2] : argv[1], failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
