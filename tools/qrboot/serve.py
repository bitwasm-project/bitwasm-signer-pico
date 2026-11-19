"""Serve the browser loader on localhost with the headers its shared memory needs.

  python3 tools/qrboot/serve.py [dir] [port]     defaults: build/qrboot/web 8000
"""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        # SharedArrayBuffer is only given to cross-origin isolated pages
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


root = sys.argv[1] if len(sys.argv) > 1 else "build/qrboot/web"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8000
http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=root)).serve_forever()
