#!/usr/bin/env python3
"""tests/lab/imapd_lab.py: a throwaway IMAP server on 127.0.0.1 for trying
OpenMail and OpenMailCheck on an Amiga (a lab) against a real TLS server,
with Python's own ssl module. A test tool only, never shipped.

    imapd_lab.py PORT USER PASSWORD [--security tls|starttls|plain]
                 [--cert CERT --key KEY] [--messages N] [--big KIB] [--log FILE]

--security tls: TLS from the first byte (IMAPS, as port 993); starttls:
plain until STARTTLS, and LOGINDISABLED before it; plain: no TLS. One
account, an INBOX of N made-up messages (the newest --big KiB long, to move
some data through TLS), and a few other folders, one with a Latin-1 name.
It answers what OpenMail's engine sends: CAPABILITY, STARTTLS,
AUTHENTICATE PLAIN (with or without SASL-IR), LOGIN, LIST, SELECT,
EXAMINE, UID FETCH (summaries and whole messages), UID STORE, NOOP, IDLE
and LOGOUT. Each connection is served in a thread of its own, until killed.
Lab use only: it binds 127.0.0.1, and the account and certificate are
throwaway ones. A lab's Amiga reaches it through openamigatls'
tests/lab/pyhook (OPENTLS_LAB_PORTS).

MIT, Copyright (c) 2026 AmigaChrome Team.
"""
import argparse
import base64
import re
import socket
import ssl
import sys
import threading
import time

FOLDERS = [('(\\HasNoChildren)', '"/"', 'INBOX'),
           ('(\\HasNoChildren \\Sent)', '"/"', '"Sent"'),
           ('(\\HasNoChildren)', '"/"', '"Gr&APw-&AN8-e"'),          # Grüße, modified UTF-7
           ('(\\HasChildren \\Noselect)', '"/"', '"Lab"'),
           ('(\\HasNoChildren)', '"/"', '"Lab/Archive"')]
FETCH_FIELDS = "DATE FROM TO CC REPLY-TO SUBJECT MESSAGE-ID IN-REPLY-TO REFERENCES CONTENT-TYPE"


def make_messages(n, big_kib):
    msgs = []
    for i in range(1, n + 1):
        subject = "=?ISO-8859-1?Q?Gr=FC=DFe_number_%d?=" % i if i % 2 else "Lab message %d (TLS test)" % i
        head = ("Date: Mon, 19 Oct 2026 0%d:%02d:00 +0100\r\n"
                "From: Lab Sender %d <sender%d@example.com>\r\n"
                "To: Lab User <user@example.com>\r\n"
                "Subject: %s\r\n"
                "Message-ID: <lab%d@example.com>\r\n"
                "Content-Type: text/plain; charset=us-ascii\r\n\r\n" % (i % 10, i % 60, i, i, subject, i)).encode()
        if i == n and big_kib:
            line = b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdef\r\n"
            body = line * (big_kib * 1024 // len(line))
        else:
            body = b"Hello from the lab's IMAP server.\r\nMessage %d.\r\n" % i
        msgs.append({"uid": 100 + i, "flags": "\\Seen" if i < n - 1 else "", "head": head, "all": head + body})
    return msgs


class Session(threading.Thread):
    def __init__(self, conn, peer, a, ctx, log):
        super().__init__(daemon=True)
        self.conn, self.peer, self.a, self.ctx, self.log = conn, peer, a, ctx, log
        self.buf = b""
        self.tls = a.security == "tls"
        self.authed = False
        self.selected = None
        self.msgs = make_messages(a.messages, a.big)

    def say(self, data):
        if isinstance(data, str):
            data = data.encode("latin-1")
        self.log("%s S: %s" % (self.peer, data[:100].decode("latin-1").rstrip()))
        self.conn.sendall(data)

    def line(self):
        while b"\r\n" not in self.buf:
            chunk = self.conn.recv(65536)
            if not chunk:
                raise EOFError("the client closed the connection")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\r\n", 1)
        return line.decode("latin-1")

    def caps(self):
        c = "IMAP4rev1 LITERAL+ IDLE UIDPLUS"
        if self.a.security == "starttls" and not self.tls:
            return c + " STARTTLS LOGINDISABLED"
        return c + " SASL-IR AUTH=PLAIN"

    def run(self):
        try:
            if self.tls:
                self.conn = self.ctx.wrap_socket(self.conn, server_side=True)
                self.log("%s TLS %s %s" % (self.peer, self.conn.version(), self.conn.cipher()[0]))
            self.say("* OK [CAPABILITY %s] imapd_lab ready\r\n" % self.caps())
            while True:
                line = self.line()
                self.log("%s C: %s" % (self.peer, re.sub(r"(?i)^(\S+ (LOGIN|AUTHENTICATE PLAIN) ).*", r"\1...", line)))
                tag, _, rest = line.partition(" ")
                verb, _, arg = rest.partition(" ")
                verb = verb.upper()
                if verb == "UID":
                    sub, _, arg = arg.partition(" ")
                    verb = "UID " + sub.upper()
                if not self.handle(tag, verb, arg):
                    break
        except (EOFError, OSError, ssl.SSLError) as e:
            self.log("%s closed: %s" % (self.peer, e))
        finally:
            try:
                self.conn.close()
            except OSError:
                pass

    def handle(self, tag, verb, arg):
        if verb == "CAPABILITY":
            self.say("* CAPABILITY %s\r\n%s OK CAPABILITY done\r\n" % (self.caps(), tag))
        elif verb == "NOOP":
            self.say("%s OK NOOP done\r\n" % tag)
        elif verb == "LOGOUT":
            self.say("* BYE imapd_lab says goodbye\r\n%s OK LOGOUT done\r\n" % tag)
            return False
        elif verb == "STARTTLS" and self.a.security == "starttls" and not self.tls:
            self.say("%s OK Begin TLS negotiation now\r\n" % tag)
            self.conn = self.ctx.wrap_socket(self.conn, server_side=True)
            self.tls, self.buf = True, b""
            self.log("%s TLS %s %s" % (self.peer, self.conn.version(), self.conn.cipher()[0]))
        elif verb in ("AUTHENTICATE", "LOGIN"):
            self.login(tag, verb, arg)
        elif not self.authed:
            self.say("%s BAD sign in first\r\n" % tag)
        elif verb in ("LIST", "LSUB"):
            out = "".join("* %s %s %s %s\r\n" % (verb, f, d, n) for f, d, n in FOLDERS)
            self.say(out + "%s OK %s done\r\n" % (tag, verb))
        elif verb in ("SELECT", "EXAMINE"):
            self.selected = arg.strip('"')
            n = len(self.msgs) if self.selected.upper() == "INBOX" else 0
            self.say("* FLAGS (\\Answered \\Flagged \\Deleted \\Seen \\Draft)\r\n* %d EXISTS\r\n* 0 RECENT\r\n"
                     "* OK [UIDVALIDITY 4242] UIDs valid\r\n* OK [UIDNEXT %d] Predicted next UID\r\n"
                     "%s OK [%s] %s completed\r\n" % (n, 101 + n, tag, "READ-ONLY" if verb == "EXAMINE" else "READ-WRITE",
                                                      verb))
        elif verb == "UID FETCH":
            self.fetch(tag, arg)
        elif verb == "UID STORE":
            m = re.match(r"(\d+) ([+-])FLAGS(?:\.SILENT)? \((.*)\)", arg, re.I)
            if m:
                for msg in self.msgs:
                    if msg["uid"] == int(m.group(1)):
                        have, change = set(msg["flags"].split()), set(m.group(3).split())
                        msg["flags"] = " ".join(sorted(have | change if m.group(2) == "+" else have - change))
            self.say("%s OK STORE done\r\n" % tag)
        elif verb == "IDLE":
            self.say("+ idling\r\n")
            while self.line().upper() != "DONE":
                pass
            self.say("%s OK IDLE done\r\n" % tag)
        else:
            self.say("%s BAD imapd_lab doesn't know %s\r\n" % (tag, verb))
        return True

    def login(self, tag, verb, arg):
        ok = False
        if verb == "LOGIN":
            word = r'("(?:[^"\\]|\\.)*"|\S+)'          # an atom or a quoted string
            m = re.match(word + " " + word + "$", arg)
            if m:
                def unquote(s):
                    return re.sub(r"\\(.)", r"\1", s[1:-1]) if s.startswith('"') else s
                ok = unquote(m.group(1)) == self.a.user and unquote(m.group(2)) == self.a.password
        else:
            mech, _, response = arg.partition(" ")
            if mech.upper() != "PLAIN":
                self.say("%s NO only PLAIN here\r\n" % tag)
                return
            if not response:
                self.say("+ \r\n")
                response = self.line()
            try:
                parts = base64.b64decode(response).split(b"\0")
                ok = len(parts) == 3 and parts[1].decode() == self.a.user and parts[2].decode() == self.a.password
            except (ValueError, UnicodeDecodeError):
                ok = False
        if self.a.security == "starttls" and not self.tls:
            ok = False                      # LOGINDISABLED until STARTTLS
        if ok:
            self.authed = True
            self.say("%s OK [CAPABILITY %s] signed in\r\n" % (tag, self.caps()))
        else:
            self.say("%s NO [AUTHENTICATIONFAILED] wrong user name or password\r\n" % tag)

    def fetch(self, tag, arg):
        m = re.match(r"(\d+)(?::(\d+|\*))? \((.*)\)$", arg, re.I)
        if not m or not self.selected:
            self.say("%s BAD bad UID FETCH\r\n" % tag)
            return
        lo, upto = int(m.group(1)), m.group(2)
        hi = (1 << 31) if upto == "*" else int(upto) if upto else lo
        whole = "BODY.PEEK[]" in m.group(3).upper() or "BODY[]" in m.group(3).upper()
        msgs = self.msgs if self.selected.upper() == "INBOX" else []
        for seq, msg in enumerate(msgs, 1):
            if not lo <= msg["uid"] <= hi:
                continue
            if whole:
                data = msg["all"]
                self.say(b"* %d FETCH (UID %d BODY[] {%d}\r\n" % (seq, msg["uid"], len(data)) + data + b")\r\n")
            else:
                h = msg["head"]
                self.say(("* %d FETCH (UID %d FLAGS (%s) RFC822.SIZE %d INTERNALDATE \"19-Oct-2026 01:%02d:00 +0100\" "
                          "BODY[HEADER.FIELDS (%s)] {%d}\r\n" % (seq, msg["uid"], msg["flags"], len(msg["all"]),
                                                                 seq % 60, FETCH_FIELDS, len(h))).encode() + h + b")\r\n")
        self.say("%s OK FETCH done\r\n" % tag)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", type=int)
    ap.add_argument("user")
    ap.add_argument("password")
    ap.add_argument("--security", choices=("tls", "starttls", "plain"), default="tls")
    ap.add_argument("--cert")
    ap.add_argument("--key")
    ap.add_argument("--messages", type=int, default=12)
    ap.add_argument("--big", type=int, default=256, help="the newest message's size in KiB (0: small)")
    ap.add_argument("--log")
    a = ap.parse_args()
    ctx = None
    if a.security != "plain":
        if not (a.cert and a.key):
            ap.error("--cert and --key are needed for tls and starttls")
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(a.cert, a.key)
    lock = threading.Lock()
    out = open(a.log, "a") if a.log else sys.stdout

    def log(text):
        with lock:
            out.write("%s %s\n" % (time.strftime("%H:%M:%S"), text))
            out.flush()

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", a.port))
    srv.listen(8)
    log("imapd_lab on 127.0.0.1:%d (%s)" % (a.port, a.security))
    while True:
        conn, peer = srv.accept()
        Session(conn, "%s:%d" % peer, a, ctx, log).start()


if __name__ == "__main__":
    sys.exit(main())
