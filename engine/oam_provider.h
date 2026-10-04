/* oam_provider: mail providers as plugins made of data.
 *
 * A provider is a text file, PROGDIR:Providers/<name>.provider, one
 * "key = value" per line; '#' starts a comment:
 *
 *     name    = Gmail
 *     domains = gmail.com googlemail.com
 *     imap    = imap.gmail.com 993 tls
 *     smtp    = smtp.gmail.com 465 tls
 *     auth    = xoauth2-browser password
 *     oauth.token = https://oauth2.googleapis.com/token
 *
 * imap and smtp are "host port security", security being tls, starttls or
 * plain. auth lists sign-in methods, preferred first. Any other key (oauth.*,
 * note, ...) is kept for the sign-in method or the window to read. A mail
 * address finds its provider by its domain. */
#ifndef OAM_PROVIDER_H
#define OAM_PROVIDER_H

#include <stddef.h>

#define OAM_PROVIDER_AUTHS  8
#define OAM_PROVIDER_EXTRAS 16

typedef struct oam_server {
    char host[128];
    int port;
    int security;               /* OAM_IMAP_PLAIN, OAM_IMAP_TLS, OAM_IMAP_STARTTLS */
} oam_server;

typedef struct oam_provider {
    char name[48];
    char domains[256];          /* separated by spaces */
    oam_server imap, smtp;      /* port 0: not given */
    char auth[OAM_PROVIDER_AUTHS][24];
    int nauth;
    struct { char key[32]; char value[256]; } extra[OAM_PROVIDER_EXTRAS];
    int nextra;
} oam_provider;

/* Reads a provider file's text. 1, or 0 with the reason (and its line) in err. */
int oam_provider_parse(const char *text, oam_provider *p, char *err, size_t errlen);

/* Another key's value (oauth.token, note, ...), or NULL. */
const char *oam_provider_get(const oam_provider *p, const char *key);

/* 1 when the address's domain is one of the provider's (or a subdomain of one). */
int oam_provider_matches(const oam_provider *p, const char *address);

/* 1 when the provider offers that sign-in method. */
int oam_provider_has_auth(const oam_provider *p, const char *method);

#endif
