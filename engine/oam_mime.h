/* oam_mime: a message's parts (RFC 2045-2049): walking multiparts, undoing
 * quoted-printable and base64, charsets to UTF-8, and what the message view
 * shows: the text part, or the HTML part as text, with its links and
 * attachments counted. */
#ifndef OAM_MIME_H
#define OAM_MIME_H

#include <stddef.h>
#include "oam_buf.h"

typedef struct oam_part {
    char type[64];          /* lower case; "text/plain" when not given */
    char charset[40];       /* "" when not given */
    char encoding[24];      /* lower case: 7bit, 8bit, binary, quoted-printable, base64 */
    int attachment;         /* Content-Disposition: attachment, or a named part that is not text */
    char filename[128];     /* UTF-8; RFC 2231 and RFC 2047 names decoded */
    char cid[128];          /* Content-ID, without its < > */
    const char *body;       /* still encoded, inside the message */
    size_t len;
} oam_part;

typedef void (*oam_part_fn)(const oam_part *p, void *user);

/* Calls fn for every part that is not itself a multipart, in order. 1, or 0
 * when the message has no header/body boundary at all. */
int oam_mime_walk(const char *msg, size_t len, oam_part_fn fn, void *user);

/* The part's bytes with its transfer encoding undone. */
int oam_mime_decode(const oam_part *p, oam_buf *out);
/* The part as UTF-8 text (decoded, its charset converted, CR LF made LF). */
int oam_mime_text(const oam_part *p, oam_buf *utf8);

typedef struct oam_view {
    oam_buf text;           /* UTF-8, LF line ends */
    oam_buf links;          /* one address per line, in the order [1] [2] ... */
    oam_buf html;           /* the HTML part, UTF-8, when there is one (for the browser) */
    int has_html;
    int attachments;
} oam_view;

void oam_view_init(oam_view *v);
void oam_view_free(oam_view *v);
/* What the window shows: the text/plain part if there is one, else the
 * HTML part as text (oam_html). */
int oam_mime_view(const char *msg, size_t len, oam_view *v);

#endif
