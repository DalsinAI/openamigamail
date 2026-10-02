/* acm_net on AmigaOS 3.x and AROS 68k: bsdsocket.library (any stack:
 * Roadshow, AmiTCP, Miami, or AmigaChrome's ACNet) and AmiSSL 5 (OpenSSL 3).
 * The calling task opens both in acm_net_init: their bases are the task's
 * own. Certificates are checked against AmiSSL's store (AmiSSL:Certs), and
 * the server's name against its certificate. */
#include "acm_net.h"

#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *SocketBase, *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;

struct acm_conn {
    long fd;
    SSL_CTX *ctx;
    SSL *ssl;
    int timeout;            /* seconds a read waits; 0 for ever */
    char error[200];
};

static void set_err(char *err, size_t errlen, const char *what)
{
    if (err && errlen) snprintf(err, errlen, "%s", what);
}

int acm_net_init(char *err, size_t errlen)
{
    if (AmiSSLBase) return 1;
    if (!SocketBase && !(SocketBase = OpenLibrary("bsdsocket.library", 4))) {
        set_err(err, errlen, "No TCP/IP stack is running (bsdsocket.library).");
        return 0;
    }
    SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof errno)), (ULONG)&errno, TAG_DONE);
    if (!(AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        set_err(err, errlen, "AmiSSL 5 is not installed (amisslmaster.library).");
        acm_net_cleanup();
        return 0;
    }
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno,
                       TAG_DONE) != 0) {
        AmiSSLBase = NULL;
        set_err(err, errlen, "AmiSSL could not be opened: it may be older than ACMail needs.");
        acm_net_cleanup();
        return 0;
    }
    return 1;
}

void acm_net_cleanup(void)
{
    if (AmiSSLBase) { CloseAmiSSL(); AmiSSLBase = AmiSSLExtBase = NULL; }
    if (AmiSSLMasterBase) { CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL; }
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
}

void acm_net_set_timeout(acm_conn *c, int seconds) { c->timeout = seconds; }

static int start_tls(acm_conn *c, const char *host, char *err, size_t errlen)
{
    long verify;
    c->ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ctx) { set_err(err, errlen, "TLS could not be set up."); return 0; }
    SSL_CTX_set_min_proto_version(c->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
    if (!SSL_CTX_set_default_verify_paths(c->ctx)) {
        set_err(err, errlen, "AmiSSL's trusted certificates could not be loaded.");
        return 0;
    }
    c->ssl = SSL_new(c->ctx);
    if (!c->ssl) { set_err(err, errlen, "TLS could not be set up."); return 0; }
    SSL_set_tlsext_host_name(c->ssl, host);
    SSL_set1_host(c->ssl, host);
    SSL_set_fd(c->ssl, (int)c->fd);
    if (SSL_connect(c->ssl) != 1) {
        verify = SSL_get_verify_result(c->ssl);
        if (verify != X509_V_OK) {
            char msg[200];
            snprintf(msg, sizeof msg, "The server's certificate was not accepted: %s.", X509_verify_cert_error_string(verify));
            set_err(err, errlen, msg);
        } else set_err(err, errlen, "The TLS handshake failed.");
        return 0;
    }
    return 1;
}

acm_conn *acm_net_connect(const char *host, int port, int tls, char *err, size_t errlen)
{
    struct hostent *he;
    struct sockaddr_in sa;
    acm_conn *c;
    if (!acm_net_init(err, errlen)) return NULL;
    c = calloc(1, sizeof *c);
    if (!c) { set_err(err, errlen, "Out of memory."); return NULL; }
    c->fd = -1;
    c->timeout = 60;
    he = gethostbyname((STRPTR)host);
    if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) {
        char msg[200];
        snprintf(msg, sizeof msg, "The server %s was not found.", host);
        set_err(err, errlen, msg);
        free(c);
        return NULL;
    }
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0 || connect(c->fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s did not answer on port %d.", host, port);
        set_err(err, errlen, msg);
        acm_net_close(c);
        return NULL;
    }
    if (tls && !start_tls(c, host, err, errlen)) { acm_net_close(c); return NULL; }
    return c;
}

int acm_net_starttls(acm_conn *c, const char *host, char *err, size_t errlen)
{
    if (c->ssl) return 1;
    return start_tls(c, host, err, errlen);
}

/* 1 when the socket has data within the timeout */
static int readable(acm_conn *c)
{
    fd_set rd;
    struct timeval tv;
    if (c->ssl && SSL_pending(c->ssl) > 0) return 1;
    if (!c->timeout) return 1;
    FD_ZERO(&rd);
    FD_SET(c->fd, &rd);
    tv.tv_sec = c->timeout;
    tv.tv_usec = 0;
    return WaitSelect(c->fd + 1, &rd, NULL, NULL, &tv, NULL) > 0;
}

long acm_net_read(acm_conn *c, void *buf, long len)
{
    long n;
    if (!readable(c)) { snprintf(c->error, sizeof c->error, "The server stopped answering."); return -1; }
    if (c->ssl) {
        n = SSL_read(c->ssl, buf, (int)len);
        if (n <= 0) {
            if (SSL_get_error(c->ssl, (int)n) == SSL_ERROR_ZERO_RETURN) return 0;
            snprintf(c->error, sizeof c->error, "Reading from the server failed (TLS).");
            return -1;
        }
        return n;
    }
    n = recv(c->fd, buf, len, 0);
    if (n < 0) snprintf(c->error, sizeof c->error, "Reading from the server failed (error %d).", errno);
    return n;
}

long acm_net_write(acm_conn *c, const void *buf, long len)
{
    const char *p = buf;
    long left = len;
    while (left > 0) {
        long n = c->ssl ? SSL_write(c->ssl, p, (int)left) : send(c->fd, (APTR)p, left, 0);
        if (n <= 0) { snprintf(c->error, sizeof c->error, "Sending to the server failed."); return -1; }
        p += n;
        left -= n;
    }
    return len;
}

const char *acm_net_error(acm_conn *c) { return c->error[0] ? c->error : "No error."; }

void acm_net_close(acm_conn *c)
{
    if (!c) return;
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->ctx) SSL_CTX_free(c->ctx);
    if (c->fd >= 0) CloseSocket(c->fd);
    free(c);
}
