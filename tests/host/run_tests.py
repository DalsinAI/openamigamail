#!/usr/bin/env python3
"""OpenAmigaMail engine tests on the host: builds test_engine (the engine with
platform/posix and OpenSSL), makes a throwaway CA and a certificate for
localhost, then runs the unit tests and each fake_imapd scenario.

    python3 run_tests.py        exit 0 when everything passes
"""
import os, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
KM = os.path.normpath(os.path.join(HERE, "..", ".."))
SCENARIOS = ["plain", "login", "xoauth2", "xoauth2bad", "tls", "starttls"]


def build(out):
    engine = os.path.join(KM, "engine")
    srcs = [os.path.join(engine, f) for f in sorted(os.listdir(engine)) if f.endswith(".c")]
    srcs += [os.path.join(KM, "platform", "posix", "oam_net_posix.c"), os.path.join(HERE, "test_engine.c")]
    cmd = ["gcc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-g", "-O1", "-fsanitize=address,undefined",
           "-I" + engine, *srcs, "-lssl", "-lcrypto", "-o", out]
    subprocess.run(cmd, check=True)


def certificates(d):
    ca_key, ca, key, csr, cert = (os.path.join(d, n) for n in ("ca.key", "ca.pem", "server.key", "server.csr", "server.pem"))
    run = lambda *a: subprocess.run(a, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1", "-subj", "/CN=OpenAmigaMail test CA",
        "-keyout", ca_key, "-out", ca)
    run("openssl", "req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=localhost", "-keyout", key, "-out", csr)
    ext = os.path.join(d, "ext.cnf")
    with open(ext, "w") as f:
        f.write("subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n")
    run("openssl", "x509", "-req", "-in", csr, "-CA", ca, "-CAkey", ca_key, "-CAcreateserial", "-days", "1",
        "-extfile", ext, "-out", cert)
    return ca, cert, key


def main():
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "test_engine")
        build(exe)
        ca, cert, key = certificates(d)
        env = dict(os.environ, OAM_CA_FILE=ca)
        failed = subprocess.run([exe, "unit"]).returncode != 0
        failed = subprocess.run([exe, "providers", os.path.join(KM, "Providers")]).returncode != 0 or failed
        for sc in SCENARIOS:
            portfile = os.path.join(d, f"port-{sc}")
            srv = subprocess.Popen([sys.executable, os.path.join(HERE, "fake_imapd.py"), sc, portfile, cert, key])
            for _ in range(100):
                if os.path.exists(portfile) and open(portfile).read():
                    break
                time.sleep(0.05)
            port = open(portfile).read()
            client = subprocess.run([exe, "imap", sc, port], env=env, timeout=30)
            server = srv.wait(timeout=30)
            if client.returncode or server:
                failed = True
                print(f"scenario {sc}: client {client.returncode}, server {server}")
        print("ALL PASSED" if not failed else "FAILURES")
        return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
