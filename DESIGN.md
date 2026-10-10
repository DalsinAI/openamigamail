# OpenMail: design

OpenMail (KyneMail, then OpenAmigaMail, until 4 October 2026) is a full mail client for AmigaOS 3.2.3, and AROS 68k second.

Our requirements, 4 October 2026:
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

## 2. The window: an Outlook-style desk (GadTools)

We, 4 October 2026: "I'd like OpenMail to have a more Outlook feel: icons
for buttons, some features you would expect, a side panel for folders."
And on sign-in: "really we want OpenMail to do this" (OAuth).

**Mock-up:** the "OpenMail Desk" canvas,
https://claude.ai/artifact/MMhp6SK1KAQVfG5jhdt1Uj (private until we share it). It has four boards:
- the desk in the Open theme;
- writing a reply;
- the Open theme, dark;
- the Graphite theme.

The colours are those of OpenGadTools' theme files, and every text colour
on it meets 4.5:1.

```
+--------------------------------------------------------------------------------+
| [New mail] | [Reply][Reply all][Forward] | [Delete][Archive][Junk][Move to] |   |
|            |                             | [Flag][Unread] | [Get mail]  [Search mail]|
+--------------+---------------------------+-------------------------------------+
| Favourites   | Inbox  Home  [All][Unread][Flagged]                             |
|  Inbox    12 | Today                     | Re: OpenFiles tabs                  |
|  Flagged   3 | | Jo Taylor    09:42 flag | (SR) Sam Rivers <sam@example.com>   |
|  Drafts    1 | |  Saturday meet: who...  |      To: Kim Example   Today 07:58  |
|  Sent        | |  I can bring the pro... |      [Reply] [Reply all] [Forward]  |
| Home         |   Sam Rivers   07:58 clip | [tabs-test.iff 38 KB  Open  Save...]|
|  Inbox    12 |   Re: OpenFiles tabs     | ----------------------------------- |
|  ...         | Yesterday                 | Hi Kim, ...                         |
| Club (Gmail) |   ...                     |                                     |
| + New folder |                           |                                     |
| [Mail][Contacts]                         |                                     |
+--------------+---------------------------+-------------------------------------+
| Home: up to date | Club: getting mail [=====     ] Stop             12 unread  |
+--------------------------------------------------------------------------------+
```

### The toolbar

- **Icons,** following the common rules of OpenGadTools section 0. A
  setting chooses icons only (the default: every Open app starts with
  icons, not icons and text, the Team's rule of 10 October 2026), icons
  and text, or text only, and help bubbles name every icon.
- **The groups:**
  - New mail;
  - Reply, Reply all, Forward;
  - Delete, Archive, Junk, Move to;
  - Flag, Unread;
  - Get mail.
  - The search field sits on the right.
- **The icons** are 24 x 24 in the OS 3.2 (GlowIcons) style, drawn for
  OpenMail.
- **How they're built:** until OpenGadTools' image buttons exist, each one is
  Intuition's `frbuttonclass` with our image, in the GadTools window.

### The folder side panel

- **Favourites first:** Inbox, Flagged, Drafts and Sent across every
  account. Its Inbox is the unified inbox.
- **Then each account** with its folders as a tree:
  - indented sub-folders;
  - the unread count beside each folder (the Inbox's bold, in the accent
    colour);
  - each section folds away.
- **The account name** shows its provider (Home, Outlook.com; Club, Gmail).
- **Drag messages onto a folder** to move them (with Shift, to copy). "+ New
  folder" is at the foot.
- **A Mail / Contacts switch** sits at the bottom. Contacts is the address
  book of M7.
- **The panel's width** is adjustable, and it can be hidden.

### The message list

- **The header:**
  - the folder's name and its account;
  - All, Unread and Flagged filters;
  - sorting (by date, the default; sender; subject; size).
- **Each message takes three lines:**
  - the sender, with marks for an attachment and a flag, and the time;
  - the subject;
  - a one-line preview.
- **Messages are grouped by date:** Today, Yesterday, Earlier this week, Last
  week, Older.
- **Unread mail** has a bar in the accent colour, a bold sender, and the
  subject bold in the accent colour.
  - **The selected message** has a soft accent fill.
- **How it's built:** a GadTools listview with `GTLV_ItemHeight` and a render
  hook (`GTLV_CallBack`, V39), until OpenGadTools' list class takes over.

### The reading pane

- **Where:** on the right by default; below the list, or off, is a setting.
- **What it shows:**
  - the subject;
  - the sender's initials in a circle, their name and address, the
    recipients and the date;
  - Reply, Reply all and Forward beside them;
  - **attachments as chips:** a picture, the name, the size, **Open**
    (OpenView, through datatypes) and **Save...**.
- **Pictures from the internet** stay blocked until "Show pictures" is
  pressed, for privacy. Links open in OpenBrowser.

### The status bar

Each account's state, a progress gauge with Stop, and the unread count. It
is busy, never frozen, as before.

### Writing

**The compose window has the same look.**
- **The toolbar:** Send (in the accent colour), Attach, Signature, Priority,
  Save draft and Discard.
- **The fields:**
  - From is a cycle of the accounts;
  - To shows recipients as chips, completed from Contacts;
  - Cc and Bcc on request;
  - Subject.
- **The editor** is OpenMail's own, as designed; GadTools has no multi-line
  gadget.
- **Under it:** "Plain text. Draft saved 08:12" and **Edit in your editor**
  (`ENV:EDITOR`).
- **Files dropped on the window** from Workbench become attachments (it's an
  AppWindow).

### The features you'd expect

| Feature | When |
| --- | --- |
| The desk above: icon toolbar, folder side panel, three-line list grouped by date, reading pane | M3b |
| Unified inbox (Favourites) and unread counts | M3b |
| Quick filter in the folder (sender and subject) | M3b |
| Search on the server (IMAP SEARCH) | M7 |
| Flags and read state, synced both ways | M6 |
| Move, copy and delete by dragging | M3b (IMAP MOVE and COPY) |
| Compose, reply, forward; drafts saved as you type; signatures per account | M4 |
| Attachments both ways, opened through OpenView | M5 |
| Offline reading from the local store | M6 |
| Contacts with completion; rules (filters); junk handling | M7 |
| New-mail notice and checking every N minutes | M3b |
| Microsoft sign-in (OAuth): "Sign in with Microsoft..." in Accounts; the engine is ready (device code), and with OpenBrowser the sign-in page opens inside it | When we give the app registration's client ID |
| Conversations (messages threaded by subject and references) | After M7 |

**Keys** follow the menus' Amiga-key shortcuts:
- Right Amiga+N new mail, Right Amiga+R reply, Shift+Right Amiga+R reply
  all;
- Right Amiga+G get mail;
- Del deletes;
- cursor keys move in the list, and Return opens a message.

**Themes:** OpenMail follows OpenGadTools' theme. Open, the OS 4-style theme,
is the closest to Outlook's feel. Until the look patch ships, it draws in
the Classic look with the same layout.

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
| **M3b** | The Outlook-style desk (section 2): icon toolbar, folder side panel with Favourites and unread counts, the three-line list grouped by date, the reading pane with attachment chips, quick filter, moving by drag and drop, new-mail notice | The desk of the mock-up, working on OS 3.2.3 against Gmail and Outlook.com |
| M4 | SMTP and the compose window, in the same look | Sending, replying, forwarding |
| M5 | MIME: multipart, attachments, charsets, quoted-printable and base64 both ways | Real-world mail reads and sends correctly |
| M6 | The local store: a cache per folder, offline reading, flags synced | It opens without the network |
| M7 | Search, filters, an address book; external `.auth` libraries | |

Targets: OS 3.2.3 first, then an AROS build with the same sources (`platform/amiga` plus `#ifdef __AROS__` only where the APIs differ).
