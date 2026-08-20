#!/usr/bin/env python3
# tools/test_server.py — minimal HTTP echo server for end-to-end relay testing.
#
# Listens on 0.0.0.0:8080, logs every request (method, path, headers, body) and
# replies with a short ACK. Used as the server_url target for the D1 mini relay
# during phase 4 verification. Run on the PC that is on the same LAN as the D1 mini.
#
# Usage:  python tools/test_server.py [port]      (default port 8080)
#
# NOTE: on Windows, the first run may prompt to allow Python through the firewall
#       — allow it on private networks, otherwise the D1 mini cannot reach the PC.

import sys
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080


class Handler(BaseHTTPRequestHandler):
    def _log_and_reply(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length else b""
        print(f"\n--- {self.command} {self.path} from {self.client_address[0]}")
        for k, v in self.headers.items():
            print(f"    {k}: {v}")
        print(f"    BODY ({len(body)} B): {body!r}")
        sys.stdout.flush()
        resp = b"ACK:" + body[:120]
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)

    def do_GET(self):
        self._log_and_reply()

    def do_POST(self):
        self._log_and_reply()


if __name__ == "__main__":
    HTTPServer(("0.0.0.0", PORT), Handler).serve_forever()