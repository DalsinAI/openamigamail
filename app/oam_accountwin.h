/* OpenMail's account window. MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OAM_ACCOUNTWIN_H
#define OAM_ACCOUNTWIN_H

#include <intuition/screens.h>
#include <graphics/text.h>
#include "oam_account.h"

/* Opens the account window on scr; 1 when the user saved (a is filled and
 * ENV:/ENVARC:OpenMail/Account are written), 0 when cancelled. a's current
 * values fill the window first. */
int oam_account_window(struct Screen *scr, APTR vi, struct TextAttr *ta, oam_account *a);

#endif
