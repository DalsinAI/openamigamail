/* oam_account: one mail account, as the settings file keeps it
 * (ENVARC:OpenMail/Account on the Amiga), one "key = value" per line:
 *
 *     name     = Dale Kirkwood
 *     address  = dale@example.com
 *     provider = Gmail
 *     imap     = imap.gmail.com 993 tls
 *     smtp     = smtp.gmail.com 465 tls
 *     auth     = password
 *     user     = dale@example.com
 *     secret   = <obscured>
 *
 * secret is the password (auth password), the token file's path
 * (xoauth2-token) or the refresh token (xoauth2-device, with oauth_token,
 * oauth_client and oauth_scope). It is obscured, not encrypted: the Amiga has no key
 * store, and the settings window says so. */
#ifndef OAM_ACCOUNT_H
#define OAM_ACCOUNT_H

#include <stddef.h>
#include "oam_buf.h"
#include "oam_provider.h"

typedef struct oam_account {
    char name[64];
    char address[128];
    char provider[48];
    oam_server imap, smtp;
    char auth[24];
    char user[128];         /* the login name; the address when empty */
    char secret[4096];      /* the password, or (xoauth2-device) the refresh token; clear in memory, obscured in the file */
    char oauth_token[256];  /* xoauth2-device: the token address, client id and scope, from the provider */
    char oauth_client[96];
    char oauth_scope[256];
} oam_account;

int oam_account_parse(const char *text, oam_account *a, char *err, size_t errlen);
int oam_account_format(const oam_account *a, oam_buf *out);

/* An account from a provider: its servers and its first sign-in method. */
void oam_account_from_provider(oam_account *a, const oam_provider *p);

#endif
