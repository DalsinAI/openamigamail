/* ogt_icons: OpenGadTools' toolbar and list icons, drawn from shapes in the
 * OS 3.2 (GlowIcons) style: soft colours with dark outlines. They scale to
 * any size, so toolbars follow the font. The shapes match the mock-ups
 * (DESIGN.md 2d; openamigamail DESIGN.md 2).
 *
 * MIT, Copyright (c) 2026 Dalsin Limited. */
#ifndef OGT_ICONS_H
#define OGT_ICONS_H

#include "ogt_draw.h"

enum {
    OGT_ICON_NONE = 0,
    OGT_ICON_NEWMAIL, OGT_ICON_REPLY, OGT_ICON_REPLYALL, OGT_ICON_FORWARD,
    OGT_ICON_DELETE, OGT_ICON_ARCHIVE, OGT_ICON_JUNK, OGT_ICON_MOVE,
    OGT_ICON_FLAG, OGT_ICON_UNREAD, OGT_ICON_GETMAIL, OGT_ICON_INBOX,
    OGT_ICON_SENT, OGT_ICON_DRAFT, OGT_ICON_TRASH, OGT_ICON_FOLDER,
    OGT_ICON_ATTACH, OGT_ICON_SEARCH, OGT_ICON_CONTACTS, OGT_ICON_MAIL,
    OGT_ICON_SEND, OGT_ICON_SETTINGS,
    OGT_ICON_COUNT
};

/* Draws an icon in a size x size box at x,y. disabled: drawn faded. */
void ogt_icon_draw(ogt_ctx *c, struct RastPort *rp, int icon, int x, int y, int size, int disabled);

#endif
