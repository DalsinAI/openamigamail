/* oam_oauth: signing in with OAuth 2.0's device code flow (RFC 8628), as
 * Microsoft wants for Outlook.com and Hotmail: OpenMail shows a short code,
 * the user enters it on the provider's page in any browser and approves,
 * and OpenMail gets tokens without ever seeing the password. Refresh tokens
 * keep it signed in.
 *
 * OpenMail. MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OAM_OAUTH_H
#define OAM_OAUTH_H

#include <stddef.h>

typedef struct oam_oauth_client {
    char device_url[256];     /* oauth.device */
    char token_url[256];      /* oauth.token */
    char client[96];          /* oauth.client */
    char scope[256];          /* oauth.scope */
} oam_oauth_client;

typedef struct oam_device_code {
    char device_code[1024];
    char user_code[32];       /* what the user types: "ABCD-EFGH" */
    char verify_url[256];     /* where: "https://microsoft.com/devicelogin" */
    int interval;             /* seconds between polls */
    int expires_in;           /* seconds the code lives */
} oam_device_code;

typedef struct oam_tokens {
    char access[4096];
    char refresh[4096];
    int expires_in;
} oam_tokens;

enum { OAM_OAUTH_ERROR = 0, OAM_OAUTH_DONE = 1, OAM_OAUTH_PENDING = 2 };

/* Asks for a device code. 1, or 0 with the reason in err. */
int oam_oauth_device_start(const oam_oauth_client *c, oam_device_code *d, char *err, size_t errlen);
/* One poll: DONE with tokens, PENDING (not approved yet; slow_down raises
 * d->interval), or ERROR with the reason in err (declined, expired...). */
int oam_oauth_device_poll(const oam_oauth_client *c, oam_device_code *d, oam_tokens *t, char *err, size_t errlen);
/* A new access token (and maybe a new refresh token) from a refresh token. */
int oam_oauth_refresh(const oam_oauth_client *c, const char *refresh, oam_tokens *t, char *err, size_t errlen);

/* Splits "https://host/path" into host and path. 1 when it is https. */
int oam_oauth_split_url(const char *url, char *host, size_t hostlen, char *path, size_t pathlen);

#endif
