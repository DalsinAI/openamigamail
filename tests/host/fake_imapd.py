#!/usr/bin/env python3
"""A scripted IMAP server for KyneMail's engine tests. It accepts one client,
answers like a real IMAP4rev1 server for the commands the engine sends,
checks each of them, and exits 0 if the session went as the scenario says
(1 otherwise, with the reason on stderr).

    fake_imapd.py SCENARIO PORTFILE [CERT KEY]

It listens on 127.0.0.1 on a free port and writes the port to PORTFILE.
Scenarios:
  plain       plain TCP, AUTH=PLAIN with SASL-IR, the whole session
  login       plain TCP, no AUTH=PLAIN: LOGIN with a quoted password
  xoauth2     plain TCP, XOAUTH2 without SASL-IR (the continuation path)
  xoauth2bad  XOAUTH2 refused: the error challenge, then NO
  tls         implicit TLS (CERT and KEY)
  starttls    STARTTLS, then the session
"""
import base64, socket, ssl, sys

USER, PASSWORD, TOKEN = "dale@example.com", 'pa"ss\\word', "ya29.test-token"

HEADERS = [
    (b"Date: Thu, 02 Oct 2026 21:00:00 +0100\r\nFrom: =?UTF-8?Q?Galen_=E2=9C=A8?= <galen@example.com>\r\n"
     b"Subject: =?ISO-8859-1?Q?Gr=FC=DFe_aus_Amiga?=\r\nMessage-ID: <1@example.com>\r\n\r\n"),
    (b"Date: Thu, 02 Oct 2026 21:05:00 +0100\r\nFrom: Thufir <thufir@example.com>\r\n"
     b"Subject: (parens) and \"quotes\"\r\nMessage-ID: <2@example.com>\r\n\r\n"),
]
BODY = (b"From: Thufir <thufir@example.com>\r\nSubject: hello\r\n\r\nLine one\r\nLine two with {braces} and ) a paren\r\n")


class Fail(Exception):
    pass


class Session:
    def __init__(self, conn, scenario, cert, key):
        self.conn, self.scenario, self.cert, self.key = conn, scenario, cert, key
        self.buf = b""
        self.tls = scenario == "tls"

    def send(self, data):
        self.conn.sendall(data if isinstance(data, bytes) else data.encode())

    def line(self):
        while b"\r\n" not in self.buf:
            chunk = self.conn.recv(4096)
            if not chunk:
                raise Fail("the client closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode("latin-1")

    def expect(self, want_prefix):
        line = self.line()
        tag, _, rest = line.partition(" ")
        if not rest.upper().startswith(want_prefix.upper()):
            raise Fail(f"expected {want_prefix!r}, got {line!r}")
        return tag, rest

    def caps(self):
        c = "IMAP4rev1 LITERAL+ IDLE"
        if self.scenario == "plain":
            c += " SASL-IR AUTH=PLAIN"
        elif self.scenario in ("xoauth2", "xoauth2bad"):
            c += " AUTH=XOAUTH2 AUTH=PLAIN"
        elif self.scenario == "starttls" and not self.tls:
            c += " STARTTLS LOGINDISABLED"
        elif self.scenario in ("tls", "starttls"):
            c += " SASL-IR AUTH=PLAIN AUTH=XOAUTH2"
        return c

    def run(self):
        self.send(f"* OK [CAPABILITY {self.caps()}] fake_imapd ready\r\n")
        if self.scenario == "starttls":
            tag, _ = self.expect("STARTTLS")
            self.send(f"{tag} OK Begin TLS negotiation now\r\n")
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(self.cert, self.key)
            self.conn = ctx.wrap_socket(self.conn, server_side=True)
            self.tls = True
            tag, _ = self.expect("CAPABILITY")
            self.send(f"* CAPABILITY {self.caps()}\r\n{tag} OK done\r\n")
        self.login()
        if self.scenario == "xoauth2bad":
            return
        # capabilities again after login
        tag, _ = self.expect("CAPABILITY")
        self.send(f"* CAPABILITY {self.caps()} MOVE\r\n{tag} OK done\r\n")
        self.session()

    def login(self):
        sc = self.scenario
        if sc in ("plain", "tls", "starttls"):
            tag, rest = self.expect("AUTHENTICATE PLAIN ")
            raw = base64.b64decode(rest.split(" ", 2)[2])
            if raw != b"\0" + USER.encode() + b"\0" + PASSWORD.encode():
                raise Fail(f"PLAIN carried {raw!r}")
            self.send(f"{tag} OK [CAPABILITY {self.caps()}] logged in\r\n")
        elif sc == "login":
            tag, rest = self.expect("LOGIN ")
            want = 'LOGIN "dale@example.com" "pa\\"ss\\\\word"'
            if rest != want:
                raise Fail(f"LOGIN was {rest!r}, wanted {want!r}")
            self.send(f"{tag} OK logged in\r\n")
        elif sc in ("xoauth2", "xoauth2bad"):
            tag, rest = self.expect("AUTHENTICATE XOAUTH2")
            if rest.strip().upper() != "AUTHENTICATE XOAUTH2":
                raise Fail("the initial response came without SASL-IR")
            self.send("+ \r\n")
            resp = base64.b64decode(self.line())
            want = f"user={USER}\x01auth=Bearer {TOKEN}\x01\x01".encode()
            if sc == "xoauth2":
                if resp != want:
                    raise Fail(f"XOAUTH2 carried {resp!r}")
                self.send(f"{tag} OK logged in\r\n")
            else:
                err = base64.b64encode(b'{"status":"401","schemes":"Bearer","scope":"https://mail.google.com/"}').decode()
                self.send(f"+ {err}\r\n")
                if self.line() != "":
                    raise Fail("the client did not answer the error challenge with an empty line")
                self.send(f"{tag} NO [AUTHENTICATIONFAILED] Invalid credentials (Failure)\r\n")

    def session(self):
        tag, rest = self.expect('LIST "" "*"')
        self.send('* LIST (\\HasNoChildren) "/" INBOX\r\n'
                  '* LIST (\\HasChildren \\Noselect) "/" "[Gmail]"\r\n'
                  '* LIST (\\HasNoChildren \\Sent) "/" "[Gmail]/Sent Mail"\r\n'
                  '* LIST (\\HasNoChildren) "/" "Gr&APw-&AN8-e"\r\n'
                  '* LIST (\\HasNoChildren) NIL {14}\r\nBraces {in} it\r\n'
                  f"{tag} OK LIST done\r\n")
        tag, rest = self.expect('SELECT "INBOX"')
        self.send("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n* 2 EXISTS\r\n* 0 RECENT\r\n"
                  "* OK [UIDVALIDITY 1234] UIDs valid\r\n* OK [UIDNEXT 43] Predicted next UID\r\n"
                  f"{tag} OK [READ-WRITE] SELECT completed\r\n")
        tag, rest = self.expect("UID FETCH 1:* (UID FLAGS RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER.FIELDS")
        out = ""
        for i, (uid, flags, h) in enumerate([(41, "\\Seen", HEADERS[0]), (42, "", HEADERS[1])], 1):
            out += (f'* {i} FETCH (UID {uid} FLAGS ({flags}) RFC822.SIZE {1000 + i} INTERNALDATE "02-Oct-2026 21:0{i}:00 +0100" '
                    f"BODY[HEADER.FIELDS (DATE FROM TO CC REPLY-TO SUBJECT MESSAGE-ID IN-REPLY-TO REFERENCES CONTENT-TYPE)] {{{len(h)}}}\r\n")
            self.send(out.encode() + h + b")\r\n")
            out = ""
        self.send("* 3 EXISTS\r\n")          # a new message arrives during the fetch
        self.send(f"{tag} OK FETCH done\r\n")
        tag, rest = self.expect("UID FETCH 42 (UID BODY.PEEK[])")
        self.send(f"* 2 FETCH (UID 42 BODY[] {{{len(BODY)}}}\r\n".encode() + BODY + f")\r\n{tag} OK done\r\n".encode())
        tag, rest = self.expect("UID STORE 42 +FLAGS.SILENT (\\Seen \\Flagged)")
        self.send(f"{tag} OK STORE done\r\n")
        tag, rest = self.expect("LOGOUT")
        self.send(f"* BYE fake_imapd says goodbye\r\n{tag} OK LOGOUT done\r\n")


def main():
    scenario, portfile = sys.argv[1], sys.argv[2]
    cert, key = (sys.argv[3], sys.argv[4]) if len(sys.argv) > 4 else (None, None)
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    srv.settimeout(20)
    with open(portfile, "w") as f:
        f.write(str(srv.getsockname()[1]))
    conn, _ = srv.accept()
    conn.settimeout(20)
    if scenario == "tls":
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cert, key)
        conn = ctx.wrap_socket(conn, server_side=True)
    try:
        Session(conn, scenario, cert, key).run()
    except (Fail, OSError, ssl.SSLError) as e:
        print(f"fake_imapd {scenario}: {e}", file=sys.stderr)
        sys.exit(1)
    conn.close()


if __name__ == "__main__":
    main()
