/* oam_browser: opens a web address or a file in a browser (DESIGN.md 4).
 *
 * Tried in this order:
 *   1. ENV:OpenAmigaMail/Browser: a command, %s standing for the address;
 *   2. AmigaChrome's browser, on ARexx port AMIGACHROME.BROWSER, with
 *      OPENURL address or OPENFILE path (reserved for that browser: once it
 *      answers there, OpenAmigaMail uses it with no change);
 *   3. openurl.library (OpenURL), which most Amiga browsers register with.
 * Sign-in pages (OAuth) and "View in browser" both come through here. */
#ifndef OAM_BROWSER_H
#define OAM_BROWSER_H

#include <stddef.h>

#define OAM_BROWSER_PORT "AMIGACHROME.BROWSER"

/* what: a URL ("https://...", "mailto:...") or an AmigaDOS path to a file.
 * 1 when a browser took it, with how it was opened in how; 0 with the
 * reason in how. */
int oam_browser_open(const char *what, char *how, size_t howlen);

#endif
