# ACMail

An email client for AmigaOS 3.2.3 and AROS 68k: IMAP and SMTP over TLS, OAuth 2 sign-in (XOAUTH2) for Gmail and Outlook.com, and a ReAction interface. ACMail is a working name.

It is written from the standards (RFC 3501, 5321, 5322, 2045-2049, 2047, 4616, 4959) and the providers' OAuth notes. SimpleMail served as a checklist of what a mail client on the Amiga does, but none of its code is used.

## Layout

| Folder | What |
| --- | --- |
| `engine/` | Portable C: the protocols, MIME and the mail store. It builds unchanged for AmigaOS 3.x, AROS and the host |
| `platform/posix/` | The network for the host's tests: POSIX sockets and OpenSSL |
| `platform/amiga/` | The network on the Amiga: bsdsocket.library and AmiSSL 5; a bigger stack for main |
| `tools/` | ACMailCheck: signs in, lists folders and the newest subjects. The engine's first program on the Amiga |
| `tests/host/` | The engine's tests on the host, with a scripted IMAP server |

The engine meets the network only through `engine/acm_net.h`, so the same protocol code runs on both.

## Where it stands

| Step | State |
| --- | --- |
| 1. IMAP over TLS or STARTTLS: PLAIN, LOGIN and XOAUTH2 sign-in; folders; select; summaries; whole messages; flags | **Done, tested on the host** |
| 2. Headers for people: fields, RFC 2047 encoded words, names and addresses, dates; seven charsets to UTF-8, and UTF-8 to the Amiga's Latin-1 | **Done, tested on the host** |
| 3. The Amiga's network: bsdsocket.library and AmiSSL 5 (`OpenAmiSSLTags`), read timeouts with WaitSelect | **Built**: ACMailCheck runs on OS 3.2.3 and reports a missing stack; a session waits for a TCP/IP stack in the guest (ACNet) |
| 4. The ReAction main window: folders, message list, plain-text reading | |
| 5. SMTP and the compose window | |
| 6. MIME: multipart, quoted-printable, base64, attachments | |
| 7. OAuth tokens: from AmigaChrome's host; device-code sign-in for providers that allow it (Microsoft); refresh on the Amiga | |
| 8. Local cache, search, filters, address book | |

## Testing on the host

    python3 tests/host/run_tests.py

This builds the engine with AddressSanitizer and UBSan, makes a throwaway CA and a certificate for localhost, and runs:
- the unit tests: base64, SASL, modified UTF-7;
- six sessions against `fake_imapd.py`:
  - **plain:** AUTH=PLAIN with SASL-IR;
  - **login:** LOGIN, with a password holding `"` and `\`;
  - **xoauth2:** the continuation path;
  - **xoauth2bad:** a refused token, with the server's reason passed on;
  - **tls** and **starttls.**

The fake server checks each command it receives, and a session passes only when both sides agree.

## Building for the Amiga

    ./build-os3.sh /path/to/AmiSSL/Developer/include

It builds `build/os3/ACMailCheck` with the os32 stove (bebbo's m68k-amigaos-gcc 6.5, NDK 3.2), soft float. To run it, the Amiga needs a TCP/IP stack (bsdsocket.library) and AmiSSL 5 installed:

    ACMailCheck imap.gmail.com 993 tls you@gmail.com S:token XOAUTH2

**Stacks:** a Shell gives a command 4 KB of stack, and the engine needs more: a TLS handshake alone runs deep. ACMail's programs swap to a 64 KB stack at start (`acm_run_with_stack`). An `acm_imap` is about 4.3 KB, so keep it on the heap.
