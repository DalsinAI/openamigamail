/* oam_net on AmigaOS 3.x and AROS 68k: bsdsocket.library (any stack:
 * Roadshow, AmiTCP, Miami, or OpenSocket, AmigaChrome's) and, for TLS,
 * the Team's own opentls.library (OpenTLS, DalsinAI/openamigatls) first,
 * then AmiSSL 5 when OpenMail is built with it (OAM_AMISSL) and OpenTLS
 * isn't installed. The calling task opens them in oam_net_init: their bases
 * are the task's own. OpenTLS checks the server's certificate against its
 * CA list (S:OpenTLS/) and the server's name; AmiSSL against AmiSSL:Certs.
 * 0.3.1 (10 October 2026): OpenTLS, so mail works on a system without AmiSSL. */
#include "oam_net.h"

#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <proto/opentls.h>
#include <libraries/opentls.h>
#ifdef OAM_AMISSL
#include <proto/amisslmaster.h>
#include <proto/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>
#include <amissl/amissl.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *SocketBase, *OpenTLSBase;
#ifdef OAM_AMISSL
struct Library *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;
#endif
void (*oam_net_progress)(const char *step);
#define STEP(s) do { if (oam_net_progress) oam_net_progress(s); } while (0)

static struct OTContext *ot_ctx;           /* OpenTLS: one context for the task */
static int net_ready;

struct oam_conn {
    long fd;
    struct OTConnection *ot;                /* OpenTLS */
#ifdef OAM_AMISSL
    SSL_CTX *ctx;                           /* AmiSSL */
    SSL *ssl;
#endif
    int timeout;            /* seconds a read waits; 0 for ever */
    char error[200];
};

static void set_err(char *err, size_t errlen, const char *what)
{
    if (err && errlen) snprintf(err, errlen, "%s", what);
}

/* OpenTLS when it is installed: 1 ready, 0 not there or not usable */
static int open_opentls(void)
{
    struct OTContextConfig config;
    LONG error = 0;
    if (ot_ctx) return 1;
    STEP("opening opentls.library");
    OpenTLSBase = OpenLibrary((CONST_STRPTR)OPENTLSLIB_NAME, OPENTLSLIB_VERSION);
    if (!OpenTLSBase) return 0;
    memset(&config, 0, sizeof config);
    config.Size = sizeof config;
    config.MinVersion = OT_TLS12;
    ot_ctx = OT_NewContext(&config, &error);
    if (!ot_ctx) {
        CloseLibrary(OpenTLSBase);
        OpenTLSBase = NULL;
        return 0;
    }
    return 1;
}

#ifdef OAM_AMISSL
static int open_amissl(char *err, size_t errlen)
{
    if (AmiSSLBase) return 1;
    STEP("opening amisslmaster.library");
    if (!(AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION))) {
        set_err(err, errlen, "No TLS library: install OpenTLS (opentls.library) or AmiSSL 5.");
        return 0;
    }
    STEP("opening AmiSSL");
    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&errno,
                       TAG_DONE) != 0) {
        AmiSSLBase = NULL;
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        set_err(err, errlen, "AmiSSL could not be opened: it may be older than OpenMail needs.");
        return 0;
    }
    return 1;
}
#endif

int oam_net_init(char *err, size_t errlen)
{
    if (net_ready) return 1;
    STEP("opening bsdsocket.library");
    if (!SocketBase && !(SocketBase = OpenLibrary("bsdsocket.library", 4))) {
        set_err(err, errlen, "No TCP/IP stack is running (bsdsocket.library).");
        return 0;
    }
    SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof errno)), (ULONG)&errno, TAG_DONE);
    if (!open_opentls()) {
#ifdef OAM_AMISSL
        if (!open_amissl(err, errlen)) { oam_net_cleanup(); return 0; }
#else
        set_err(err, errlen, "OpenTLS (opentls.library) is not installed: OpenUp's OpenTLS part puts it in LIBS:.");
        oam_net_cleanup();
        return 0;
#endif
    }
    net_ready = 1;
    STEP("network ready");
    return 1;
}

void oam_net_cleanup(void)
{
    if (ot_ctx) { OT_FreeContext(ot_ctx); ot_ctx = NULL; }
    if (OpenTLSBase) { CloseLibrary(OpenTLSBase); OpenTLSBase = NULL; }
#ifdef OAM_AMISSL
    if (AmiSSLBase) { CloseAmiSSL(); AmiSSLBase = AmiSSLExtBase = NULL; }
    if (AmiSSLMasterBase) { CloseLibrary(AmiSSLMasterBase); AmiSSLMasterBase = NULL; }
#endif
    if (SocketBase) { CloseLibrary(SocketBase); SocketBase = NULL; }
    net_ready = 0;
}

void oam_net_set_timeout(oam_conn *c, int seconds) { c->timeout = seconds; }

static int start_opentls(oam_conn *c, const char *host, char *err, size_t errlen)
{
    LONG error = 0, r;
    c->ot = OT_NewConnection(ot_ctx, (CONST_STRPTR)host, &error);
    if (!c->ot) {
        char msg[200];
        CONST_STRPTR why = OT_ErrorString(error);
        snprintf(msg, sizeof msg, "TLS could not be set up: %s.", (const char *)why);
        set_err(err, errlen, msg);
        return 0;
    }
    OT_SetSocket(c->ot, c->fd, SocketBase);
    STEP("TLS handshake");
    r = OT_Handshake(c->ot);
    if (r != OTERR_OK) {
        char msg[200];
        CONST_STRPTR why = OT_GetErrorText(c->ot);
        if (r == OTERR_UNTRUSTED || r == OTERR_HOSTNAME || r == OTERR_EXPIRED || r == OTERR_BADCERT || r == OTERR_TRUSTSTORE)
            snprintf(msg, sizeof msg, "The server's certificate was not accepted: %s.", why ? (const char *)why : "unknown");
        else
            snprintf(msg, sizeof msg, "The TLS handshake failed: %s.", why ? (const char *)why : "unknown");
        set_err(err, errlen, msg);
        return 0;
    }
    return 1;
}

#ifdef OAM_AMISSL
static int start_amissl(oam_conn *c, const char *host, char *err, size_t errlen)
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
    STEP("TLS handshake");
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
#endif

static int start_tls(oam_conn *c, const char *host, char *err, size_t errlen)
{
    if (ot_ctx) return start_opentls(c, host, err, errlen);
#ifdef OAM_AMISSL
    if (AmiSSLBase) return start_amissl(c, host, err, errlen);
#endif
    set_err(err, errlen, "No TLS library is open.");
    return 0;
}

static int tls_on(oam_conn *c)
{
#ifdef OAM_AMISSL
    if (c->ssl) return 1;
#endif
    return c->ot != NULL;
}

oam_conn *oam_net_connect(const char *host, int port, int tls, char *err, size_t errlen)
{
    struct hostent *he;
    struct sockaddr_in sa;
    oam_conn *c;
    if (!oam_net_init(err, errlen)) return NULL;
    c = calloc(1, sizeof *c);
    if (!c) { set_err(err, errlen, "Out of memory."); return NULL; }
    c->fd = -1;
    c->timeout = 60;
    STEP("resolving the name");
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
    STEP("connecting");
    c->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (c->fd < 0 || connect(c->fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        char msg[200];
        snprintf(msg, sizeof msg, "%s did not answer on port %d.", host, port);
        set_err(err, errlen, msg);
        oam_net_close(c);
        return NULL;
    }
    if (tls && !start_tls(c, host, err, errlen)) { oam_net_close(c); return NULL; }
    return c;
}

int oam_net_starttls(oam_conn *c, const char *host, char *err, size_t errlen)
{
    if (tls_on(c)) return 1;
    return start_tls(c, host, err, errlen);
}

/* 1 when the socket has data within the timeout */
static int readable(oam_conn *c)
{
    fd_set rd;
    struct timeval tv;
    if (c->ot && OT_Pending(c->ot) > 0) return 1;
#ifdef OAM_AMISSL
    if (c->ssl && SSL_pending(c->ssl) > 0) return 1;
#endif
    if (!c->timeout) return 1;
    FD_ZERO(&rd);
    FD_SET(c->fd, &rd);
    tv.tv_sec = c->timeout;
    tv.tv_usec = 0;
    return WaitSelect(c->fd + 1, &rd, NULL, NULL, &tv, NULL) > 0;
}

long oam_net_read(oam_conn *c, void *buf, long len)
{
    long n;
    if (!readable(c)) { snprintf(c->error, sizeof c->error, "The server stopped answering."); return -1; }
    if (c->ot) {
        n = OT_Read(c->ot, buf, len);
        if (n == OTERR_CLOSED) return 0;      /* closed without close_notify: the end, as a plain socket's */
        if (n < 0) {
            CONST_STRPTR why = OT_GetErrorText(c->ot);
            snprintf(c->error, sizeof c->error, "Reading from the server failed (TLS: %s).", why ? (const char *)why : "error");
            return -1;
        }
        return n;
    }
#ifdef OAM_AMISSL
    if (c->ssl) {
        n = SSL_read(c->ssl, buf, (int)len);
        if (n <= 0) {
            if (SSL_get_error(c->ssl, (int)n) == SSL_ERROR_ZERO_RETURN) return 0;
            snprintf(c->error, sizeof c->error, "Reading from the server failed (TLS).");
            return -1;
        }
        return n;
    }
#endif
    n = recv(c->fd, buf, len, 0);
    if (n < 0) snprintf(c->error, sizeof c->error, "Reading from the server failed (error %d).", errno);
    return n;
}

long oam_net_write(oam_conn *c, const void *buf, long len)
{
    const char *p = buf;
    long left = len;
    while (left > 0) {
        long n;
        if (c->ot) n = OT_Write(c->ot, (CONST_APTR)p, left);
#ifdef OAM_AMISSL
        else if (c->ssl) n = SSL_write(c->ssl, p, (int)left);
#endif
        else n = send(c->fd, (APTR)p, left, 0);
        if (n <= 0) { snprintf(c->error, sizeof c->error, "Sending to the server failed."); return -1; }
        p += n;
        left -= n;
    }
    return len;
}

const char *oam_net_error(oam_conn *c) { return c->error[0] ? c->error : "No error."; }

void oam_net_close(oam_conn *c)
{
    if (!c) return;
    if (c->ot) { OT_Close(c->ot); OT_FreeConnection(c->ot); }
#ifdef OAM_AMISSL
    if (c->ssl) { SSL_shutdown(c->ssl); SSL_free(c->ssl); }
    if (c->ctx) SSL_CTX_free(c->ctx);
#endif
    if (c->fd >= 0) CloseSocket(c->fd);
    free(c);
}
