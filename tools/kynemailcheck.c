/* KyneMailCheck: signs in to an IMAP server, lists its folders and the
 * newest subjects in INBOX. The engine's first run on a real Amiga, and a
 * check of an account's settings. Builds for the host too.
 *
 *   KyneMailCheck HOST PORT SECURITY USER SECRETFILE [XOAUTH2]
 *     SECURITY   tls, starttls or plain
 *     SECRETFILE a file holding the password, or the OAuth access token
 *                with XOAUTH2 (so no secret appears on the command line)
 *     USER "-"   only connects (TLS and all) and shows what the server
 *                offers, then logs out: no sign-in
 */
#include "km_imap.h"
#include "km_text.h"
#ifdef __amigaos__
#include "km_stack.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
static const char version[] __attribute__((used)) = "$VER: KyneMailCheck 0.1 (2.10.2026)";
#endif

static void progress(const char *step) { printf("  ... %s\n", step); }

static char *read_secret(const char *path)
{
    FILE *f = fopen(path, "rb");
    char buf[4096];
    size_t n;
    char *s;
    if (!f) return NULL;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) n--;
    buf[n] = 0;
    s = malloc(n + 1);
    if (s) memcpy(s, buf, n + 1);
    memset(buf, 0, sizeof buf);
    return s;
}

/* text for the console: Latin-1 on the Amiga, UTF-8 elsewhere */
static void put(const char *utf8)
{
#ifdef __amigaos__
    char *l = km_utf8_to_latin1(utf8);
    fputs(l ? l : "", stdout);
    free(l);
#else
    fputs(utf8, stdout);
#endif
}

static int check(int argc, char **argv)
{
    km_imap *m;
    km_imap_folder *folders;
    km_imap_msg *msgs;
    int nf, nm, i, security, xoauth2, ok, rc = 10;
    char *secret, err[200];
    int steps = getenv("KYNEMAIL_STEPS") != NULL;
    setvbuf(stdout, NULL, _IONBF, 0);       /* every line out at once: a crash still leaves what came before */
    if (argc < 6) {
        printf("usage: KyneMailCheck HOST PORT tls|starttls|plain USER SECRETFILE [XOAUTH2]\n");
        return 20;
    }
    security = !strcmp(argv[3], "tls") ? KM_IMAP_TLS : !strcmp(argv[3], "starttls") ? KM_IMAP_STARTTLS : KM_IMAP_PLAIN;
    xoauth2 = argc > 6 && !strcmp(argv[6], "XOAUTH2");
    if (!(secret = read_secret(argv[5]))) { printf("KyneMailCheck: %s could not be read\n", argv[5]); return 20; }
    if (steps || !strcmp(argv[4], "-")) km_net_progress = progress;      /* a probe shows each step */
    if (!km_net_init(err, sizeof err)) { printf("KyneMailCheck: %s\n", err); free(secret); return 20; }
    if (!(m = calloc(1, sizeof *m))) { printf("KyneMailCheck: out of memory\n"); free(secret); km_net_cleanup(); return 20; }
    km_imap_init(m);
    printf("Connecting to %s:%s (%s)...\n", argv[1], argv[2], argv[3]);
    if (!km_imap_connect(m, argv[1], atoi(argv[2]), security)) { printf("Failed: %s\n", km_imap_error(m)); goto out; }
    if (!strcmp(argv[4], "-")) {
        printf("Connected%s. The server offers:%s\n", security == KM_IMAP_PLAIN ? "" : " over TLS, its certificate checked", km_buf_str(&m->caps));
        km_imap_logout(m);
        rc = 0;
        goto out;
    }
    ok = xoauth2 ? km_imap_login_xoauth2(m, argv[4], secret) : km_imap_login(m, argv[4], secret);
    if (!ok) { printf("Sign-in failed: %s\n", km_imap_error(m)); goto out; }
    printf("Signed in as %s.\n", argv[4]);
    if (km_imap_list(m, &folders, &nf)) {
        printf("%d folders:\n", nf);
        for (i = 0; i < nf; i++) { printf("  "); put(folders[i].display); printf("%s\n", folders[i].attrs & KM_FOLDER_NOSELECT ? "  (not a mailbox)" : ""); }
        km_imap_free_folders(folders, nf);
    } else printf("LIST failed: %s\n", km_imap_error(m));
    if (km_imap_select(m, "INBOX", 1)) {
        unsigned long from = m->uidnext > 10 ? m->uidnext - 10 : 1;
        printf("INBOX: %lu messages. The newest:\n", m->exists);
        if (km_imap_fetch_summaries(m, from, &msgs, &nm)) {
            for (i = nm - 1; i >= 0; i--) {
                const char *h = msgs[i].header ? msgs[i].header : "";
                char *raw = km_hdr_get(h, strlen(h), "Subject"), *from_raw = km_hdr_get(h, strlen(h), "From");
                char *subject = km_hdr_decode(raw ? raw : "(no subject)"), *name = NULL, *addr = NULL;
                km_hdr_address(from_raw ? from_raw : "", &name, &addr);
                printf("  %c ", msgs[i].flags & KM_F_SEEN ? ' ' : '*');
                put(name && *name ? name : addr ? addr : "?");
                printf(": ");
                put(subject ? subject : "");
                printf("\n");
                free(raw); free(from_raw); free(subject); free(name); free(addr);
            }
            km_imap_free_msgs(msgs, nm);
        } else printf("FETCH failed: %s\n", km_imap_error(m));
    } else printf("INBOX could not be opened: %s\n", km_imap_error(m));
    km_imap_logout(m);
    rc = 0;
out:
    memset(secret, 0, strlen(secret));
    free(secret);
    km_imap_dispose(m);
    free(m);
    km_net_cleanup();
    return rc;
}

int main(int argc, char **argv)
{
#ifdef __amigaos__
    return km_run_with_stack(65536, check, argc, argv);
#else
    return check(argc, argv);
#endif
}
