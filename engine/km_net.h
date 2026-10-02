/* km_net: the one place the engine meets the network. Each platform
 * supplies these (platform/posix for the host's tests, platform/amiga for
 * bsdsocket.library and AmiSSL); the engine never includes a socket header.
 * Errors come back as text in err (at most errlen bytes). */
#ifndef KM_NET_H
#define KM_NET_H

#include <stddef.h>

typedef struct km_conn km_conn;

/* Per task: on the Amiga, bsdsocket.library and AmiSSL belong to the task
 * that opens them, so the task that talks to servers calls km_net_init
 * first and km_net_cleanup at the end. 1 when the network is there. */
int km_net_init(char *err, size_t errlen);
void km_net_cleanup(void);
/* when set, the platform layer reports each step (opening the stack and
 * TLS, resolving, connecting, the handshake) to it: for check tools */
extern void (*km_net_progress)(const char *step);
/* how long a read waits for the server before giving up (seconds) */
void km_net_set_timeout(km_conn *c, int seconds);

/* tls: 1 to start TLS at once (IMAPS 993, SMTPS 465), 0 for a plain
 * connection that may be upgraded with km_net_starttls */
km_conn *km_net_connect(const char *host, int port, int tls, char *err, size_t errlen);
int km_net_starttls(km_conn *c, const char *host, char *err, size_t errlen);   /* 1 on success */
long km_net_read(km_conn *c, void *buf, long len);         /* bytes read; 0 closed; <0 error */
long km_net_write(km_conn *c, const void *buf, long len);  /* bytes written (all of them), <0 error */
const char *km_net_error(km_conn *c);                      /* the last error's text */
void km_net_close(km_conn *c);

#endif
