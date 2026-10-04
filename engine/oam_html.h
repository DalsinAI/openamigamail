/* oam_html: an HTML part as text, for the message view.
 *
 * Paragraphs, line breaks, headings and list items keep their shape; links
 * become "text [n]" with the n-th address added to links (one per line);
 * images become their alt text; head, script and style are dropped;
 * <pre> keeps its spacing; entities are decoded. Nothing is fetched: the
 * light view never reaches the network. The text is UTF-8, as the HTML
 * was (the caller converts the part's charset first). */
#ifndef OAM_HTML_H
#define OAM_HTML_H

#include <stddef.h>
#include "oam_buf.h"

/* 1, or 0 when memory ran out (what was made is kept). */
int oam_html_to_text(const char *html, size_t len, oam_buf *text, oam_buf *links);

#endif
