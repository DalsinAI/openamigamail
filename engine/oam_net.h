/* oam_net: the one place the engine meets the network. Each platform
 * supplies these (platform/posix for the host's tests, platform/amiga for
 * bsdsocket.library and OpenTLS, or AmiSSL); the engine never includes a socket header.
 * Errors come back as text in err (at most errlen bytes). */
#ifndef OAM_NET_H
#define OAM_NET_H

#include <stddef.h>

typedef struct oam_conn oam_conn;

/* Per task: on the Amiga, bsdsocket.library and the TLS library belong to the task
 * that opens them, so the task that talks to servers calls oam_net_init
 * first and oam_net_cleanup at the end. 1 when the network is there. */
int oam_net_init(char *err, size_t errlen);
void oam_net_cleanup(void);
/* when set, the platform layer reports each step (opening the stack and
 * TLS, resolving, connecting, the handshake) to it: for check tools */
extern void (*oam_net_progress)(const char *step);
/* how long a read waits for the server before giving up (seconds) */
void oam_net_set_timeout(oam_conn *c, int seconds);

/* tls: 1 to start TLS at once (IMAPS 993, SMTPS 465), 0 for a plain
 * connection that may be upgraded with oam_net_starttls */
oam_conn *oam_net_connect(const char *host, int port, int tls, char *err, size_t errlen);
int oam_net_starttls(oam_conn *c, const char *host, char *err, size_t errlen);   /* 1 on success */
long oam_net_read(oam_conn *c, void *buf, long len);         /* bytes read; 0 closed; <0 error */
long oam_net_write(oam_conn *c, const void *buf, long len);  /* bytes written (all of them), <0 error */
const char *oam_net_error(oam_conn *c);                      /* the last error's text */
void oam_net_close(oam_conn *c);

#endif
