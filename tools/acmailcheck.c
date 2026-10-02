/* ACMailCheck: signs in to an IMAP server, lists its folders and the
 * newest subjects in INBOX. The engine's first run on a real Amiga, and a
 * check of an account's settings. Builds for the host too.
 *
 *   ACMailCheck HOST PORT SECURITY USER SECRETFILE [XOAUTH2]
 *     SECURITY   tls, starttls or plain
 *     SECRETFILE a file holding the password, or the OAuth access token
 *                with XOAUTH2 (so no secret appears on the command line)
 */
#include "acm_imap.h"
#include "acm_text.h"
#ifdef __amigaos__
#include "acm_stack.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __amigaos__
static const char version[] __attribute__((used)) = "$VER: ACMailCheck 0.1 (2.10.2026)";
#endif

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
    char *l = acm_utf8_to_latin1(utf8);
    fputs(l ? l : "", stdout);
    free(l);
#else
    fputs(utf8, stdout);
#endif
}

static int check(int argc, char **argv)
{
    acm_imap *m;
    acm_imap_folder *folders;
    acm_imap_msg *msgs;
    int nf, nm, i, security, xoauth2, ok, rc = 10;
    char *secret, err[200];
    int steps = getenv("ACMAIL_STEPS") != NULL;
    setvbuf(stdout, NULL, _IONBF, 0);       /* every line out at once: a crash still leaves what came before */
    if (argc < 6) {
        printf("usage: ACMailCheck HOST PORT tls|starttls|plain USER SECRETFILE [XOAUTH2]\n");
        return 20;
    }
    security = !strcmp(argv[3], "tls") ? ACM_IMAP_TLS : !strcmp(argv[3], "starttls") ? ACM_IMAP_STARTTLS : ACM_IMAP_PLAIN;
    xoauth2 = argc > 6 && !strcmp(argv[6], "XOAUTH2");
    if (steps) printf("step: arguments read\n");
    if (!(secret = read_secret(argv[5]))) { printf("ACMailCheck: %s could not be read\n", argv[5]); return 20; }
    if (steps) printf("step: secret read, opening the network\n");
    if (!acm_net_init(err, sizeof err)) { printf("ACMailCheck: %s\n", err); free(secret); return 20; }
    if (!(m = calloc(1, sizeof *m))) { printf("ACMailCheck: out of memory\n"); free(secret); acm_net_cleanup(); return 20; }
    acm_imap_init(m);
    printf("Connecting to %s:%s (%s)...\n", argv[1], argv[2], argv[3]);
    if (!acm_imap_connect(m, argv[1], atoi(argv[2]), security)) { printf("Failed: %s\n", acm_imap_error(m)); goto out; }
    ok = xoauth2 ? acm_imap_login_xoauth2(m, argv[4], secret) : acm_imap_login(m, argv[4], secret);
    if (!ok) { printf("Sign-in failed: %s\n", acm_imap_error(m)); goto out; }
    printf("Signed in as %s.\n", argv[4]);
    if (acm_imap_list(m, &folders, &nf)) {
        printf("%d folders:\n", nf);
        for (i = 0; i < nf; i++) { printf("  "); put(folders[i].display); printf("%s\n", folders[i].attrs & ACM_FOLDER_NOSELECT ? "  (not a mailbox)" : ""); }
        acm_imap_free_folders(folders, nf);
    } else printf("LIST failed: %s\n", acm_imap_error(m));
    if (acm_imap_select(m, "INBOX", 1)) {
        unsigned long from = m->uidnext > 10 ? m->uidnext - 10 : 1;
        printf("INBOX: %lu messages. The newest:\n", m->exists);
        if (acm_imap_fetch_summaries(m, from, &msgs, &nm)) {
            for (i = nm - 1; i >= 0; i--) {
                const char *h = msgs[i].header ? msgs[i].header : "";
                char *raw = acm_hdr_get(h, strlen(h), "Subject"), *from_raw = acm_hdr_get(h, strlen(h), "From");
                char *subject = acm_hdr_decode(raw ? raw : "(no subject)"), *name = NULL, *addr = NULL;
                acm_hdr_address(from_raw ? from_raw : "", &name, &addr);
                printf("  %c ", msgs[i].flags & ACM_F_SEEN ? ' ' : '*');
                put(name && *name ? name : addr ? addr : "?");
                printf(": ");
                put(subject ? subject : "");
                printf("\n");
                free(raw); free(from_raw); free(subject); free(name); free(addr);
            }
            acm_imap_free_msgs(msgs, nm);
        } else printf("FETCH failed: %s\n", acm_imap_error(m));
    } else printf("INBOX could not be opened: %s\n", acm_imap_error(m));
    acm_imap_logout(m);
    rc = 0;
out:
    memset(secret, 0, strlen(secret));
    free(secret);
    acm_imap_dispose(m);
    free(m);
    acm_net_cleanup();
    return rc;
}

int main(int argc, char **argv)
{
#ifdef __amigaos__
    return acm_run_with_stack(65536, check, argc, argv);
#else
    return check(argc, argv);
#endif
}
