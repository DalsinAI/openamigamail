/* acm_text: headers for people. Raw RFC 5322 header blocks in; UTF-8 out,
 * and UTF-8 down to ISO-8859-1 for the Amiga's fonts.
 *   acm_hdr_get      a field's value, unfolded, still encoded
 *   acm_hdr_decode   an unstructured value (Subject) with RFC 2047 encoded
 *                    words decoded, as UTF-8
 *   acm_hdr_address  the first mailbox in From/To: display name and address
 *   acm_hdr_date     an RFC 5322 date as seconds since 1970, UTC
 * Results are malloc'd; the caller frees them. */
#ifndef ACM_TEXT_H
#define ACM_TEXT_H

#include "acm_buf.h"

char *acm_hdr_get(const char *header, size_t len, const char *name);
char *acm_hdr_decode(const char *value);
int acm_hdr_address(const char *value, char **name, char **address);
/* 1 and *when (UTC) and *zone (minutes east of UTC) when the date reads */
int acm_hdr_date(const char *value, long long *when, int *zone);

/* bytes in a MIME charset to UTF-8, appended. Known: UTF-8, US-ASCII,
 * ISO-8859-1, ISO-8859-2, ISO-8859-15, Windows-1250, Windows-1252. Others
 * are read as UTF-8 when they are valid UTF-8, else as ISO-8859-1. */
int acm_charset_to_utf8(acm_buf *out, const char *charset, const char *s, size_t len);
int acm_utf8_valid(const char *s, size_t len);
/* UTF-8 to ISO-8859-1, the Amiga's default: typographic quotes, dashes,
 * the euro sign and the like become their nearest Latin-1; anything else
 * becomes '?' */
char *acm_utf8_to_latin1(const char *utf8);

#endif
