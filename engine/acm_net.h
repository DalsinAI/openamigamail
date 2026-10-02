/* acm_net: the one place the engine meets the network. Each platform
 * supplies these (platform/posix for the host's tests, platform/amiga for
 * bsdsocket.library and AmiSSL); the engine never includes a socket header.
 * Errors come back as text in err (at most errlen bytes). */
#ifndef ACM_NET_H
#define ACM_NET_H

#include <stddef.h>

typedef struct acm_conn acm_conn;

/* Per task: on the Amiga, bsdsocket.library and AmiSSL belong to the task
 * that opens them, so the task that talks to servers calls acm_net_init
 * first and acm_net_cleanup at the end. 1 when the network is there. */
int acm_net_init(char *err, size_t errlen);
void acm_net_cleanup(void);
/* how long a read waits for the server before giving up (seconds) */
void acm_net_set_timeout(acm_conn *c, int seconds);

/* tls: 1 to start TLS at once (IMAPS 993, SMTPS 465), 0 for a plain
 * connection that may be upgraded with acm_net_starttls */
acm_conn *acm_net_connect(const char *host, int port, int tls, char *err, size_t errlen);
int acm_net_starttls(acm_conn *c, const char *host, char *err, size_t errlen);   /* 1 on success */
long acm_net_read(acm_conn *c, void *buf, long len);         /* bytes read; 0 closed; <0 error */
long acm_net_write(acm_conn *c, const void *buf, long len);  /* bytes written (all of them), <0 error */
const char *acm_net_error(acm_conn *c);                      /* the last error's text */
void acm_net_close(acm_conn *c);

#endif
