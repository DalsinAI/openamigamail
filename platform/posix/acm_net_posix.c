/* acm_net for the host (Linux, macOS): POSIX sockets and OpenSSL. It is
 * how the engine's tests run on the PC; the Amiga's is platform/amiga.
 * Certificates are checked against the system's store, or the file in
 * ACM_CA_FILE (the tests' own CA). */
#include "acm_net.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

struct acm_conn {
    int fd;
    SSL_CTX *ctx;
    SSL *ssl;
    char error[256];
};

int acm_net_init(char *err, size_t errlen) { (void)err; (void)errlen; return 1; }

void acm_net_cleanup(void) {}

void acm_net_set_timeout(acm_conn *c, int seconds)
{
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
}

static void set_err(char *err, size_t errlen, const char *what)
{
    if (err && errlen) snprintf(err, errlen, "%s", what);
}

static int start_tls(acm_conn *c, const char *host, char *err, size_t errlen)
{
    const char *ca = getenv("ACM_CA_FILE");
    long verify;
    c->ctx = SSL_CTX_new(TLS_client_method());
    if (!c->ctx) { set_err(err, errlen, "TLS could not be set up."); return 0; }
    SSL_CTX_set_min_proto_version(c->ctx, TLS1_2_VERSION);
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
    if (ca ? !SSL_CTX_load_verify_locations(c->ctx, ca, NULL) : !SSL_CTX_set_default_verify_paths(c->ctx)) {
        set_err(err, errlen, "The trusted certificates could not be loaded.");
        return 0;
    }
    c->ssl = SSL_new(c->ctx);
    if (!c->ssl) { set_err(err, errlen, "TLS could not be set up."); return 0; }
    SSL_set_tlsext_host_name(c->ssl, host);
    SSL_set1_host(c->ssl, host);
    SSL_set_fd(c->ssl, c->fd);
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
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    acm_conn *c = calloc(1, sizeof *c);
    int rc;
    if (!c) { set_err(err, errlen, "Out of memory."); return NULL; }
    c->fd = -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portstr, sizeof portstr, "%d", port);
    rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc) {
        char msg[200];
        snprintf(msg, sizeof msg, "The server %s was not found.", host);
        set_err(err, errlen, msg);
        free(c);
        return NULL;
    }
    for (ai = res; ai; ai = ai->ai_next) {
        c->fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (c->fd < 0) continue;
        if (connect(c->fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(c->fd);
        c->fd = -1;
    }
    freeaddrinfo(res);
    if (c->fd < 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s did not answer on port %d.", host, port);
        set_err(err, errlen, msg);
        free(c);
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

long acm_net_read(acm_conn *c, void *buf, long len)
{
    long n;
    if (c->ssl) {
        n = SSL_read(c->ssl, buf, (int)len);
        if (n <= 0) {
            int e = SSL_get_error(c->ssl, (int)n);
            if (e == SSL_ERROR_ZERO_RETURN) return 0;
            snprintf(c->error, sizeof c->error, "Reading from the server failed (TLS).");
            return -1;
        }
        return n;
    }
    do n = (long)recv(c->fd, buf, (size_t)len, 0); while (n < 0 && errno == EINTR);
    if (n < 0) snprintf(c->error, sizeof c->error, "Reading from the server failed: %s.", strerror(errno));
    return n;
}

long acm_net_write(acm_conn *c, const void *buf, long len)
{
    const char *p = buf;
    long left = len;
    while (left > 0) {
        long n;
        if (c->ssl) n = SSL_write(c->ssl, p, (int)left);
        else do n = (long)send(c->fd, p, (size_t)left, MSG_NOSIGNAL); while (n < 0 && errno == EINTR);
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
    if (c->fd >= 0) close(c->fd);
    free(c);
}
