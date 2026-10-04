# OpenMail: design

OpenMail (KyneMail, then OpenAmigaMail, until 4 October 2026) is a full mail client for AmigaOS 3.2.3, and AROS 68k second.

Dale's requirements, 4 October 2026:
- **A GadTools UI** for a full mail client, "as modern as possible".
- **"Hooks for html linking into our website browser later."**
- **"A plugin architecture for different mail providers / authentication methods."**

## 1. Shape

```
 the window (GadTools, the program's own task)
     |  jobs, as exec messages: connect, list, select, fetch, flag, send
     v
 the worker (a process of its own; it owns bsdsocket.library and AmiSSL)
     |  the engine: oam_imap, oam_smtp, MIME, the store
     v
 OpenSocket, or any bsdsocket.library  ->  the mail servers
```

- **Why two tasks.**
  - The engine's calls block until the server answers.
  - bsdsocket.library and AmiSSL belong to the task that opens them.
  - So all network work runs in one worker process, and the window never waits on a server: it stays live, shows progress, and can cancel.
- **Jobs.** The window sends `struct oam_job` messages and the worker replies to each when it is done. A job carries:
  - its kind;
  - its arguments: account, folder, UID, flags, the message to send;
  - its results: folders, summaries, the message, an error text.
- **Cancelling.** The window signals the worker with Ctrl-C. The engine's reads time out and check for the break.
- **The engine stays portable C.** The host tests (a scripted IMAP server under ASan/UBSan) keep testing it unchanged.

## 2. The window (GadTools)

```
+--------------------------------------------------------------------------+
| [Get mail] [Write] [Reply] [Reply all] [Forward] [Delete] [Search...]     |
+-----------------+--------------------------------------------------------+
| Folders         |   From                 Subject                  Date   |
| > Inbox     12  | * Dale Kirkwood        OpenSocket is MIT        09:30  |
|   Drafts        |   Galen                Phase 0 reviewed         Sat    |
|   Sent          |   ...                                                  |
|   Archive       +--------------------------------------------------------+
|   Trash         | From: Dale Kirkwood <...>          Date: 4 Oct 09:30   |
|                 | Subject: OpenSocket is MIT                             |
|                 | -------------------------------------------------------|
|                 | the message, as text; links are marked [1] [2]         |
|                 |                                                        |
|                 | [View in browser] [Links...] [Attachments (2)...]      |
+-----------------+--------------------------------------------------------+
| imap.gmail.com: 12 new. Getting Inbox...                       [Stop]    |
+--------------------------------------------------------------------------+
```

- **Three panes:** folders, the message list, and the message.
- **Font-sensitive and resizable.** It lays out from the screen's font, and the panes keep their proportions.
- **The message list** is a GadTools listview with a render hook (`GTLV_CallBack`, V39). It draws:
  - columns aligned in any font;
  - unread mail in bold, flagged mail marked;
  - the selected row in the screen's fill colour.

  On V37 the list is plain text in columns.
- **The message** is a read-only listview of wrapped lines. Its header is a few text gadgets above it.
- **Menus** (new look, with Amiga-key shortcuts):
  - Project: Accounts, Settings, About, Quit.
  - Mailbox: Get mail, New folder, Rename, Delete.
  - Message: Write, Reply, Reply all, Forward, Delete, Mark read/unread, Flag, View source, View in browser.
- **Keys:** cursor up and down move in the list, Return opens a message, Del deletes it, and the underlined letters press buttons.
- **Busy, never frozen:** the busy pointer, a status line, and Stop.
- **Writing** happens in a compose window:
  - To, Cc and Subject are string gadgets.
  - The body uses an editor drawn by OpenMail itself (GadTools has no multi-line gadget): typing, cursor keys, word wrap, scrolling, and paste through the clipboard.
  - "Edit in your editor" hands the text to `ENV:EDITOR` and takes it back.
  - Attach, Send and Save draft complete it.
- **Accounts:** a window with a provider cycle (from the provider plugins), the address, a display name and the sign-in method. "Sign in..." runs the chosen auth plugin.

## 3. Plugins: providers and sign-in methods

Two kinds of plugin, so anyone can add a mail service or a sign-in method without changing OpenMail itself.

### Providers: data files

`PROGDIR:Providers/<name>.provider` is plain text, one `key = value` per line:

```
name      = Gmail
domains   = gmail.com googlemail.com
imap      = imap.gmail.com 993 tls
smtp      = smtp.gmail.com 465 tls
auth      = xoauth2-browser password
oauth.authorize = https://accounts.google.com/o/oauth2/v2/auth
oauth.token     = https://oauth2.googleapis.com/token
oauth.scope     = https://mail.google.com/
oauth.client    = <OpenMail's registered client id>
note      = Google wants OAuth; an app password works for accounts with 2-step verification.
```

- **Matching:** the address's domain picks the provider. An address that matches none gets "Other", where the user types the servers.
- **Shipped:** Gmail, Outlook.com and Microsoft 365, Yahoo, iCloud, Fastmail, GMX, and Other.
- **Why data:** a provider is just data, so a new one is a text file anyone can write or share.

### Sign-in methods: Amiga libraries

`PROGDIR:Auth/<name>.auth` is an Amiga shared library (opened with `OpenLibrary` by its path), with this interface (`include/oam_auth.h`):

| Call | What it does |
| --- | --- |
| `AuthInfo()` | Its name, version, and what it needs: a password, a token, the browser |
| `AuthBegin(ctx)` | Starts a sign-in. Password: nothing to do. OAuth device code: asks the provider for a code and returns the code and the address to show the user |
| `AuthPoll(ctx)` | OAuth: is the token there yet (pending, done, refused)? |
| `AuthRefresh(ctx)` | A new access token from the refresh token |
| `AuthResponse(ctx, mech, out)` | The SASL initial response for IMAP and SMTP (PLAIN, XOAUTH2) |

- **What the context brings:**
  - the account and the provider's keys;
  - callbacks into OpenMail: an HTTPS POST (through the worker's TLS, so a plugin needs no network code), the secret store, the log, and the browser hook.
- **Built in, behind the same interface:**
  - `password` (PLAIN and LOGIN);
  - `xoauth2-device` (Microsoft's device-code flow, entirely on the Amiga);
  - `xoauth2-browser` (Google: the sign-in page opens through the browser hook, and the code comes back through AmigaChrome's host or a pasted code);
  - `xoauth2-token` (a token from a file, as KyneMailCheck took it).
- **External `.auth` libraries** load the same way. That lets later methods (Yahoo's OAuth, a corporate SSO) be shipped apart from OpenMail.
- **Secrets:**
  - Refresh tokens and passwords are kept in `ENVARC:OpenMail/Secrets`.
  - They are obscured with a key unique to the machine. It is not strong encryption, and the settings say so: the Amiga has no key store.
  - Passwords are never written to the log or to a trace.

## 4. HTML, links and the browser hook

- **text/plain** parts show as they are, wrapped. Links are found and numbered.
- **text/html** parts get a light rendering for the text view:
  - the text, paragraphs and lists kept;
  - entities decoded;
  - links numbered [1] [2], with their addresses in "Links...";
  - images replaced by their alt text.

  OpenMail fetches nothing from the network to show a message, so there are no tracking pixels.
- **"View in browser"** writes the HTML part and its inline images (`cid:`) to `T:OpenMail/<uid>/` as files. It then hands `index.html` to the browser hook.
- **The browser hook** (`include/oam_browser.h`) opens a URL or a file. It tries these in order:
  1. `ENV:OpenMail/Browser`, a command with `%s` for the address, if the user set one;
  2. AmigaChrome's browser, through its ARexx port. The name is reserved as `AMIGACHROME.BROWSER`, with the commands `OPENURL url` and `OPENFILE path`. That is the hook for "our website browser later": once that browser answers on the port, OpenMail uses it with no change;
  3. `openurl.library` (OpenURL), which most Amiga browsers register with;
  4. none found: OpenMail says so and shows the address so it can be copied.
- **Sign-in pages** (OAuth) go through the same hook.

## 5. Milestones

| | What | Done when |
| --- | --- | --- |
| M1-M2 | IMAP engine; Amiga network (KyneMail, 2 Oct) | Done |
| **M3** | The GadTools main window, read-only: worker, accounts from providers, folders, message list, reading text and HTML, the browser hook | It reads Gmail and Outlook on OS 3.2.3 over OpenSocket |
| M4 | SMTP and the compose window | Sending, replying, forwarding |
| M5 | MIME: multipart, attachments, charsets, quoted-printable and base64 both ways | Real-world mail reads and sends correctly |
| M6 | The local store: a cache per folder, offline reading, flags synced | It opens without the network |
| M7 | Search, filters, an address book; external `.auth` libraries | |

Targets: OS 3.2.3 first, then an AROS build with the same sources (`platform/amiga` plus `#ifdef __AROS__` only where the APIs differ).
