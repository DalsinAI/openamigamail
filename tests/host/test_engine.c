/* ACMail engine tests on the host.
 *   test_engine unit                 the engine's pieces on their own
 *   test_engine imap SCENARIO PORT   a session against fake_imapd.py
 * Exit 0 when every check passes; each failure is printed. */
#include "acm_base64.h"
#include "acm_imap.h"
#include "acm_sasl.h"

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
        acm_buf b, d;
        acm_buf_init(&b);
        acm_buf_init(&d);
        acm_base64_encode(&b, plain[i], strlen(plain[i]));
        check_str(acm_buf_str(&b), coded[i], "base64 encode");
        CHECK(acm_base64_decode(&d, coded[i], strlen(coded[i])), "base64 decode %s", coded[i]);
        check_str(acm_buf_str(&d), plain[i], "base64 decode");
        acm_buf_free(&b);
        acm_buf_free(&d);
    }
    {
        acm_buf d;
        acm_buf_init(&d);
        CHECK(acm_base64_decode(&d, "Zm9v\r\nYmFy", 10) && !strcmp(acm_buf_str(&d), "foobar"), "base64 across a line break");
        acm_buf_clear(&d);
        CHECK(!acm_base64_decode(&d, "Zm9v!", 5), "base64 refuses '!'");
        acm_buf_free(&d);
    }
}

static void unit_sasl(void)
{
    acm_buf b;
    acm_buf_init(&b);
    /* Google's published XOAUTH2 example */
    CHECK(acm_sasl_xoauth2(&b, "someuser@example.com", "ya29.vF9dft4qmTc2Nvb3RlckBhdHRhdmljYS5vY29tCg"), "xoauth2");
    check_str(acm_buf_str(&b), "dXNlcj1zb21ldXNlckBleGFtcGxlLmNvbQFhdXRoPUJlYXJlciB5YTI5LnZGOWRmdDRxbVRjMk52YjNSbGNrQmhkSFJoZG1sallTNXZZMjl0Q2cBAQ==", "xoauth2");
    acm_buf_clear(&b);
    CHECK(acm_sasl_plain(&b, "dale", "secret"), "plain");
    check_str(acm_buf_str(&b), "AGRhbGUAc2VjcmV0", "plain");
    acm_buf_clear(&b);
    CHECK(!acm_sasl_plain(&b, "dale", "sec\nret"), "plain refuses a line break");
    CHECK(!acm_sasl_xoauth2(&b, "dale", "tok en"), "xoauth2 refuses a space in the token");
    acm_buf_free(&b);
}

static void unit_mutf7(void)
{
    char *s;
    s = acm_mutf7_decode("&U,BTFw-/&ZeVnLIqe-");
    check_str(s, "\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", "mutf7 decode (RFC 3501's example)");
    free(s);
    s = acm_mutf7_decode("Gr&APw-&AN8-e");
    check_str(s, "Gr\xc3\xbc\xc3\x9f" "e", "mutf7 decode Gruesse");
    free(s);
    s = acm_mutf7_decode("Tom &- Jerry");
    check_str(s, "Tom & Jerry", "mutf7 decode &-");
    free(s);
    s = acm_mutf7_encode("\xe5\x8f\xb0\xe5\x8c\x97/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e");
    check_str(s, "&U,BTFw-/&ZeVnLIqe-", "mutf7 encode");
    free(s);
    s = acm_mutf7_encode("Tom & Jerry");
    check_str(s, "Tom &- Jerry", "mutf7 encode &");
    free(s);
    s = acm_mutf7_encode("\xf0\x9f\x93\xa7 Mail");       /* outside the BMP: a surrogate pair */
    {
        char *back = acm_mutf7_decode(s);
        check_str(back, "\xf0\x9f\x93\xa7 Mail", "mutf7 round trip of U+1F4E7");
        free(back);
    }
    free(s);
}

static void trace(void *user, int sent, const char *line)
{
    acm_buf *log = user;
    acm_buf_printf(log, "%s %s\n", sent ? "C:" : "S:", line);
}

static int imap_session(const char *scenario, int port)
{
    acm_imap m;
    acm_buf log;
    int security = !strcmp(scenario, "tls") ? ACM_IMAP_TLS : !strcmp(scenario, "starttls") ? ACM_IMAP_STARTTLS : ACM_IMAP_PLAIN;
    int start = failures;
    acm_imap_init(&m);
    acm_buf_init(&log);
    m.trace = trace;
    m.trace_user = &log;
    if (!acm_imap_connect(&m, "localhost", port, security)) {
        CHECK(0, "connect: %s", acm_imap_error(&m));
        goto done;
    }
    if (!strcmp(scenario, "xoauth2") || !strcmp(scenario, "xoauth2bad")) {
        int ok = acm_imap_login_xoauth2(&m, "dale@example.com", "ya29.test-token");
        if (!strcmp(scenario, "xoauth2bad")) {
            CHECK(!ok, "a refused token logs in");
            CHECK(strstr(acm_imap_error(&m), "AUTHENTICATIONFAILED") && strstr(acm_imap_error(&m), "\"status\":\"401\""),
                  "the error says why: %s", acm_imap_error(&m));
            acm_imap_close(&m);
            goto done;
        }
        CHECK(ok, "xoauth2 login: %s", acm_imap_error(&m));
    } else CHECK(acm_imap_login(&m, "dale@example.com", "pa\"ss\\word"), "login: %s", acm_imap_error(&m));
    CHECK(acm_imap_has(&m, "MOVE"), "capabilities after login");
    {
        acm_imap_folder *f;
        int n;
        CHECK(acm_imap_list(&m, &f, &n), "list: %s", acm_imap_error(&m));
        CHECK(n == 5, "5 folders, got %d", n);
        if (n == 5) {
            check_str(f[0].name, "INBOX", "folder 0");
            CHECK(f[0].delimiter == '/', "INBOX's delimiter");
            CHECK(f[1].attrs & ACM_FOLDER_NOSELECT && f[1].attrs & ACM_FOLDER_CHILDREN, "[Gmail] is not selectable and has children");
            check_str(f[2].display, "[Gmail]/Sent Mail", "folder 2");
            check_str(f[3].display, "Gr\xc3\xbc\xc3\x9f" "e", "folder 3 in UTF-8");
            check_str(f[3].name, "Gr&APw-&AN8-e", "folder 3 as sent");
            check_str(f[4].name, "Braces {in} it", "folder 4, a literal");
            CHECK(f[4].delimiter == 0, "NIL delimiter");
        }
        acm_imap_free_folders(f, n);
    }
    CHECK(acm_imap_select(&m, "INBOX", 0), "select: %s", acm_imap_error(&m));
    CHECK(m.exists == 2 && m.uidvalidity == 1234 && m.uidnext == 43 && !m.readonly,
          "select state: exists %lu uidvalidity %lu uidnext %lu readonly %d", m.exists, m.uidvalidity, m.uidnext, m.readonly);
    {
        acm_imap_msg *v;
        int n;
        CHECK(acm_imap_fetch_summaries(&m, 1, &v, &n), "fetch: %s", acm_imap_error(&m));
        CHECK(n == 2, "2 summaries, got %d", n);
        if (n == 2) {
            CHECK(v[0].uid == 41 && v[0].flags == ACM_F_SEEN && v[0].size == 1001, "message 41: uid %lu flags %u size %lu", v[0].uid, v[0].flags, v[0].size);
            CHECK(v[1].uid == 42 && v[1].flags == 0 && v[1].size == 1002, "message 42");
            CHECK(v[0].header && strstr(v[0].header, "Subject: =?ISO-8859-1?Q?Gr=FC=DFe_aus_Amiga?="), "message 41's header");
            CHECK(v[1].header && strstr(v[1].header, "Subject: (parens) and \"quotes\""), "message 42's header");
            check_str(v[0].internaldate, "02-Oct-2026 21:01:00 +0100", "internal date");
        }
        acm_imap_free_msgs(v, n);
        CHECK(m.exists == 3, "a new message noticed during the fetch: exists %lu", m.exists);
    }
    {
        acm_buf body;
        acm_buf_init(&body);
        CHECK(acm_imap_fetch_message(&m, 42, &body), "fetch message: %s", acm_imap_error(&m));
        CHECK(strstr(acm_buf_str(&body), "Line two with {braces} and ) a paren\r\n") != NULL, "the message's text");
        CHECK(body.len == 101, "the message is 101 bytes, got %lu", (unsigned long)body.len);
        acm_buf_free(&body);
    }
    CHECK(acm_imap_set_flags(&m, 42, ACM_F_SEEN | ACM_F_FLAGGED, 1), "store: %s", acm_imap_error(&m));
    acm_imap_logout(&m);
done:
    if (failures != start) printf("--- protocol ---\n%s", acm_buf_str(&log));
    else if (!strcmp(scenario, "plain")) {
        /* the password never shows in the protocol log */
        CHECK(!strstr(acm_buf_str(&log), "AGRhbGVAZXhhbXBsZS5jb20A"), "the PLAIN response is hidden in the log");
    }
    acm_buf_free(&log);
    acm_imap_dispose(&m);
    return failures == start;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "unit")) {
        unit_base64();
        unit_sasl();
        unit_mutf7();
    } else if (argc >= 4 && !strcmp(argv[1], "imap")) {
        imap_session(argv[2], atoi(argv[3]));
    } else {
        fprintf(stderr, "usage: test_engine unit | imap SCENARIO PORT\n");
        return 2;
    }
    printf("%s: %d failure%s\n", argc >= 3 ? argv[2] : argv[1], failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
