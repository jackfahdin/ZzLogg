#!/usr/bin/env python3
"""Exercise the explicit TLS probe against local TLS and HTTP CONNECT fixtures."""
import hashlib
import http.server
import json
import os
from pathlib import Path
import select
import socket
import socketserver
import ssl
import subprocess
import tempfile
import threading
import time
import unittest

PAYLOAD = b"ZzLogg TLS audit\n"
BINARY = Path(os.environ.get("ZZLOGG_TLS_PROBE", "out/build/p6-boolean/output/zzlogg_tls_probe")).resolve()


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/stall":
            time.sleep(0.5)
            return
        if self.path in ("/redirect", "/downgrade", "/loop"):
            self.send_response(302)
            target = {"/redirect": "/payload", "/downgrade": "http://localhost:1/payload",
                      "/loop": "/loop"}[self.path]
            self.send_header("Location", target)
            self.send_header("Content-Length", "13")
            self.end_headers()
            self.wfile.write(b"redirect body")
            return
        self.send_response(200 if self.path == "/payload" else 404)
        self.send_header("Content-Length", str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, *_):
        pass


class Proxy(socketserver.StreamRequestHandler):
    def handle(self):
        line = self.rfile.readline().decode("ascii")
        while self.rfile.readline() not in (b"\r\n", b""):
            pass
        method, authority, _ = line.split()
        host, port = authority.rsplit(":", 1)
        # Fixture only permits its own TLS server, never arbitrary destinations.
        if method != "CONNECT" or host != "localhost" or int(port) != self.server.tls_port:
            return
        with socket.create_connection(("127.0.0.1", int(port)), timeout=5) as upstream:
            self.server.connections += 1
            self.wfile.write(b"HTTP/1.1 200 Connection established\r\n\r\n")
            self.wfile.flush()
            streams = [self.connection, upstream]
            while True:
                ready, _, _ = select.select(streams, [], [], 5)
                if not ready:
                    return
                for source in ready:
                    data = source.recv(65536)
                    if not data:
                        return
                    (upstream if source is self.connection else self.connection).sendall(data)


class TlsProbeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise AssertionError(f"Build zzlogg_tls_probe first: {BINARY}")
        cls.temp = tempfile.TemporaryDirectory(prefix="zzlogg-tls-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.ca = Path(cls.temp.name) / "cert.pem"
        key = Path(cls.temp.name) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(key), "-out", str(cls.ca), "-days", "2",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                       check=True, capture_output=True)
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cls.ca, key)
        cls.server.socket = context.wrap_socket(cls.server.socket, server_side=True)
        cls.port = cls.server.server_port
        cls.proxy = socketserver.ThreadingTCPServer(("127.0.0.1", 0), Proxy)
        cls.proxy.daemon_threads = True
        cls.proxy.tls_port = cls.port
        cls.proxy.connections = 0
        for server in (cls.server, cls.proxy):
            threading.Thread(target=server.serve_forever, daemon=True).start()
            cls.addClassCleanup(server.server_close)
            cls.addClassCleanup(server.shutdown)

    def probe(self, path="/payload", *, trust=True, success=True, host="localhost", extra=(), digest=True):
        command = [str(BINARY), "--url", f"https://{host}:{self.port}{path}",
                   "--timeout-ms", "5000", "--proxy", "direct"]
        if trust:
            command += ["--ca-file", str(self.ca)]
        if digest:
            command += ["--sha256", hashlib.sha256(PAYLOAD).hexdigest()]
        command += list(extra)
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report["passed"], success)
        self.assertTrue(report["active_backend"])
        return report

    def test_verified_content(self):
        report = self.probe()
        self.assertTrue(report["encrypted"])
        self.assertEqual(report["bytes"], len(PAYLOAD))
        self.assertEqual(report["ssl_errors"], [])

    def test_wrong_hash(self):
        report = self.probe(success=False, digest=False, extra=("--sha256", "0" * 64))
        self.assertEqual(report["http_status"], 200)

    def test_untrusted_certificate_is_specific_failure(self):
        report = self.probe(trust=False, digest=False, extra=("--expect-tls-error",))
        self.assertTrue(report["ssl_errors"])

    def test_untrusted_certificate_cannot_pass_download(self):
        self.probe(trust=False, success=False)

    def test_wrong_hostname(self):
        report = self.probe(host="127.0.0.1", digest=False, extra=("--expect-tls-error",))
        self.assertTrue(report["ssl_errors"])

    def test_valid_certificate_cannot_pass_negative_case(self):
        self.probe(digest=False, success=False, extra=("--expect-tls-error",))

    def test_https_redirect(self):
        report = self.probe("/redirect")
        self.assertTrue(report["final_url"].endswith("/payload"))

    def test_downgrade_rejected(self):
        self.probe("/downgrade", success=False)

    def test_http_error_cannot_pass_negative_case(self):
        self.probe("/missing", digest=False, success=False, extra=("--expect-tls-error",))

    def test_timeout_cannot_pass_negative_case(self):
        report = self.probe("/stall", digest=False, success=False,
                            extra=("--expect-tls-error", "--timeout-ms", "100"))
        self.assertTrue(report["timed_out"])

    def test_redirect_loop_is_bounded(self):
        self.probe("/loop", success=False)

    def test_connection_failure_cannot_pass_negative_case(self):
        with socket.socket() as unused:
            unused.bind(("127.0.0.1", 0))
            # Bound but not listening: the port cannot be acquired by another test.
            url = f"https://localhost:{unused.getsockname()[1]}/"
            self.probe(digest=False, success=False, extra=("--url", url, "--expect-tls-error"))

    def test_connect_proxy(self):
        before = self.proxy.connections
        self.probe(extra=("--proxy", f"http://127.0.0.1:{self.proxy.server_address[1]}"))
        self.assertGreater(self.proxy.connections, before)

    def test_unavailable_backend(self):
        result = subprocess.run([str(BINARY), "--inspect", "--backend", "nonexistent-zzlogg"],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(json.loads(result.stdout)["passed"])


if __name__ == "__main__":
    unittest.main()
