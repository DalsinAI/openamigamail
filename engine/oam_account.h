/* oam_account: one mail account, as the settings file keeps it
 * (ENVARC:OpenMail/Account on the Amiga), one "key = value" per line:
 *
 *     name     = Kim Example
 *     address  = kim@example.com
 *     provider = Gmail
 *     imap     = imap.gmail.com 993 tls
 *     smtp     = smtp.gmail.com 465 tls
 *     auth     = password
 *     user     = kim@example.com
 *     secret   = <obscured>
 *
 * secret is the password (auth password) or the token file's path
 * (xoauth2-token). It is obscured, not encrypted: the Amiga has no key
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
    char secret[256];       /* in the clear in memory, obscured in the file */
} oam_account;

int oam_account_parse(const char *text, oam_account *a, char *err, size_t errlen);
int oam_account_format(const oam_account *a, oam_buf *out);

/* An account from a provider: its servers and its first sign-in method. */
void oam_account_from_provider(oam_account *a, const oam_provider *p);

#endif
